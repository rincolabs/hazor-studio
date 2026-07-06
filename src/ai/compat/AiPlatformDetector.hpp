#pragma once

#include "ai/compat/AiCompatibilityReport.h"

// Detects raw platform facts (OS name/version, distro id, CPU architecture).
// Classification into supported/experimental/untested lives in
// AiOsCompatibilityProfile so detection stays free of policy.
namespace AiPlatformDetector {

// Gathers the current platform facts. On Linux reads /etc/os-release; on
// Windows/macOS uses QSysInfo. Cheap; safe to call from the UI thread.
AiPlatformInfo detect();

} // namespace AiPlatformDetector
