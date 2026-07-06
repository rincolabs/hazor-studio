#include "SoftProofDialog.hpp"

#include "color/ColorManagementService.hpp"
#include "color/ColorProfileRepository.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

SoftProofDialog::SoftProofDialog(const SoftProofSettings& current, QWidget* parent)
    : QDialog(parent)
{
    auto* th = ThemeManager::instance()->current();
    setWindowTitle(tr("Proof Setup"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(th->spaceSM);

    auto* intro = new QLabel(
        tr("Soft proofing previews how the document would look in another color "
           "space. It is a display-only preview and never changes the document's "
           "pixels or profile."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    form->setSpacing(th->spaceSM);

    // Device / profile to simulate. Same built-in RGB list as Convert/Assign;
    // the advanced engine will add printer/CMYK profiles later.
    m_proofCombo = new QComboBox(this);
    ColorProfileRepository repository;
    for (const ColorProfile& profile : repository.builtInRgbProfiles())
        m_proofCombo->addItem(profile.displayName(), static_cast<int>(profile.kind()));
    const ColorProfileKind currentKind = current.proofProfile.isValid()
        ? current.proofProfile.kind()
        : ColorProfileKind::SRgb;
    const int proofIndex = m_proofCombo->findData(static_cast<int>(currentKind));
    m_proofCombo->setCurrentIndex(proofIndex >= 0 ? proofIndex : 0);
    form->addRow(tr("Device to Simulate:"), m_proofCombo);

    m_intentCombo = new QComboBox(this);
    m_intentCombo->addItem(tr("Relative Colorimetric"),
                           static_cast<int>(RenderingIntent::RelativeColorimetric));
    m_intentCombo->addItem(tr("Perceptual"),
                           static_cast<int>(RenderingIntent::Perceptual));
    m_intentCombo->addItem(tr("Saturation"),
                           static_cast<int>(RenderingIntent::Saturation));
    m_intentCombo->addItem(tr("Absolute Colorimetric"),
                           static_cast<int>(RenderingIntent::AbsoluteColorimetric));
    const int intentIndex = m_intentCombo->findData(static_cast<int>(current.intent));
    m_intentCombo->setCurrentIndex(intentIndex >= 0 ? intentIndex : 0);
    form->addRow(tr("Rendering Intent:"), m_intentCombo);

    m_blackPointCb = new AppCheckBox(this);
    m_blackPointCb->setChecked(current.blackPointCompensation == BlackPointCompensation::Enabled);
    form->addRow(tr("Black Point Compensation:"), m_blackPointCb);

    m_simulatePaperCb = new AppCheckBox(this);
    m_simulatePaperCb->setChecked(current.simulatePaperColor);
    form->addRow(tr("Simulate Paper Color:"), m_simulatePaperCb);

    m_simulateBlackCb = new AppCheckBox(this);
    m_simulateBlackCb->setChecked(current.simulateBlackInk);
    form->addRow(tr("Simulate Black Ink:"), m_simulateBlackCb);

    m_preserveNumbersCb = new AppCheckBox(this);
    m_preserveNumbersCb->setChecked(current.preserveRgbNumbers);
    form->addRow(tr("Preserve RGB Numbers:"), m_preserveNumbersCb);

    m_enabledCb = new AppCheckBox(this);
    m_enabledCb->setChecked(current.enabled);
    form->addRow(tr("Proof Colors On:"), m_enabledCb);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

SoftProofSettings SoftProofDialog::settings() const
{
    SoftProofSettings proof;

    const ColorProfileKind kind = static_cast<ColorProfileKind>(
        m_proofCombo ? m_proofCombo->currentData().toInt()
                     : static_cast<int>(ColorProfileKind::SRgb));
    proof.proofProfile = ColorProfile::fromKind(kind);

    proof.enabled = m_enabledCb && m_enabledCb->isChecked();
    proof.intent = static_cast<RenderingIntent>(
        m_intentCombo ? m_intentCombo->currentData().toInt()
                      : static_cast<int>(RenderingIntent::RelativeColorimetric));
    proof.blackPointCompensation = m_blackPointCb && m_blackPointCb->isChecked()
        ? BlackPointCompensation::Enabled
        : BlackPointCompensation::Disabled;
    proof.simulatePaperColor = m_simulatePaperCb && m_simulatePaperCb->isChecked();
    proof.simulateBlackInk = m_simulateBlackCb && m_simulateBlackCb->isChecked();
    proof.preserveRgbNumbers = m_preserveNumbersCb && m_preserveNumbersCb->isChecked();
    proof.presetName = proof.proofProfile.isValid() ? proof.proofProfile.displayName() : QString();
    return proof;
}
