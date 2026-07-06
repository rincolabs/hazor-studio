#include "AssignProfileDialog.hpp"

#include "color/ColorProfileRepository.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

AssignProfileDialog::AssignProfileDialog(const ColorProfile& currentProfile, QWidget* parent)
    : QDialog(parent)
    , m_currentProfile(currentProfile)
{
    auto* th = ThemeManager::instance()->current();
    setWindowTitle(tr("Assign Profile"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(th->spaceSM);

    auto* currentLabel = new QLabel(
        tr("Current profile: %1")
            .arg(currentProfile.isValid() ? currentProfile.displayName() : tr("Untagged")),
        this);
    currentLabel->setWordWrap(true);
    layout->addWidget(currentLabel);

    auto* form = new QFormLayout();
    form->setSpacing(th->spaceSM);
    m_profileCombo = new QComboBox(this);
    m_profileCombo->addItem(tr("Don't color manage this document"),
                            static_cast<int>(ColorProfileKind::Unknown));

    ColorProfileRepository repository;
    for (const ColorProfile& profile : repository.builtInRgbProfiles()) {
        m_profileCombo->addItem(profile.displayName(), static_cast<int>(profile.kind()));
    }

    if (currentProfile.isCustom()) {
        m_profileCombo->addItem(currentProfile.displayName(),
                                static_cast<int>(ColorProfileKind::CustomIcc));
    }

    const int currentIndex = m_profileCombo->findData(static_cast<int>(currentProfile.kind()));
    m_profileCombo->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);
    form->addRow(tr("Profile:"), m_profileCombo);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ColorProfile AssignProfileDialog::selectedProfile() const
{
    const ColorProfileKind kind = static_cast<ColorProfileKind>(
        m_profileCombo ? m_profileCombo->currentData().toInt() : static_cast<int>(ColorProfileKind::Unknown));
    ColorProfile profile = ColorProfile::fromKind(kind);
    if (!profile.isValid() && kind == ColorProfileKind::CustomIcc)
        profile = m_currentProfile;
    if (profile.isValid())
        profile.setSource(ColorProfileSource::AssignedByUser);
    return profile;
}
