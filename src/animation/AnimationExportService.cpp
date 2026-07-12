#include "AnimationExportService.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

#include "core/AppPaths.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "io/ImageCodec.hpp"

namespace anim {

// ── Format helpers ───────────────────────────────────────────────────────────

bool isSequenceFormat(ExportFormat format)
{
    switch (format) {
        case ExportFormat::Mp4:
        case ExportFormat::Gif:
            return false;
        case ExportFormat::PngSequence:
        case ExportFormat::JpegSequence:
        case ExportFormat::WebpSequence:
            return true;
    }
    return true;
}

bool formatSupportsAlpha(ExportFormat format)
{
    switch (format) {
        case ExportFormat::Mp4:
        case ExportFormat::JpegSequence:
            return false;
        case ExportFormat::Gif:            // 1-bit transparency
        case ExportFormat::PngSequence:
        case ExportFormat::WebpSequence:
            return true;
    }
    return false;
}

QString extensionForFormat(ExportFormat format)
{
    switch (format) {
        case ExportFormat::Mp4:          return QStringLiteral("mp4");
        case ExportFormat::Gif:          return QStringLiteral("gif");
        case ExportFormat::PngSequence:  return QStringLiteral("png");
        case ExportFormat::JpegSequence: return QStringLiteral("jpg");
        case ExportFormat::WebpSequence: return QStringLiteral("webp");
    }
    return QStringLiteral("png");
}

namespace {

QString frameToken(Frame frame, int padding)
{
    const qint64 value = frame;
    const bool negative = value < 0;
    const qint64 magnitude = negative ? -value : value;
    QString token = QString::number(magnitude).rightJustified(
        std::max(1, padding), QLatin1Char('0'));
    if (negative)
        token.prepend(QLatin1Char('-'));
    return token;
}

QString sequenceOutputPath(const QDir& directory, const QString& prefix, Frame frame,
                           int padding, const QString& extension)
{
    return directory.filePath(QStringLiteral("%1_%2.%3")
        .arg(prefix, frameToken(frame, padding), extension));
}

// Composite `frame` on the isolated snapshot, then map the canvas onto the
// output resolution using the requested fit mode / background. Never touches
// the live document.
QImage renderExportFrame(Document* snapshot, Frame frame,
                         const AnimationExportRequest& req,
                         const QSize& pipeSize, bool wantAlpha)
{
    snapshot->setCurrentFrame(frame);
    QImage src = ::compositeImage(snapshot);
    if (src.isNull())
        return QImage();
    if (src.format() != QImage::Format_RGBA8888)
        src = src.convertToFormat(QImage::Format_RGBA8888);

    const QSize canvas = src.size();
    // Same size and no flattening required: hand the composite straight through.
    if (pipeSize == canvas && wantAlpha)
        return src;

    QImage out(pipeSize, QImage::Format_RGBA8888);
    out.fill(wantAlpha ? QColor(Qt::transparent) : req.backgroundColor);

    QRectF dst;
    switch (req.fitMode) {
        case FitMode::Stretch:
            dst = QRectF(0, 0, pipeSize.width(), pipeSize.height());
            break;
        case FitMode::Fit: {
            const QSize s = canvas.scaled(pipeSize, Qt::KeepAspectRatio);
            dst = QRectF((pipeSize.width() - s.width()) / 2.0,
                         (pipeSize.height() - s.height()) / 2.0,
                         s.width(), s.height());
            break;
        }
        case FitMode::Fill: {
            const QSize s = canvas.scaled(pipeSize, Qt::KeepAspectRatioByExpanding);
            dst = QRectF((pipeSize.width() - s.width()) / 2.0,
                         (pipeSize.height() - s.height()) / 2.0,
                         s.width(), s.height());
            break;
        }
    }

    QPainter painter(&out);
    const bool smooth = (req.resampleMode == Qt::SmoothTransformation);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    painter.setRenderHint(QPainter::Antialiasing, smooth);
    painter.drawImage(dst, src);
    painter.end();
    return out;
}

QString readTextFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll()).trimmed();
}

// Write one processed frame's raw RGBA8888 bytes to the encoder's stdin. Rows
// are written individually so an unexpected stride never corrupts the stream.
bool writeFrameToProcess(QProcess& proc, const QImage& img)
{
    const int rowBytes = img.width() * 4;
    for (int y = 0; y < img.height(); ++y) {
        const char* row = reinterpret_cast<const char*>(img.constScanLine(y));
        qint64 offset = 0;
        while (offset < rowBytes) {
            if (proc.state() != QProcess::Running)
                return false;
            const qint64 n = proc.write(row + offset, rowBytes - offset);
            if (n < 0)
                return false;
            offset += n;
            // Flush to bound memory; a false return here is only fatal if the
            // process has actually stopped, which the loop head re-checks.
            proc.waitForBytesWritten(30000);
        }
    }
    return true;
}

// ── MP4 / GIF via FFmpeg ─────────────────────────────────────────────────────

AnimationExportResult exportVideo(const std::shared_ptr<Document>& snapshot,
                                  const AnimationExportRequest& req,
                                  const std::shared_ptr<ExportProgress>& progress)
{
    AnimationExportResult result;

    const QString outPath = req.outputFile.trimmed();
    if (outPath.isEmpty()) {
        result.errorMessage = QStringLiteral("No output file was provided.");
        return result;
    }
    if (!req.overwriteExisting && QFile::exists(outPath)) {
        result.errorMessage = QStringLiteral("Output file already exists: %1").arg(outPath);
        return result;
    }

    const QString ffmpeg = AnimationExportService::detectFfmpegExecutable();
    if (ffmpeg.isEmpty()) {
        result.errorMessage = QStringLiteral(
            "FFmpeg was not found. Install FFmpeg and make sure it is on your PATH "
            "(or set the HAZOR_FFMPEG environment variable) to export MP4 or GIF.");
        return result;
    }

    const bool isMp4 = (req.format == ExportFormat::Mp4);
    const bool wantAlpha = formatSupportsAlpha(req.format) && req.transparency;

    QSize pipeSize = req.targetSize;
    if (isMp4) {
        // yuv420p and most codecs require even dimensions.
        pipeSize.setWidth(std::max(2, pipeSize.width() & ~1));
        pipeSize.setHeight(std::max(2, pipeSize.height() & ~1));
    }
    if (pipeSize.isEmpty()) {
        result.errorMessage = QStringLiteral("The output resolution is invalid.");
        return result;
    }

    const QString fpsArg = QString::number(req.fps, 'f', 6);

    QStringList args;
    args << QStringLiteral("-y") << QStringLiteral("-hide_banner")
         << QStringLiteral("-loglevel") << QStringLiteral("error")
         << QStringLiteral("-f") << QStringLiteral("rawvideo")
         << QStringLiteral("-pixel_format") << QStringLiteral("rgba")
         << QStringLiteral("-video_size")
         << QStringLiteral("%1x%2").arg(pipeSize.width()).arg(pipeSize.height())
         << QStringLiteral("-framerate") << fpsArg
         << QStringLiteral("-i") << QStringLiteral("-");

    if (isMp4) {
        const QString codec = req.hardwareEncoder
            ? QStringLiteral("h264_nvenc")
            : (req.videoCodec.isEmpty() ? QStringLiteral("libx264") : req.videoCodec);
        args << QStringLiteral("-an")
             << QStringLiteral("-c:v") << codec
             << QStringLiteral("-pix_fmt") << req.pixelFormat;
        if (req.hardwareEncoder) {
            args << QStringLiteral("-cq") << QString::number(req.crf);
        } else {
            args << QStringLiteral("-crf") << QString::number(req.crf)
                 << QStringLiteral("-preset") << req.encoderPreset;
        }
        args << QStringLiteral("-r") << fpsArg
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    } else {
        // Single-pass, high-quality palette: generate a palette from the whole
        // clip, then apply it. reserve_transparent keeps a GIF transparency slot.
        const QString dither = req.gifDither ? QStringLiteral("sierra2_4a")
                                             : QStringLiteral("none");
        const QString paletteGen = wantAlpha
            ? QStringLiteral("palettegen=max_colors=%1:reserve_transparent=1")
                  .arg(req.gifColors)
            : QStringLiteral("palettegen=max_colors=%1").arg(req.gifColors);
        const QString paletteUse = wantAlpha
            ? QStringLiteral("paletteuse=dither=%1:alpha_threshold=128").arg(dither)
            : QStringLiteral("paletteuse=dither=%1").arg(dither);
        args << QStringLiteral("-vf")
             << QStringLiteral("split[s0][s1];[s0]%1[p];[s1][p]%2")
                    .arg(paletteGen, paletteUse)
             << QStringLiteral("-loop") << QString::number(req.gifLoop);
    }
    args << QDir::toNativeSeparators(outPath);

    const QString logPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("hazor-ffmpeg-%1.log")
            .arg(QCoreApplication::applicationPid()));

    QProcess proc;
    proc.setProgram(ffmpeg);
    proc.setArguments(args);
    proc.setStandardOutputFile(QProcess::nullDevice());
    proc.setStandardErrorFile(logPath);  // avoids a full-stderr-pipe deadlock
    proc.start();
    if (!proc.waitForStarted(8000)) {
        QFile::remove(logPath);
        result.errorMessage = QStringLiteral("Could not start FFmpeg: %1")
            .arg(proc.errorString());
        return result;
    }

    const Frame start = req.startFrame;
    const Frame end = req.endFrame;
    if (progress)
        progress->totalFrames.store(static_cast<qint64>(end) - start + 1,
                                    std::memory_order_relaxed);

    bool cancelled = false;
    QString frameError;
    for (qint64 f = start; f <= static_cast<qint64>(end); ++f) {
        if (progress && progress->cancelRequested.load(std::memory_order_relaxed)) {
            cancelled = true;
            break;
        }
        const QImage img = renderExportFrame(snapshot.get(), static_cast<Frame>(f),
                                             req, pipeSize, wantAlpha);
        if (img.isNull()) {
            frameError = QStringLiteral("Could not render frame %1.").arg(f);
            break;
        }
        if (!writeFrameToProcess(proc, img)) {
            frameError = QStringLiteral("FFmpeg stopped accepting input.");
            break;
        }
        if (progress)
            progress->completedFrames.fetch_add(1, std::memory_order_relaxed);
    }

    proc.closeWriteChannel();

    if (cancelled) {
        proc.kill();
        proc.waitForFinished(3000);
        QFile::remove(outPath);         // never leave a partial file behind
        QFile::remove(logPath);
        result.cancelled = true;
        return result;
    }

    if (!proc.waitForFinished(180000)) {
        proc.kill();
        proc.waitForFinished(2000);
        if (frameError.isEmpty())
            frameError = QStringLiteral("FFmpeg timed out while encoding.");
    }

    const bool crashed = (proc.exitStatus() != QProcess::NormalExit);
    const int exitCode = proc.exitCode();
    const QString log = readTextFile(logPath);
    QFile::remove(logPath);

    if (!frameError.isEmpty() || crashed || exitCode != 0) {
        QFile::remove(outPath);
        QString message = frameError.isEmpty()
            ? QStringLiteral("FFmpeg failed (exit code %1).").arg(exitCode)
            : frameError;
        if (!log.isEmpty())
            message += QStringLiteral("\n\n%1").arg(log);
        result.errorMessage = message;
        return result;
    }

    result.ok = true;
    result.outputPath = outPath;
    result.exportedFrameCount = static_cast<qint64>(end) - start + 1;
    return result;
}

// ── Image sequence ───────────────────────────────────────────────────────────

bool writeSequenceFrame(const QImage& img, const QString& path,
                        const ExportOptions& opts, const ColorProfile& profile,
                        QString* error)
{
    const QString extension = normalizeImageExtension(opts.format);
    ImageCodec* codec = imageCodecRegistry().findWriter(extension);
    if (!codec) {
        if (error)
            *error = QStringLiteral("The selected image format cannot be written "
                                    "by this build.");
        return false;
    }

    ImageSaveOptions saveOptions;
    saveOptions.quality = opts.quality >= 0 ? opts.quality : 90;
    saveOptions.compressionLevel = opts.compression;
    saveOptions.progressive = opts.progressive;
    saveOptions.preserveAlpha = opts.preserveAlpha;

    DocumentImage image = convertQImageToDocumentImageValue(img, QString(), extension);
    if (profile.isValid()) {
        image.colorProfile = profile;
        image.iccProfile = profile.iccBytes();
    }
    saveOptions.embedColorProfile = !image.iccProfile.isEmpty();
    return codec->write(path, image, saveOptions, error);
}

AnimationExportResult exportSequence(const std::shared_ptr<Document>& snapshot,
                                     const AnimationExportRequest& req,
                                     const std::shared_ptr<ExportProgress>& progress)
{
    AnimationExportResult result;

    if (req.outputDirectory.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("No output directory was provided.");
        return result;
    }
    if (req.fileNamePrefix.trimmed().isEmpty()
        || QFileInfo(req.fileNamePrefix).fileName() != req.fileNamePrefix) {
        result.errorMessage = QStringLiteral("The sequence filename prefix is invalid.");
        return result;
    }

    const QString extension = extensionForFormat(req.format);
    if (!imageCodecRegistry().findWriter(extension)) {
        result.errorMessage = QStringLiteral(
            "The selected image format cannot be written by this build.");
        return result;
    }

    QDir directory(req.outputDirectory);
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        result.errorMessage = QStringLiteral("Could not create the output directory.");
        return result;
    }
    directory = QDir(directory.absolutePath());

    const Frame start = req.startFrame;
    const Frame end = req.endFrame;

    // Preflight the whole range so overwrite protection fails before any frame
    // is written.
    if (!req.overwriteExisting) {
        for (qint64 f = start; f <= static_cast<qint64>(end); ++f) {
            const QString path = sequenceOutputPath(directory, req.fileNamePrefix,
                                                    static_cast<Frame>(f),
                                                    req.framePadding, extension);
            if (QFile::exists(path)) {
                result.errorMessage = QStringLiteral("Output file already exists: %1")
                    .arg(path);
                return result;
            }
        }
    }

    const QSize pipeSize = req.targetSize;
    if (pipeSize.isEmpty()) {
        result.errorMessage = QStringLiteral("The output resolution is invalid.");
        return result;
    }
    const bool wantAlpha = formatSupportsAlpha(req.format) && req.transparency;

    ExportOptions imageOptions = req.imageOptions;
    imageOptions.format = extension;
    imageOptions.preserveAlpha = wantAlpha;
    const ColorProfile profile = snapshot->colorProfile();

    if (progress)
        progress->totalFrames.store(static_cast<qint64>(end) - start + 1,
                                    std::memory_order_relaxed);

    for (qint64 f = start; f <= static_cast<qint64>(end); ++f) {
        if (progress && progress->cancelRequested.load(std::memory_order_relaxed)) {
            // Remove only the files this operation created.
            for (const QString& written : result.writtenFiles)
                QFile::remove(written);
            result.cancelled = true;
            result.writtenFiles.clear();
            return result;
        }

        const QImage img = renderExportFrame(snapshot.get(), static_cast<Frame>(f),
                                             req, pipeSize, wantAlpha);
        if (img.isNull()) {
            result.errorMessage = QStringLiteral("Could not render frame %1.").arg(f);
            return result;
        }

        const QString path = sequenceOutputPath(directory, req.fileNamePrefix,
                                                static_cast<Frame>(f),
                                                req.framePadding, extension);
        QString error;
        if (!writeSequenceFrame(img, path, imageOptions, profile, &error)) {
            result.errorMessage = error.isEmpty()
                ? QStringLiteral("Could not export frame %1.").arg(f)
                : QStringLiteral("Could not export frame %1: %2").arg(f).arg(error);
            return result;
        }
        ++result.exportedFrameCount;
        result.writtenFiles.push_back(path);
        if (progress)
            progress->completedFrames.fetch_add(1, std::memory_order_relaxed);
    }

    result.ok = true;
    result.outputPath = directory.absolutePath();
    return result;
}

} // namespace

// ── Snapshot ─────────────────────────────────────────────────────────────────

std::shared_ptr<Document> AnimationExportService::createRenderSnapshot(
    const Document& source)
{
    auto snapshot = std::make_shared<Document>();
    snapshot->name = source.name;
    snapshot->size = source.size;
    snapshot->resolutionDpi = source.resolutionDpi;
    snapshot->colorMode = source.colorMode;
    snapshot->bitDepth = source.bitDepth;
    snapshot->activeFlatIndex = source.activeFlatIndex;
    snapshot->perfConfig = source.perfConfig;
    snapshot->animation = source.animation;
    snapshot->setColorProfile(source.colorProfile());
    snapshot->setProfileSource(source.profileSource());

    snapshot->roots.reserve(source.roots.size());
    for (const auto& root : source.roots) {
        if (root)
            snapshot->roots.push_back(root->shallowClone());
    }

    // Preserve the source frame in the snapshot, then export can move
    // independently from this private playhead.
    snapshot->setCurrentFrame(source.currentFrame());
    return snapshot;
}

// ── FFmpeg discovery ─────────────────────────────────────────────────────────

QString AnimationExportService::detectFfmpegExecutable()
{
    auto runnable = [](const QString& path) -> QString {
        if (path.isEmpty())
            return QString();
        const QFileInfo fi(path);
        if (fi.exists() && fi.isFile() && fi.isExecutable())
            return fi.absoluteFilePath();
        return QString();
    };

    // 1. Explicit override.
    const QByteArray env = qgetenv("HAZOR_FFMPEG");
    if (!env.isEmpty()) {
        const QString r = runnable(QString::fromLocal8Bit(env));
        if (!r.isEmpty())
            return r;
    }

#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("ffmpeg.exe");
#else
    const QString exe = QStringLiteral("ffmpeg");
#endif

    // 2. Bundled under the read-only 3rdparty tree.
    const QString thirdParty = AppPaths::thirdPartyDir();
    for (const QString& cand : {
             QDir(thirdParty).filePath(QStringLiteral("ffmpeg/bin/") + exe),
             QDir(thirdParty).filePath(QStringLiteral("ffmpeg/") + exe) }) {
        const QString r = runnable(cand);
        if (!r.isEmpty())
            return r;
    }

    // 3. Next to the executable.
    const QString r = runnable(
        QDir(QCoreApplication::applicationDirPath()).filePath(exe));
    if (!r.isEmpty())
        return r;

    // 4. System PATH.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!onPath.isEmpty())
        return onPath;

    return QString();
}

bool AnimationExportService::isFfmpegAvailable()
{
    return !detectFfmpegExecutable().isEmpty();
}

// ── Dispatch ─────────────────────────────────────────────────────────────────

AnimationExportResult AnimationExportService::exportAnimation(
    const std::shared_ptr<Document>& snapshot,
    const AnimationExportRequest& request,
    const std::shared_ptr<ExportProgress>& progress)
{
    AnimationExportResult result;
    if (!snapshot) {
        result.errorMessage = QStringLiteral("No document snapshot was provided.");
        return result;
    }
    if (snapshot->size.isEmpty()) {
        result.errorMessage = QStringLiteral("The document has no exportable canvas.");
        return result;
    }
    if (request.targetSize.isEmpty()) {
        result.errorMessage = QStringLiteral("The output resolution is invalid.");
        return result;
    }

    // Clamp the requested range to the document's animation range.
    const Frame docStart = snapshot->animation.startFrame();
    const Frame docEnd = std::max(docStart, snapshot->animation.endFrame());
    AnimationExportRequest req = request;
    req.startFrame = std::clamp(request.startFrame, docStart, docEnd);
    req.endFrame = std::clamp(request.endFrame, docStart, docEnd);
    if (req.endFrame < req.startFrame) {
        result.errorMessage = QStringLiteral("The export frame range is invalid.");
        return result;
    }
    if (!(req.fps > 0.0))
        req.fps = snapshot->animation.fps();

    if (progress) {
        progress->completedFrames.store(0, std::memory_order_relaxed);
        progress->totalFrames.store(
            static_cast<qint64>(req.endFrame) - req.startFrame + 1,
            std::memory_order_relaxed);
    }

    return isSequenceFormat(req.format)
        ? exportSequence(snapshot, req, progress)
        : exportVideo(snapshot, req, progress);
}

QFuture<AnimationExportResult> AnimationExportService::exportAnimationAsync(
    const Document& source,
    AnimationExportRequest request,
    std::shared_ptr<ExportProgress> progress)
{
    std::shared_ptr<Document> snapshot = createRenderSnapshot(source);
    return QtConcurrent::run(
        [snapshot = std::move(snapshot), request = std::move(request),
         progress = std::move(progress)]() {
            return exportAnimation(snapshot, request, progress);
        });
}

} // namespace anim
