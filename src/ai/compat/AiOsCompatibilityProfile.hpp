#pragma once

#include "ai/compat/AiCompatibilityReport.h"

// Classifies a detected platform (AiPlatformInfo) into the officially-supported /
// experimental / untested buckets from spec §2/§5. Data-driven: extend the tables
// in the .cpp to validate new distros or OS versions. Never blocks a platform —
// unknown ones are reported as "Untested", not rejected.
namespace AiOsCompatibilityProfile {

AiPlatformCompatibility classify(const AiPlatformInfo& info);

} // namespace AiOsCompatibilityProfile
