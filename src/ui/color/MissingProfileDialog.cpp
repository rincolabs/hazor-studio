#include "MissingProfileDialog.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

MissingProfileDialog::MissingProfileDialog(const ColorProfile& workingSpace, QWidget* parent)
    : QDialog(parent)
{
    auto* th = ThemeManager::instance()->current();
    setWindowTitle(tr("Missing Profile"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(th->spaceSM);

    auto* title = new QLabel(tr("This document does not have an embedded RGB profile."), this);
    title->setWordWrap(true);
    layout->addWidget(title);

    m_leaveUntaggedRadio = new QRadioButton(tr("Leave as untagged"), this);
    m_assignWorkingRadio = new QRadioButton(
        tr("Assign working RGB: %1").arg(workingSpace.displayName()), this);
    m_assignSRgbRadio = new QRadioButton(tr("Assign sRGB"), this);
    m_assignSRgbRadio->setChecked(true);

    layout->addWidget(m_leaveUntaggedRadio);
    layout->addWidget(m_assignWorkingRadio);
    layout->addWidget(m_assignSRgbRadio);

    m_dontAskAgainCb = new AppCheckBox(tr("Don't ask again"), this);
    layout->addWidget(m_dontAskAgainCb);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

MissingProfileUserChoice MissingProfileDialog::choice() const
{
    if (m_leaveUntaggedRadio && m_leaveUntaggedRadio->isChecked())
        return MissingProfileUserChoice::LeaveUntagged;
    if (m_assignWorkingRadio && m_assignWorkingRadio->isChecked())
        return MissingProfileUserChoice::AssignWorking;
    return MissingProfileUserChoice::AssignSRgb;
}

bool MissingProfileDialog::dontAskAgain() const
{
    return m_dontAskAgainCb && m_dontAskAgainCb->isChecked();
}
