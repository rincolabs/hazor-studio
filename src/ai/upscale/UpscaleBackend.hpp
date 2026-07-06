#pragma once

#include "ai/upscale/UpscaleTypes.hpp"

#include <QString>

#include <atomic>

class UpscaleBackend {
public:
    virtual ~UpscaleBackend() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    virtual UpscaleBackendStatus probe() = 0;
    virtual UpscaleJobResult upscale(const UpscaleInput& input,
                                     const UpscaleOptions& options,
                                     UpscaleProgressCallback progress,
                                     std::atomic_bool& cancelRequested) = 0;
};
