#include "ConvertToProfileDialog.hpp"

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

ConvertToProfileDialog::ConvertToProfileDialog(const ColorProfile& sourceProfile, QWidget* parent)
    : QDialog(parent)
{
    auto* th = ThemeManager::instance()->current();
    setWindowTitle(tr("Convert to Profile"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(th->spaceSM);

    auto* sourceLabel = new QLabel(
        tr("Source Space: %1")
            .arg(sourceProfile.isValid() ? sourceProfile.displayName() : tr("Untagged")),
        this);
    sourceLabel->setWordWrap(true);
    layout->addWidget(sourceLabel);

    auto* form = new QFormLayout();
    form->setSpacing(th->spaceSM);

    m_destinationCombo = new QComboBox(this);
    ColorProfileRepository repository;
    for (const ColorProfile& profile : repository.builtInRgbProfiles())
        m_destinationCombo->addItem(profile.displayName(), static_cast<int>(profile.kind()));
    const int sRgbIndex = m_destinationCombo->findData(static_cast<int>(ColorProfileKind::SRgb));
    m_destinationCombo->setCurrentIndex(sRgbIndex >= 0 ? sRgbIndex : 0);
    form->addRow(tr("Destination Space:"), m_destinationCombo);

    m_intentCombo = new QComboBox(this);
    m_intentCombo->addItem(tr("Relative Colorimetric"),
                           static_cast<int>(RenderingIntent::RelativeColorimetric));
    m_intentCombo->addItem(tr("Perceptual"),
                           static_cast<int>(RenderingIntent::Perceptual));
    m_intentCombo->addItem(tr("Saturation"),
                           static_cast<int>(RenderingIntent::Saturation));
    m_intentCombo->addItem(tr("Absolute Colorimetric"),
                           static_cast<int>(RenderingIntent::AbsoluteColorimetric));
    const auto& settings = ColorManagementService::instance().settings();
    const int intentIndex = m_intentCombo->findData(static_cast<int>(settings.defaultRenderingIntent));
    m_intentCombo->setCurrentIndex(intentIndex >= 0 ? intentIndex : 0);
    form->addRow(tr("Intent:"), m_intentCombo);

    m_blackPointCb = new AppCheckBox(this);
    m_blackPointCb->setChecked(settings.blackPointCompensation == BlackPointCompensation::Enabled);
    form->addRow(tr("Black Point Compensation:"), m_blackPointCb);

    m_ditherCb = new AppCheckBox(this);
    m_ditherCb->setChecked(true);
    form->addRow(tr("Dither:"), m_ditherCb);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ColorProfile ConvertToProfileDialog::destinationProfile() const
{
    const ColorProfileKind kind = static_cast<ColorProfileKind>(
        m_destinationCombo ? m_destinationCombo->currentData().toInt() : static_cast<int>(ColorProfileKind::SRgb));
    ColorProfile profile = ColorProfile::fromKind(kind);
    if (profile.isValid())
        profile.setSource(ColorProfileSource::ConvertedByUser);
    return profile;
}

ColorConversionOptions ConvertToProfileDialog::conversionOptions() const
{
    ColorConversionOptions options;
    options.intent = static_cast<RenderingIntent>(
        m_intentCombo ? m_intentCombo->currentData().toInt()
                      : static_cast<int>(RenderingIntent::RelativeColorimetric));
    options.blackPointCompensation = m_blackPointCb && m_blackPointCb->isChecked()
        ? BlackPointCompensation::Enabled
        : BlackPointCompensation::Disabled;
    options.dither = !m_ditherCb || m_ditherCb->isChecked();
    return options;
}
