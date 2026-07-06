#pragma once

#include "ai/upscale/UpscaleBackend.hpp"

#include <QString>
#include <QStringList>

class RealEsrganProcessBackend final : public UpscaleBackend {
public:
    QString id() const override;
    QString displayName() const override;

    UpscaleBackendStatus probe() override;
    UpscaleJobResult upscale(const UpscaleInput& input,
                             const UpscaleOptions& options,
                             UpscaleProgressCallback progress,
                             std::atomic_bool& cancelRequested) override;

    static QString resolveExecutablePath();
    static QString packagedModelsDirectory();
    static QString defaultModelDirectory(const QString& modelId);
    static QStringList expectedModelFiles(const QString& modelId);

private:
    static QString classifyProcessOutput(const QString& output);
};
