#include "AiAlertService.hpp"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace AiAlertService {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("AiAlertService", s); }

// Presents a themed warning with an "AI Settings" button (always), an optional
// "Use CPU" button and "Cancel". Returns the chosen action.
AiAlertAction present(QWidget* parent, const QString& title, const QString& message,
                      const QString& details, bool offerUseCpu)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(title);
    box.setInformativeText(message);
    if (!details.isEmpty())
        box.setDetailedText(details);

    QPushButton* settingsBtn = box.addButton(tr("AI Settings"), QMessageBox::AcceptRole);
    QPushButton* useCpuBtn = offerUseCpu ? box.addButton(tr("Use CPU"), QMessageBox::ActionRole) : nullptr;
    QPushButton* cancelBtn = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(settingsBtn);

    box.exec();
    QAbstractButton* clicked = box.clickedButton();
    if (clicked == settingsBtn) return AiAlertAction::OpenSettings;
    if (useCpuBtn && clicked == useCpuBtn) return AiAlertAction::UseCpu;
    Q_UNUSED(cancelBtn);
    return AiAlertAction::Cancel;
}

} // namespace

AiAlertAction showAiError(QWidget* parent, const AiCompatibilityMessage& m)
{
    const QString title = m.title.isEmpty() ? tr("AI error") : m.title;
    return present(parent, title, m.message, m.details, false);
}

AiAlertAction showAiRuntimeError(QWidget* parent, const AiCompatibilityMessage& m)
{
    const QString title = m.title.isEmpty() ? tr("AI runtime unavailable") : m.title;
    return present(parent, title, m.message, m.details, false);
}

AiAlertAction showAiDisabled(QWidget* parent)
{
    return present(parent,
        tr("AI features are disabled"),
        tr("AI Object Selection requires AI features to be enabled in "
           "Settings > AI / Machine Learning."),
        QString(), false);
}

AiAlertAction showMissingModel(QWidget* parent, const QString& requiredCapability)
{
    QString what = tr("an AI model");
    if (requiredCapability == QLatin1String("object_selection")
        || requiredCapability == QLatin1String("segmentation"))
        what = tr("a segmentation model");
    else if (requiredCapability == QLatin1String("matting")
             || requiredCapability == QLatin1String("alpha_matte"))
        what = tr("a matting model");
    else if (requiredCapability == QLatin1String("background_removal"))
        what = tr("a background-removal model");

    return present(parent,
        tr("No AI model installed"),
        tr("This feature requires %1. Install or enable one in "
           "Settings > AI / Machine Learning > Models.").arg(what),
        QString(), false);
}

AiAlertAction showProviderIncompatible(QWidget* parent, const AiCompatibilityMessage& m)
{
    const QString title = m.title.isEmpty() ? tr("Execution provider not compatible") : m.title;
    const QString msg = m.message.isEmpty()
        ? tr("The selected ONNX Runtime provider is not compatible with the libraries "
             "found on this system.")
        : m.message;
    return present(parent, title, msg, m.details, /*offerUseCpu=*/true);
}

AiAlertAction showCudaOom(QWidget* parent, bool fallbackDisabled)
{
    if (fallbackDisabled) {
        return present(parent,
            tr("Not enough GPU memory"),
            tr("GPU memory was not enough to run this AI operation. Enable CPU fallback in "
               "Settings > AI / Machine Learning, or reduce the AI working resolution."),
            QString(), /*offerUseCpu=*/true);
    }
    return present(parent,
        tr("Not enough GPU memory"),
        tr("GPU memory was not enough for this AI operation. The editor retried it on CPU."),
        QString(), false);
}

} // namespace AiAlertService
