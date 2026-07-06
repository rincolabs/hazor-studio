#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QString>

class QWidget;

// What the user chose in an AI alert. The caller maps OpenSettings to opening
// Settings ▸ AI / Machine Learning (MainWindow::onOpenAiSettings), and UseCpu to
// switching the Execution Provider to CPU.
enum class AiAlertAction {
    OpenSettings,
    UseCpu,
    Cancel
};

// Standardized, user-facing alert dialogs for AI errors (spec §11/§18). AI
// failures must never be log-only: every hard error funnels through here so the
// user sees an actionable dialog with an "AI Settings" button. Pure UI — it never
// opens Settings itself (that stays a MainWindow concern via the returned action).
namespace AiAlertService {

AiAlertAction showAiError(QWidget* parent, const AiCompatibilityMessage& message);
AiAlertAction showAiRuntimeError(QWidget* parent, const AiCompatibilityMessage& message);
AiAlertAction showAiDisabled(QWidget* parent);
AiAlertAction showMissingModel(QWidget* parent, const QString& requiredCapability);
AiAlertAction showProviderIncompatible(QWidget* parent, const AiCompatibilityMessage& message);
AiAlertAction showCudaOom(QWidget* parent, bool fallbackDisabled);

} // namespace AiAlertService
