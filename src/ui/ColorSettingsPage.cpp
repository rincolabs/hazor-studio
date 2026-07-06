#include "ColorSettingsPage.hpp"

#include "color/ColorManagementService.hpp"
#include "color/ColorProfileRepository.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace {

constexpr auto kColorGroup = "colorManagement";

void addCheckRow(QFormLayout* form, const QString& label, QCheckBox* checkbox)
{
    checkbox->setText(QString());
    checkbox->setFixedWidth(28);
    checkbox->setToolTip(label);
    form->addRow(label + QStringLiteral(":"), checkbox);
}

} // namespace

ColorSettingsPage::ColorSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* th = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("colorSettingsPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_scroll);

    m_content = new QWidget(m_scroll);
    m_content->setObjectName(QStringLiteral("colorSettingsContent"));
    auto* mainLay = new QVBoxLayout(m_content);
    mainLay->setContentsMargins(24, 16, 24, 16);
    mainLay->setSpacing(th->spaceMD);

    auto* workingGroup = new QGroupBox(tr("Working Spaces"), m_content);
    auto* workingForm = new QFormLayout(workingGroup);
    workingForm->setSpacing(th->spaceSM);
    workingForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_workingRgbCombo = new QComboBox(workingGroup);
    addProfileItems(m_workingRgbCombo);
    workingForm->addRow(tr("RGB:"), m_workingRgbCombo);
    mainLay->addWidget(workingGroup);

    auto* policyGroup = new QGroupBox(tr("Color Management Policies"), m_content);
    auto* policyForm = new QFormLayout(policyGroup);
    policyForm->setSpacing(th->spaceSM);
    policyForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_missingPolicyCombo = new QComboBox(policyGroup);
    m_missingPolicyCombo->addItem(tr("Ask when opening"),
                                  static_cast<int>(MissingProfilePolicy::AskUser));
    m_missingPolicyCombo->addItem(tr("Assume sRGB"),
                                  static_cast<int>(MissingProfilePolicy::AssumeSRgb));
    m_missingPolicyCombo->addItem(tr("Assign Working RGB"),
                                  static_cast<int>(MissingProfilePolicy::AssignWorkingSpace));
    m_missingPolicyCombo->addItem(tr("Leave Untagged"),
                                  static_cast<int>(MissingProfilePolicy::LeaveUntagged));
    policyForm->addRow(tr("Missing Profiles:"), m_missingPolicyCombo);

    m_mismatchPolicyCombo = new QComboBox(policyGroup);
    m_mismatchPolicyCombo->addItem(tr("Ask when opening"),
                                   static_cast<int>(ProfileMismatchPolicy::AskUser));
    m_mismatchPolicyCombo->addItem(tr("Preserve Embedded Profile"),
                                   static_cast<int>(ProfileMismatchPolicy::PreserveEmbeddedProfile));
    m_mismatchPolicyCombo->addItem(tr("Convert to Working RGB"),
                                   static_cast<int>(ProfileMismatchPolicy::ConvertToWorkingSpace));
    m_mismatchPolicyCombo->addItem(tr("Assign Working RGB"),
                                   static_cast<int>(ProfileMismatchPolicy::AssignWorkingSpace));
    policyForm->addRow(tr("Profile Mismatches:"), m_mismatchPolicyCombo);
    mainLay->addWidget(policyGroup);

    auto* conversionGroup = new QGroupBox(tr("Conversion Options"), m_content);
    auto* conversionForm = new QFormLayout(conversionGroup);
    conversionForm->setSpacing(th->spaceSM);
    conversionForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_intentCombo = new QComboBox(conversionGroup);
    m_intentCombo->addItem(tr("Relative Colorimetric"),
                           static_cast<int>(RenderingIntent::RelativeColorimetric));
    m_intentCombo->addItem(tr("Perceptual"),
                           static_cast<int>(RenderingIntent::Perceptual));
    m_intentCombo->addItem(tr("Saturation"),
                           static_cast<int>(RenderingIntent::Saturation));
    m_intentCombo->addItem(tr("Absolute Colorimetric"),
                           static_cast<int>(RenderingIntent::AbsoluteColorimetric));
    conversionForm->addRow(tr("Intent:"), m_intentCombo);

    m_blackPointCb = new AppCheckBox(conversionGroup);
    addCheckRow(conversionForm, tr("Black Point Compensation"), m_blackPointCb);

    m_convertNewDocumentsCb = new AppCheckBox(conversionGroup);
    addCheckRow(conversionForm, tr("Convert New Documents to Working RGB"), m_convertNewDocumentsCb);
    mainLay->addWidget(conversionGroup);

    auto* displayGroup = new QGroupBox(tr("Display"), m_content);
    auto* displayForm = new QFormLayout(displayGroup);
    displayForm->setSpacing(th->spaceSM);
    displayForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_displayManagementCb = new AppCheckBox(displayGroup);
    addCheckRow(displayForm, tr("Enable Display Color Management"), m_displayManagementCb);

    m_systemMonitorProfileCb = new AppCheckBox(displayGroup);
    addCheckRow(displayForm, tr("Use System Monitor Profile"), m_systemMonitorProfileCb);

    m_fallbackDisplayCombo = new QComboBox(displayGroup);
    addProfileItems(m_fallbackDisplayCombo);
    displayForm->addRow(tr("Fallback Profile:"), m_fallbackDisplayCombo);

    m_softProofCb = new AppCheckBox(displayGroup);
    addCheckRow(displayForm, tr("Enable Soft Proof"), m_softProofCb);
    mainLay->addWidget(displayGroup);

    mainLay->addStretch();
    m_scroll->setWidget(m_content);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ColorSettingsPage::applyTheme);
    applyTheme();
    loadSettings();
}

void ColorSettingsPage::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral(
        "ColorSettingsPage { background: %1; color: %2; }"
        "QWidget#colorSettingsContent { background: %1; color: %2; }")
        .arg(th->colorSurface.name(), th->colorTextPrimary.name()));
    if (m_scroll) {
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea { background: %2; border: none; }"
            "QScrollArea > QWidget > QWidget { background: %1; }")
            .arg(th->colorSurface.name(), th->colorBackgroundSecondary.name()));
    }
}

void ColorSettingsPage::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kColorGroup));

    selectComboData(m_workingRgbCombo,
                    settings.value(QStringLiteral("defaultRgbWorkingSpace"),
                                   static_cast<int>(ColorProfileKind::SRgb)).toInt());
    selectComboData(m_missingPolicyCombo,
                    settings.value(QStringLiteral("missingProfilePolicy"),
                                   static_cast<int>(MissingProfilePolicy::AssumeSRgb)).toInt());
    selectComboData(m_mismatchPolicyCombo,
                    settings.value(QStringLiteral("profileMismatchPolicy"),
                                   static_cast<int>(ProfileMismatchPolicy::PreserveEmbeddedProfile)).toInt());
    selectComboData(m_intentCombo,
                    settings.value(QStringLiteral("defaultRenderingIntent"),
                                   static_cast<int>(RenderingIntent::RelativeColorimetric)).toInt());
    m_blackPointCb->setChecked(settings.value(QStringLiteral("blackPointCompensation"), true).toBool());
    m_convertNewDocumentsCb->setChecked(
        settings.value(QStringLiteral("convertNewDocumentsToWorkingSpace"), false).toBool());
    m_displayManagementCb->setChecked(
        settings.value(QStringLiteral("enableDisplayColorManagement"), true).toBool());
    m_systemMonitorProfileCb->setChecked(
        settings.value(QStringLiteral("useSystemMonitorProfile"), true).toBool());
    selectComboData(m_fallbackDisplayCombo,
                    settings.value(QStringLiteral("fallbackDisplayProfile"),
                                   static_cast<int>(ColorProfileKind::SRgb)).toInt());
    m_softProofCb->setChecked(settings.value(QStringLiteral("enableSoftProof"), false).toBool());

    settings.endGroup();
}

void ColorSettingsPage::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kColorGroup));

    settings.setValue(QStringLiteral("defaultRgbWorkingSpace"), m_workingRgbCombo->currentData().toInt());
    settings.setValue(QStringLiteral("missingProfilePolicy"), m_missingPolicyCombo->currentData().toInt());
    settings.setValue(QStringLiteral("profileMismatchPolicy"), m_mismatchPolicyCombo->currentData().toInt());
    settings.setValue(QStringLiteral("defaultRenderingIntent"), m_intentCombo->currentData().toInt());
    settings.setValue(QStringLiteral("blackPointCompensation"), m_blackPointCb->isChecked());
    settings.setValue(QStringLiteral("convertNewDocumentsToWorkingSpace"), m_convertNewDocumentsCb->isChecked());
    settings.setValue(QStringLiteral("enableDisplayColorManagement"), m_displayManagementCb->isChecked());
    settings.setValue(QStringLiteral("useSystemMonitorProfile"), m_systemMonitorProfileCb->isChecked());
    settings.setValue(QStringLiteral("fallbackDisplayProfile"), m_fallbackDisplayCombo->currentData().toInt());
    settings.setValue(QStringLiteral("enableSoftProof"), m_softProofCb->isChecked());

    settings.endGroup();
    ColorManagementService::instance().reloadSettings();
}

void ColorSettingsPage::addProfileItems(QComboBox* combo) const
{
    ColorProfileRepository repository;
    for (const ColorProfile& profile : repository.builtInRgbProfiles()) {
        combo->addItem(profile.displayName(), static_cast<int>(profile.kind()));
    }
}

void ColorSettingsPage::selectComboData(QComboBox* combo, int value) const
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}
