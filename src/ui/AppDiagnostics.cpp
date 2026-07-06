#include "AppDiagnostics.hpp"

#include "core/AppPaths.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiExecutionProvider.hpp"
#include "ai/onnx/OnnxRuntimeBackend.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/upscale/RealEsrganProcessBackend.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>

#include <opencv2/core/version.hpp>

AppDiagnostics AppDiagnostics::collect()
{
    AppDiagnostics d;

    // ── Application ──────────────────────────────────────────────
    d.appName    = QCoreApplication::applicationName();
    d.appVersion = QCoreApplication::applicationVersion();
    d.buildDate  = QStringLiteral(__DATE__ " " __TIME__);

    // ── System ───────────────────────────────────────────────────
    d.osName          = QSysInfo::prettyProductName();
    d.cpuArchitecture = QSysInfo::currentCpuArchitecture();

    // ── Libraries ────────────────────────────────────────────────
    d.qtVersion     = QString::fromUtf8(qVersion());
    d.opencvVersion = QStringLiteral(CV_VERSION);

    // ── ONNX Runtime ─────────────────────────────────────────────
    d.onnxCompiledIn = OnnxRuntimeBackend::isCompiledIn();
    if (d.onnxCompiledIn) {
        auto* rt = AiRuntimeManager::instance();
        const AiDeviceInfo dev = rt->deviceInfo();
        d.onnxVersion = dev.onnxRuntimeVersion.isEmpty()
                            ? QStringLiteral("(not initialized)")
                            : dev.onnxRuntimeVersion;
        for (const QString& id : dev.availableProviders)
            d.onnxAvailableProviders << AiProvider::displayName(id);
        d.onnxSelectedProvider = AiProvider::displayName(rt->selectedExecutionProvider());
        d.gpuName = dev.gpuName;
    } else {
        d.onnxVersion = QStringLiteral("Not compiled in");
    }

    // ── Models ───────────────────────────────────────────────────
    auto* registry = AiModelRegistry::instance();
    d.modelsTotal  = registry->allModels().count();
    d.modelsFound  = registry->validModels().count();
    d.modelsPath   = registry->modelsDirectory();

    // ── Real-ESRGAN ──────────────────────────────────────────────
    d.esrganBinaryPath  = RealEsrganProcessBackend::resolveExecutablePath();
    d.esrganBinaryFound = QFileInfo::exists(d.esrganBinaryPath);

    const QString esrganModelsDir = RealEsrganProcessBackend::packagedModelsDirectory();
    if (QFileInfo(esrganModelsDir).isDir()) {
        const QStringList paramFiles = QDir(esrganModelsDir).entryList(
            QStringList() << QStringLiteral("*.param"), QDir::Files);
        d.esrganModelsFound = !paramFiles.isEmpty();
    }

    // ── Paths ─────────────────────────────────────────────────────
    d.cachePath  = AppPaths::cacheDir();
    d.configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    return d;
}

QString AppDiagnostics::toText() const
{
    QStringList lines;

    lines << QStringLiteral("App: %1").arg(appName)
          << QStringLiteral("Version: %1").arg(appVersion)
          << QStringLiteral("Build date: %1").arg(buildDate)
          << QString()
          << QStringLiteral("OS: %1").arg(osName)
          << QStringLiteral("Architecture: %1").arg(cpuArchitecture)
          << QString()
          << QStringLiteral("Qt: %1").arg(qtVersion)
          << QStringLiteral("OpenCV: %1").arg(opencvVersion)
          << QStringLiteral("ONNX Runtime: %1").arg(onnxVersion);

    if (onnxCompiledIn) {
        lines << QStringLiteral("ONNX Providers: %1").arg(
                     onnxAvailableProviders.isEmpty()
                         ? QStringLiteral("None")
                         : onnxAvailableProviders.join(QStringLiteral(", ")))
              << QStringLiteral("Selected Provider: %1").arg(onnxSelectedProvider);
    }

    lines << QString()
          << QStringLiteral("ONNX Models: %1 of %2 found").arg(modelsFound).arg(modelsTotal)
          << QString()
          << QStringLiteral("Real-ESRGAN Binary: %1 (%2)")
                 .arg(esrganBinaryPath,
                      esrganBinaryFound ? QStringLiteral("found") : QStringLiteral("not found"))
          << QStringLiteral("Real-ESRGAN Models: %1")
                 .arg(esrganModelsFound ? QStringLiteral("found") : QStringLiteral("not found"))
          << QString()
          << QStringLiteral("Models Path: %1").arg(
                 modelsPath.isEmpty() ? QStringLiteral("Not configured") : modelsPath)
          << QStringLiteral("Cache Path: %1").arg(cachePath)
          << QStringLiteral("Config Path: %1").arg(configPath);

    if (!gpuName.isEmpty())
        lines << QString()
              << QStringLiteral("GPU: %1").arg(gpuName);

    return lines.join(QStringLiteral("\n"));
}
