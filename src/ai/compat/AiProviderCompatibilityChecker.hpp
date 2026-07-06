#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QList>
#include <QStringList>

// Builds the per-provider compatibility list (spec §9) by combining platform
// support, what the loaded runtime actually offers, and the CUDA/cuDNN probe +
// requirement table. The AI Object Selection tool and the Settings combobox both
// consume this — they never check providers themselves.
namespace AiProviderCompatibilityChecker {

QList<AiProviderCompatibility> check(const AiPlatformInfo& platform,
                                     bool runtimeAvailable,
                                     const QStringList& runtimeProviders,
                                     const AiCudaInfo& cuda);

} // namespace AiProviderCompatibilityChecker
