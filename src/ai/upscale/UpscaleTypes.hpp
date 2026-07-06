#pragma once

#include "color/ColorProfile.hpp"

#include <QImage>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <functional>

enum class UpscaleTarget {
    CurrentLayer,
    CurrentDocument
};

enum class UpscaleOutputMode {
    NewLayer,
    ReplaceLayer,
    NewDocument,
    ReplaceDocument
};

enum class UpscaleBackendStatusCode {
    Available,
    MissingExecutable,
    ExecutableNotRunnable,
    MissingModelDirectory,
    MissingModelFiles,
    VulkanUnavailable,
    GpuUnsupported,
    ProbeFailed,
    DisabledByUser
};

enum class UpscaleErrorCode {
    BackendUnavailable,
    ModelMissing,
    ProcessStartFailed,
    ProcessCrashed,
    ProcessTimedOut,
    OutputMissing,
    OutputInvalid,
    VulkanError,
    OutOfMemory,
    Cancelled,
    Unknown
};

struct UpscaleOptions {
    QString backendId = QStringLiteral("realesrgan-process");
    QString modelId = QStringLiteral("realesrgan-x4plus");

    int scale = 4;
    int tileSize = 0;
    int tilePadding = 16;

    bool preserveAlpha = true;
    bool preserveLayerMask = true;
    bool preserveColorProfile = true;

    UpscaleTarget target = UpscaleTarget::CurrentDocument;
    UpscaleOutputMode output = UpscaleOutputMode::NewDocument;
    bool openResultAfterFinish = true;
};

struct UpscaleInput {
    QImage image;
    QString sourceName;
    QString modelDir;
    ColorProfile colorProfile;
    ColorProfileSource profileSource = ColorProfileSource::Missing;
};

struct UpscaleBackendStatus {
    UpscaleBackendStatusCode code = UpscaleBackendStatusCode::ProbeFailed;
    QString userMessage;
    QString technicalDetails;
    QString executablePath;
    QString modelDirectory;
    QStringList missingFiles;

    bool available() const { return code == UpscaleBackendStatusCode::Available; }
};

struct UpscaleError {
    UpscaleErrorCode code = UpscaleErrorCode::Unknown;
    QString userMessage;
    QString technicalDetails;
    QString logPath;
};

struct UpscaleJobResult {
    bool ok = false;
    bool cancelled = false;
    QImage image;
    UpscaleOptions options;
    UpscaleError error;
    QString logPath;
    QString outputPath;
    ColorProfile colorProfile;
    ColorProfileSource profileSource = ColorProfileSource::Missing;
};

using UpscaleProgressCallback = std::function<void(int, const QString&)>;

Q_DECLARE_METATYPE(UpscaleJobResult)
Q_DECLARE_METATYPE(UpscaleError)
