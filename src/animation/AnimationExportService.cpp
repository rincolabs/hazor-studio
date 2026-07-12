#include "AnimationExportService.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "io/ImageCodec.hpp"

namespace anim {
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

QString outputPath(const QDir& directory, const QString& prefix, Frame frame,
                   int padding, const QString& extension)
{
    return directory.filePath(QStringLiteral("%1_%2.%3")
        .arg(prefix, frameToken(frame, padding), extension));
}

} // namespace

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

    // Preserve the source frame in the snapshot, then sequence export can move
    // independently from this private playhead.
    snapshot->setCurrentFrame(source.currentFrame());
    return snapshot;
}

SequenceExportResult AnimationExportService::exportSequence(
    const std::shared_ptr<Document>& snapshot,
    const SequenceExportOptions& options,
    const std::shared_ptr<std::atomic_bool>& cancelRequested)
{
    SequenceExportResult result;
    if (!snapshot) {
        result.errorMessage = QStringLiteral("No document snapshot was provided.");
        return result;
    }
    if (snapshot->size.isEmpty()) {
        result.errorMessage = QStringLiteral("The document has no exportable canvas.");
        return result;
    }
    if (options.outputDirectory.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("No output directory was provided.");
        return result;
    }
    if (options.fileNamePrefix.trimmed().isEmpty()
        || QFileInfo(options.fileNamePrefix).fileName() != options.fileNamePrefix) {
        result.errorMessage = QStringLiteral("The sequence filename prefix is invalid.");
        return result;
    }

    const Frame documentStart = snapshot->animation.startFrame();
    const Frame documentEnd = std::max(documentStart, snapshot->animation.endFrame());
    Frame start = options.startFrame.value_or(snapshot->animation.playbackStart());
    Frame end = options.endFrame.value_or(snapshot->animation.playbackEnd());
    start = std::clamp(start, documentStart, documentEnd);
    end = std::clamp(end, documentStart, documentEnd);
    if (end < start) {
        result.errorMessage = QStringLiteral("The export frame range is invalid.");
        return result;
    }

    QString extension = normalizeImageExtension(options.imageOptions.format);
    if (extension.isEmpty())
        extension = QStringLiteral("png");
    if (!imageCodecRegistry().findWriter(extension)) {
        result.errorMessage = QStringLiteral(
            "The selected image format cannot be written by this build.");
        return result;
    }

    QDir directory(options.outputDirectory);
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        result.errorMessage = QStringLiteral("Could not create the output directory.");
        return result;
    }
    directory = QDir(directory.absolutePath());

    // Preflight the entire range so overwrite protection fails before writing a
    // partial sequence.
    if (!options.overwriteExisting) {
        for (qint64 frame = start; frame <= static_cast<qint64>(end); ++frame) {
            const QString path = outputPath(directory, options.fileNamePrefix,
                                            static_cast<Frame>(frame),
                                            options.framePadding, extension);
            if (QFile::exists(path)) {
                result.errorMessage = QStringLiteral("Output file already exists: %1")
                    .arg(path);
                return result;
            }
        }
    }

    ExportOptions imageOptions = options.imageOptions;
    imageOptions.format = extension;
    for (qint64 frame = start; frame <= static_cast<qint64>(end); ++frame) {
        if (cancelRequested
            && cancelRequested->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }

        const Frame currentFrame = static_cast<Frame>(frame);
        snapshot->setCurrentFrame(currentFrame);
        const QString path = outputPath(directory, options.fileNamePrefix,
                                        currentFrame, options.framePadding,
                                        extension);
        QString error;
        if (!saveImage(snapshot.get(), path, imageOptions, &error)) {
            result.errorMessage = error.isEmpty()
                ? QStringLiteral("Could not export frame %1.").arg(currentFrame)
                : QStringLiteral("Could not export frame %1: %2")
                    .arg(currentFrame).arg(error);
            return result;
        }
        ++result.exportedFrameCount;
        result.writtenFiles.push_back(path);
    }

    result.ok = true;
    return result;
}

QFuture<SequenceExportResult> AnimationExportService::exportSequenceAsync(
    const Document& source,
    SequenceExportOptions options,
    std::shared_ptr<std::atomic_bool> cancelRequested)
{
    std::shared_ptr<Document> snapshot = createRenderSnapshot(source);
    return QtConcurrent::run(
        [snapshot = std::move(snapshot), options = std::move(options),
         cancelRequested = std::move(cancelRequested)]() {
            return exportSequence(snapshot, options, cancelRequested);
        });
}

} // namespace anim
