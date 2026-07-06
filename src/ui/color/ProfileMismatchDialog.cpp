#include "ProfileMismatchDialog.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

ProfileMismatchDialog::ProfileMismatchDialog(const ColorProfile& embeddedProfile,
                                             const ColorProfile& workingSpace,
                                             QWidget* parent)
    : QDialog(parent)
{
    auto* th = ThemeManager::instance()->current();
    setWindowTitle(tr("Embedded Profile Mismatch"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(th->spaceSM);

    auto* title = new QLabel(
        tr("Embedded: %1\nWorking RGB: %2")
            .arg(embeddedProfile.displayName(), workingSpace.displayName()), this);
    title->setWordWrap(true);
    layout->addWidget(title);

    m_preserveRadio = new QRadioButton(tr("Use the embedded profile"), this);
    m_convertRadio = new QRadioButton(tr("Convert document colors to the working RGB"), this);
    m_assignRadio = new QRadioButton(tr("Discard the embedded profile and assign working RGB"), this);
    m_preserveRadio->setChecked(true);

    layout->addWidget(m_preserveRadio);
    layout->addWidget(m_convertRadio);
    layout->addWidget(m_assignRadio);

    m_dontAskAgainCb = new AppCheckBox(tr("Don't ask again"), this);
    layout->addWidget(m_dontAskAgainCb);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ProfileMismatchUserChoice ProfileMismatchDialog::choice() const
{
    if (m_convertRadio && m_convertRadio->isChecked())
        return ProfileMismatchUserChoice::ConvertToWorking;
    if (m_assignRadio && m_assignRadio->isChecked())
        return ProfileMismatchUserChoice::AssignWorking;
    return ProfileMismatchUserChoice::PreserveEmbedded;
}

bool ProfileMismatchDialog::dontAskAgain() const
{
    return m_dontAskAgainCb && m_dontAskAgainCb->isChecked();
}
