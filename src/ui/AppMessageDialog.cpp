#include "AppMessageDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace AppMessageDialog {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("AppMessageDialog", s); }

QMessageBox::Icon toQtIcon(Icon icon)
{
    switch (icon) {
    case Icon::Information: return QMessageBox::Information;
    case Icon::Warning:     return QMessageBox::Warning;
    case Icon::Error:       return QMessageBox::Critical;
    case Icon::Question:    return QMessageBox::Question;
    }
    return QMessageBox::NoIcon;
}

// Gives buttons the same themed look as the app's purpose-built dialogs
// (ClassicTheme/DarkerTheme style QPushButton#okBtn / #cancelBtn by role),
// plus a visible border. The base QPushButton style is flat everywhere else
// (borderWidth = 0 in ClassicTheme), but in a message dialog a borderless
// button reads as plain text, so these get the theme's standard border as an
// inline, instance-only override — it doesn't touch QPushButton globally.
void applyRoleStyle(QPushButton* button, QMessageBox::ButtonRole role)
{
    if (role == QMessageBox::AcceptRole)
        button->setObjectName(QStringLiteral("okBtn"));
    else if (role == QMessageBox::RejectRole)
        button->setObjectName(QStringLiteral("cancelBtn"));

    const Theme* t = ThemeManager::instance()->current();
    button->setStyleSheet(QStringLiteral("border: %1px solid %2; border-radius: %3px;")
        .arg(t->borderNormalWidth)
        .arg(t->colorBorder.name())
        .arg(t->radiusMD));
}

// QMessageBox lays its own text and (optional) informative-text labels out
// as two independent rows of its internal grid. Giving the first row alone a
// minimum height — the simplest fix — leaves a gap above the second label
// whenever both are set, since the second row starts below that forced
// height instead of right after the first label's actual content. Moving
// both labels into one wrapper widget means the minimum height applies to
// their combined block, so short text still gets a reasonable minimum size
// without pushing a second label down.
void wrapMessageText(QMessageBox& box)
{
    auto* grid = qobject_cast<QGridLayout*>(box.layout());
    auto* mainLabel = box.findChild<QLabel*>(QStringLiteral("qt_msgbox_label"));
    if (!grid || !mainLabel)
        return;

    int row = 0, col = 0, rowSpan = 0, colSpan = 0;
    const int mainIdx = grid->indexOf(mainLabel);
    if (mainIdx < 0)
        return;
    grid->getItemPosition(mainIdx, &row, &col, &rowSpan, &colSpan);

    auto* wrapper = new QWidget(&box);
    wrapper->setObjectName(QStringLiteral("appMessageDialogTextWrapper"));
    auto* wrapperLayout = new QVBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(5, 5, 5, 5);

    grid->removeWidget(mainLabel);
    wrapperLayout->addWidget(mainLabel);

    auto* infoLabel = box.findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
    if (infoLabel) {
        int infoRow = 0, infoCol = 0, infoRowSpan = 0, infoColSpan = 0;
        const int infoIdx = grid->indexOf(infoLabel);
        if (infoIdx >= 0) {
            grid->getItemPosition(infoIdx, &infoRow, &infoCol, &infoRowSpan, &infoColSpan);
            rowSpan = (infoRow + infoRowSpan) - row;
            grid->removeWidget(infoLabel);
        }
        wrapperLayout->addWidget(infoLabel);
    }

    grid->addWidget(wrapper, row, col, rowSpan, colSpan);
}

// QDialogButtonBox positions buttons by ButtonRole according to the active
// style's platform layout policy, not by the order they were added — so a
// caller-specified button order has no visual effect on its own. Re-inserting
// the buttons into the box's own layout, in caller order, overrides that.
void reorderButtons(QMessageBox& box, const QList<QAbstractButton*>& orderedButtons)
{
    auto* buttonBox = box.findChild<QDialogButtonBox*>();
    auto* buttonLayout = buttonBox ? qobject_cast<QBoxLayout*>(buttonBox->layout()) : nullptr;
    if (!buttonLayout)
        return;
    for (QAbstractButton* btn : orderedButtons)
        buttonLayout->removeWidget(btn);
    for (QAbstractButton* btn : orderedButtons)
        buttonLayout->addWidget(btn);
}

// Scoped to this QMessageBox instance only, not the global QLabel style.
void applyMinimumTextHeight(QMessageBox& box)
{
    wrapMessageText(box);
    box.setStyleSheet(QStringLiteral(
        "QWidget#appMessageDialogTextWrapper { min-height: 50px; }"));
}

void present(QWidget* parent, QMessageBox::Icon icon, const QString& title,
             const QString& text, const QString& detailedText)
{
    QMessageBox box(parent);
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    if (!detailedText.isEmpty())
        box.setDetailedText(detailedText);
    applyMinimumTextHeight(box);

    QPushButton* okBtn = box.addButton(tr("OK"), QMessageBox::AcceptRole);
    applyRoleStyle(okBtn, QMessageBox::AcceptRole);
    box.setDefaultButton(okBtn);
    box.exec();
}

} // namespace

void information(QWidget* parent, const QString& title, const QString& text,
                  const QString& detailedText)
{
    present(parent, QMessageBox::Information, title, text, detailedText);
}

void warning(QWidget* parent, const QString& title, const QString& text,
             const QString& detailedText)
{
    present(parent, QMessageBox::Warning, title, text, detailedText);
}

void error(QWidget* parent, const QString& title, const QString& text,
           const QString& detailedText)
{
    present(parent, QMessageBox::Critical, title, text, detailedText);
}

bool confirm(QWidget* parent, const QString& title, const QString& text)
{
    const QList<ButtonSpec> buttons = {
        { tr("Yes"), QMessageBox::AcceptRole },
        { tr("No"),  QMessageBox::RejectRole },
    };
    return choice(parent, title, text, Icon::Question, buttons, /*defaultIndex=*/1) == 0;
}

int choice(QWidget* parent, const QString& title, const QString& text, Icon icon,
           const QList<ButtonSpec>& buttons, int defaultIndex,
           const QString& informativeText, const QString& detailedText)
{
    QMessageBox box(parent);
    box.setIcon(toQtIcon(icon));
    box.setWindowTitle(title);
    box.setText(text);
    if (!informativeText.isEmpty())
        box.setInformativeText(informativeText);
    if (!detailedText.isEmpty())
        box.setDetailedText(detailedText);
    applyMinimumTextHeight(box);

    QList<QAbstractButton*> buttonPtrs;
    buttonPtrs.reserve(buttons.size());
    for (const ButtonSpec& spec : buttons) {
        QPushButton* btn = box.addButton(spec.label, spec.role);
        applyRoleStyle(btn, spec.role);
        buttonPtrs.append(btn);
    }
    if (defaultIndex >= 0 && defaultIndex < buttonPtrs.size())
        box.setDefaultButton(qobject_cast<QPushButton*>(buttonPtrs[defaultIndex]));
    reorderButtons(box, buttonPtrs);

    box.exec();

    return buttonPtrs.indexOf(box.clickedButton());
}

} // namespace AppMessageDialog
