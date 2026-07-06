#include "ai/upscale/RealEsrganProcessBackend.hpp"

#include "ai/models/AiModelRegistry.hpp"
#include "core/AppPaths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

namespace {

QString logRoot()
{
    return AppPaths::subDir(QStringLiteral("logs/ai"));
}

QString jobsRoot()
{
    return AppPaths::cacheSubDir(QStringLiteral("ai-jobs/upscale"));
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

QString modelBaseName(const QString& modelId)
{
    return modelId.trimmed().isEmpty() ? QStringLiteral("realesrgan-x4plus") : modelId.trimmed();
}

QImage preserveAlphaFromSource(const QImage& source, const QImage& upscaled)
{
    if (source.isNull() || upscaled.isNull() || !source.hasAlphaChannel())
        return upscaled.convertToFormat(QImage::Format_RGBA8888);

    QImage result = upscaled.convertToFormat(QImage::Format_ARGB32);
    QImage sourceRgba = source.convertToFormat(QImage::Format_ARGB32);
    QImage alpha(sourceRgba.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < sourceRgba.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(sourceRgba.constScanLine(y));
        uchar* dst = alpha.scanLine(y);
        for (int x = 0; x < sourceRgba.width(); ++x)
            dst[x] = static_cast<uchar>(qAlpha(src[x]));
    }
    alpha = alpha.scaled(result.size(), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
    result.setAlphaChannel(alpha);
    return result.convertToFormat(QImage::Format_RGBA8888);
}

void appendLine(QFile& file, const QString& line)
{
    if (!file.isOpen())
        return;
    QTextStream out(&file);
    out << line << '\n';
}

UpscaleBackendStatus makeStatus(UpscaleBackendStatusCode code,
                                const QString& userMessage,
                                const QString& technicalDetails = QString())
{
    UpscaleBackendStatus status;
    status.code = code;
    status.userMessage = userMessage;
    status.technicalDetails = technicalDetails;
    status.executablePath = RealEsrganProcessBackend::resolveExecutablePath();
    status.modelDirectory = RealEsrganProcessBackend::packagedModelsDirectory();
    return status;
}

} // namespace

QString RealEsrganProcessBackend::id() const
{
    return QStringLiteral("realesrgan-process");
}

QString RealEsrganProcessBackend::displayName() const
{
    return QStringLiteral("Real-ESRGAN ncnn Vulkan");
}

QString RealEsrganProcessBackend::resolveExecutablePath()
{
    QSettings settings;
    const QString override = settings.value(QStringLiteral("ai/upscale/realesrganExecutable")).toString().trimmed();
    if (!override.isEmpty())
        return override;

    // Flat layout: the binary lives directly under 3rdparty/realesrgan (no per-OS
    // folder). Only the Windows file extension differs.
    const QString tp = AppPaths::thirdPartyDir();
#ifdef Q_OS_WIN
    return QDir(tp).filePath(QStringLiteral("realesrgan/realesrgan-ncnn-vulkan.exe"));
#else
    return QDir(tp).filePath(QStringLiteral("realesrgan/realesrgan-ncnn-vulkan"));
#endif
}

QString RealEsrganProcessBackend::packagedModelsDirectory()
{
    return QDir(AppPaths::thirdPartyDir()).filePath(QStringLiteral("realesrgan/models"));
}

QString RealEsrganProcessBackend::defaultModelDirectory(const QString& modelId)
{
    const QString installed = AiModelRegistry::instance()->modelDirectory(modelId);
    if (!installed.isEmpty())
        return installed;
    return packagedModelsDirectory();
}

QStringList RealEsrganProcessBackend::expectedModelFiles(const QString& modelId)
{
    const QString base = modelBaseName(modelId);
    if (base == QLatin1String("realesr-animevideov3")) {
        return {
            QStringLiteral("realesr-animevideov3-x2.param"),
            QStringLiteral("realesr-animevideov3-x2.bin"),
            QStringLiteral("realesr-animevideov3-x3.param"),
            QStringLiteral("realesr-animevideov3-x3.bin"),
            QStringLiteral("realesr-animevideov3-x4.param"),
            QStringLiteral("realesr-animevideov3-x4.bin")
        };
    }
    return {
        base + QStringLiteral(".param"),
        base + QStringLiteral(".bin")
    };
}

UpscaleBackendStatus RealEsrganProcessBackend::probe()
{
    QSettings settings;
    if (settings.value(QStringLiteral("ai/upscale/realesrganDisabled"), false).toBool()) {
        return makeStatus(UpscaleBackendStatusCode::DisabledByUser,
                          QCoreApplication::translate("RealEsrganProcessBackend",
                              "Real-ESRGAN is disabled in Settings > AI."));
    }

    const QString exe = resolveExecutablePath();
    QFileInfo exeInfo(exe);
    if (!exeInfo.exists() || !exeInfo.isFile()) {
        return makeStatus(UpscaleBackendStatusCode::MissingExecutable,
                          QCoreApplication::translate("RealEsrganProcessBackend",
                              "Real-ESRGAN is not available. The backend executable was not found."),
                          exe);
    }
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    if (!exeInfo.isExecutable()) {
        return makeStatus(UpscaleBackendStatusCode::ExecutableNotRunnable,
                          QCoreApplication::translate("RealEsrganProcessBackend",
                              "Real-ESRGAN could not start. Check that the file has execute permission."),
                          exe);
    }
#endif

    const QString packagedDir = packagedModelsDirectory();
    const QStringList packagedModelIds = {
        QStringLiteral("realesrgan-x4plus"),
        QStringLiteral("realesrgan-x4plus-anime"),
        QStringLiteral("realesr-animevideov3")
    };
    bool hasAnyPackagedModel = false;
    if (QFileInfo(packagedDir).isDir()) {
        for (const QString& modelId : packagedModelIds) {
            bool complete = true;
            for (const QString& file : expectedModelFiles(modelId)) {
                if (!QFile::exists(QDir(packagedDir).filePath(file))) {
                    complete = false;
                    break;
                }
            }
            if (complete) {
                hasAnyPackagedModel = true;
                break;
            }
        }
    }

    const QList<AiModelDescriptor> installed = AiModelRegistry::instance()->installedModelsForTask(QStringLiteral("upscale"));
    QString modelDir = (hasAnyPackagedModel || installed.isEmpty()) ? packagedDir : installed.first().rootDir;
    QFileInfo modelInfo(modelDir);
    if (!modelInfo.exists() || !modelInfo.isDir()) {
        auto status = makeStatus(UpscaleBackendStatusCode::MissingModelDirectory,
                                 QCoreApplication::translate("RealEsrganProcessBackend",
                                     "Real-ESRGAN did not find its model folder."),
                                 modelDir);
        status.modelDirectory = modelDir;
        return status;
    }

    QStringList missing;
    if (hasAnyPackagedModel || installed.isEmpty()) {
        bool hasAnyCompleteModel = false;
        for (const QString& modelId : packagedModelIds) {
            bool complete = true;
            for (const QString& file : expectedModelFiles(modelId)) {
                if (!QFile::exists(QDir(modelDir).filePath(file))) {
                    missing << file;
                    complete = false;
                }
            }
            hasAnyCompleteModel = hasAnyCompleteModel || complete;
        }
        if (hasAnyCompleteModel)
            missing.clear();
    } else {
        for (const AiModelDescriptor& model : installed) {
            for (const QString& rel : model.files.allFiles()) {
                if (!QFile::exists(QDir(model.rootDir).filePath(rel)))
                    missing << QStringLiteral("%1/%2").arg(model.id, rel);
            }
        }
    }
    if (!missing.isEmpty()) {
        auto status = makeStatus(UpscaleBackendStatusCode::MissingModelFiles,
                                 QCoreApplication::translate("RealEsrganProcessBackend",
                                     "Real-ESRGAN did not find the required model files."),
                                 missing.join(QStringLiteral(", ")));
        status.modelDirectory = modelDir;
        status.missingFiles = missing;
        return status;
    }

    QProcess help;
    help.setProgram(exe);
    help.setArguments({ QStringLiteral("-h") });
    help.setProcessChannelMode(QProcess::MergedChannels);
    help.start();
    if (!help.waitForStarted(3000)) {
        return makeStatus(UpscaleBackendStatusCode::ExecutableNotRunnable,
                          QCoreApplication::translate("RealEsrganProcessBackend",
                              "Real-ESRGAN could not start. Check that the backend can run on this computer."),
                          help.errorString());
    }
    if (!help.waitForFinished(5000)) {
        help.kill();
        help.waitForFinished(1000);
        return makeStatus(UpscaleBackendStatusCode::ProbeFailed,
                          QCoreApplication::translate("RealEsrganProcessBackend",
                              "Real-ESRGAN did not respond to the backend probe."),
                          help.errorString());
    }
    auto status = makeStatus(UpscaleBackendStatusCode::Available,
                             QCoreApplication::translate("RealEsrganProcessBackend",
                                 "Real-ESRGAN is available."));
    status.modelDirectory = modelDir;
    return status;
}

QString RealEsrganProcessBackend::classifyProcessOutput(const QString& output)
{
    const QString text = output.toLower();
    if (text.contains(QStringLiteral("vkcreate"))
        || text.contains(QStringLiteral("failed to create gpu instance"))
        || text.contains(QStringLiteral("no vulkan capable gpu")))
        return QStringLiteral("vulkan");
    if (text.contains(QStringLiteral("out of memory")) || text.contains(QStringLiteral("bad alloc")))
        return QStringLiteral("oom");
    return QString();
}

UpscaleJobResult RealEsrganProcessBackend::upscale(const UpscaleInput& input,
                                                   const UpscaleOptions& options,
                                                   UpscaleProgressCallback progress,
                                                   std::atomic_bool& cancelRequested)
{
    UpscaleJobResult result;
    result.options = options;
    result.colorProfile = input.colorProfile;
    result.profileSource = input.profileSource;

    if (input.image.isNull()) {
        result.error.code = UpscaleErrorCode::Unknown;
        result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
            "There is no image to upscale.");
        return result;
    }

    const QString jobId = QStringLiteral("upscale-%1-%2")
        .arg(timestamp())
        .arg(QString::number(reinterpret_cast<quintptr>(&cancelRequested), 16));
    const QString jobDir = QDir(jobsRoot()).filePath(jobId);
    QDir().mkpath(jobDir);

    const QString inputPath = QDir(jobDir).filePath(QStringLiteral("input.png"));
    const QString outputPath = QDir(jobDir).filePath(QStringLiteral("output.png"));
    const QString processLogPath = QDir(jobDir).filePath(QStringLiteral("process.log"));
    const QString diagnosticLogPath = QDir(logRoot()).filePath(
        QStringLiteral("realesrgan-job-%1.log").arg(timestamp()));

    result.logPath = diagnosticLogPath;

    QFile processLog(processLogPath);
    processLog.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    QFile diagnosticLog(diagnosticLogPath);
    diagnosticLog.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);

    auto log = [&](const QString& line) {
        appendLine(processLog, line);
        appendLine(diagnosticLog, line);
    };

    const QString exe = resolveExecutablePath();
    const QString modelDir = input.modelDir.isEmpty() ? defaultModelDirectory(options.modelId) : input.modelDir;
    const int effectiveTileSize = options.tileSize > 0 ? options.tileSize : 0;

    log(QStringLiteral("Executable: %1").arg(exe));
    log(QStringLiteral("Model: %1").arg(options.modelId));
    log(QStringLiteral("Model directory: %1").arg(modelDir));
    log(QStringLiteral("Scale: %1").arg(options.scale));
    log(QStringLiteral("Tile size: %1").arg(effectiveTileSize));
    log(QStringLiteral("Device selection: auto"));
    log(QStringLiteral("OS: %1").arg(QSysInfo::prettyProductName()));

    if (cancelRequested.load()) {
        result.cancelled = true;
        result.error.code = UpscaleErrorCode::Cancelled;
        result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend", "Upscale was cancelled.");
        return result;
    }

    if (progress)
        progress(5, QCoreApplication::translate("RealEsrganProcessBackend", "Preparing image..."));
    // Write the input image inside an explicit scope so the file handle is
    // flushed and closed before the child process is launched. Otherwise the
    // QImageWriter (and its underlying QFile) stays alive until the end of the
    // function, leaving input.png open/partially buffered. On Windows the
    // realesrgan process then opens the file and reads incomplete data,
    // producing a "decode image ... failed" error even though the path is
    // correct and the file later looks valid on disk.
    {
        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error.code = UpscaleErrorCode::Unknown;
            result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
                "Could not write the temporary input image.");
            result.error.technicalDetails = inputFile.errorString();
            result.error.logPath = diagnosticLogPath;
            return result;
        }
        QImageWriter writer(&inputFile, "png");
        if (!writer.write(input.image.convertToFormat(QImage::Format_RGBA8888))) {
            result.error.code = UpscaleErrorCode::Unknown;
            result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
                "Could not write the temporary input image.");
            result.error.technicalDetails = writer.errorString();
            result.error.logPath = diagnosticLogPath;
            return result;
        }
        inputFile.flush();
        inputFile.close();
    }

    QStringList args;
    args << QStringLiteral("-i") << inputPath
         << QStringLiteral("-o") << outputPath
         << QStringLiteral("-n") << options.modelId
         << QStringLiteral("-s") << QString::number(options.scale)
         << QStringLiteral("-m") << modelDir
         << QStringLiteral("-f") << QStringLiteral("png")
         << QStringLiteral("-t") << QString::number(effectiveTileSize);

    log(QStringLiteral("Arguments: %1").arg(args.join(QLatin1Char(' '))));

    if (progress)
        progress(-1, QCoreApplication::translate("RealEsrganProcessBackend", "Running Real-ESRGAN..."));

    QProcess process;
    process.setProgram(exe);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        result.error.code = UpscaleErrorCode::ProcessStartFailed;
        result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
            "Real-ESRGAN could not start.");
        result.error.technicalDetails = process.errorString();
        result.error.logPath = diagnosticLogPath;
        log(QStringLiteral("Start failed: %1").arg(process.errorString()));
        return result;
    }

    QString output;
    while (!process.waitForFinished(100)) {
        output += QString::fromLocal8Bit(process.readAll());
        if (cancelRequested.load()) {
            process.kill();
            process.waitForFinished(3000);
            result.cancelled = true;
            result.error.code = UpscaleErrorCode::Cancelled;
            result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend", "Upscale was cancelled.");
            result.error.logPath = diagnosticLogPath;
            log(QStringLiteral("Cancelled."));
            return result;
        }
    }
    output += QString::fromLocal8Bit(process.readAll());
    log(QStringLiteral("Process output:"));
    log(output);
    log(QStringLiteral("Exit code: %1").arg(process.exitCode()));

    if (process.exitStatus() == QProcess::CrashExit) {
        result.error.code = UpscaleErrorCode::ProcessCrashed;
        result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
            "Real-ESRGAN crashed while processing the image.");
        result.error.technicalDetails = output;
        result.error.logPath = diagnosticLogPath;
        return result;
    }
    if (process.exitCode() != 0) {
        const QString classified = classifyProcessOutput(output);
        if (classified == QLatin1String("vulkan")) {
            result.error.code = UpscaleErrorCode::VulkanError;
            result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
                "Real-ESRGAN could not run with the packaged CPU backend.");
        } else if (classified == QLatin1String("oom")) {
            result.error.code = UpscaleErrorCode::OutOfMemory;
            result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
                "Real-ESRGAN ran out of memory. Try a smaller tile size.");
        } else {
            result.error.code = UpscaleErrorCode::Unknown;
            result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
                "Real-ESRGAN failed while processing the image.");
        }
        result.error.technicalDetails = output;
        result.error.logPath = diagnosticLogPath;
        return result;
    }

    if (!QFile::exists(outputPath)) {
        result.error.code = UpscaleErrorCode::OutputMissing;
        result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
            "Real-ESRGAN finished but did not create an output image.");
        result.error.technicalDetails = output;
        result.error.logPath = diagnosticLogPath;
        return result;
    }

    if (progress)
        progress(85, QCoreApplication::translate("RealEsrganProcessBackend", "Loading result..."));
    QImageReader reader(outputPath);
    QImage image = reader.read();
    if (image.isNull()) {
        result.error.code = UpscaleErrorCode::OutputInvalid;
        result.error.userMessage = QCoreApplication::translate("RealEsrganProcessBackend",
            "Real-ESRGAN created an output that the editor could not load.");
        result.error.technicalDetails = reader.errorString();
        result.error.logPath = diagnosticLogPath;
        return result;
    }

    if (options.preserveAlpha)
        image = preserveAlphaFromSource(input.image, image);
    else
        image = image.convertToFormat(QImage::Format_RGBA8888);

    result.ok = true;
    result.image = image;
    result.outputPath = outputPath;
    result.logPath = diagnosticLogPath;
    if (progress)
        progress(95, QCoreApplication::translate("RealEsrganProcessBackend", "Finalizing..."));
    return result;
}
