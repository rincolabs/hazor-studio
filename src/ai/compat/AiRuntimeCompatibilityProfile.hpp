#pragma once

#include "ai/compat/AiCompatibilityReport.h"

// Checks the ONNX Runtime itself (compiled in, version, load path). This is the
// spec's AiRuntimeChecker: it never touches ONNX directly, only the public
// AiRuntimeManager surface. A missing runtime yields an actionable
// "open_ai_settings" message.
namespace AiRuntimeCompatibilityProfile {

AiRuntimeCompatibility check();

} // namespace AiRuntimeCompatibilityProfile
