#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QString>

// Summarises the installed models for the compatibility report (spec §13), and
// answers the capability questions the tool guard asks ("is there a model that
// can do object selection?"). Pure queries over AiModelRegistry — no file or
// runtime access.
namespace AiModelCompatibilityChecker {

AiModelCompatibility check();

// True when at least one valid model advertises the given capability/task.
// Accepts a capability ("object_selection") or a task ("segmentation").
bool hasRequiredModel(const QString& capabilityOrTask);

} // namespace AiModelCompatibilityChecker
