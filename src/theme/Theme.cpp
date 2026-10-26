#include "Theme.hpp"
#include "ThemeBuilder.hpp"

Theme::Theme(QObject* parent)
    : QObject(parent)
{
}

void Theme::populateComponentTheme()
{
}

QString Theme::globalStyleSheet() const
{
    QString ss = ThemeBuilder::buildStyleSheet(m_componentTheme);
    ss += QStringLiteral("QScrollArea { background: %1; border: none; }\n")
              .arg(colorBackgroundSecondary.name());
    return ss;
}

QString Theme::titleBarStyleSheet() const
{
    return QStringLiteral(
        "TitleBar { background: %1; }\n"
        "QMenuBar { padding: 0; margin: 0; }\n"
        "QLabel#titleBarTitle { color: %2; background: transparent; padding-left: 2px; }\n"
        "QFrame#titleBarWindowSeparator { background: %6; border: none; }\n"
        "QPushButton#sponsorBtn,\n"
        "QPushButton#minBtn, QPushButton#maxBtn, QPushButton#closeBtn {\n"
        "    background: transparent; color: %2;\n"
        "    border: none; border-radius: 0;\n"
        "    font-size: 13px; padding: 0;\n"
        "}\n"
        "QPushButton#sponsorBtn:hover,\n"
        "QPushButton#minBtn:hover, QPushButton#maxBtn:hover {\n"
        "    background: %3; color: %4;\n"
        "}\n"
        "QPushButton#closeBtn:hover {\n"
        "    background: %5; color: white;\n"
        "}\n"
    )
    .arg(colorSurface.name())
    .arg(colorTextPrimary.name())
    .arg(colorSurfaceHover.name())
    .arg(colorTextBright.name())
    .arg(colorDanger.name())
    .arg(colorBorder.name());
}

QString Theme::layerPanelStyleSheet() const
{
    return QStringLiteral(
        "QTreeWidget { background: %1; border: none; border-radius: 0; padding: 2px; outline: none; }\n"
        "QTreeWidget::item { padding: 2px; border: 1px solid; }\n"
        "QTreeWidget::branch {padding: 2px; background: %1; }\n"
    )
    .arg(colorPanelBackground.name());
}

QString Theme::layerItemDelegateEditorStyleSheet() const
{
    return QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; padding: %5px %6px; font-size: %4px; }\n"
    )
    .arg(colorSurfaceRow.name())
    .arg(colorTextBright.name())
    .arg(colorBorder.name())
    .arg(fontSizeMD)
    .arg(spaceXS)
    .arg(spaceSM);
}

QString Theme::historyPanelStyleSheet() const
{
    return QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: %3px; padding: %8px %9px; font-size: %4px; }\n"
        "QPushButton:hover { color: %5; background: %6; }\n"
        "QPushButton:disabled { color: %7; }\n"
    )
    .arg(colorTextSecondary.name())
    .arg(colorBorder.name())
    .arg(radiusSM)
    .arg(fontSizeSM)
    .arg(colorTextPrimary.name())
    .arg(colorSurfaceHover.name())
    .arg(colorTextDisabled.name())
    .arg(spaceXS)
    .arg(spaceMD);
}

QString Theme::agentPanelStyleSheet() const
{
    return QStringLiteral(
        "QComboBox { font-size: %1px; }\n"
        "QPushButton#agentConfigBtn { background: transparent; border: none; color: %2; font-size: 16px; }\n"
        "QPushButton#agentConfigBtn:hover { color: %3; }\n"
        "QTextEdit { font: %1px monospace; }\n"
        "QLineEdit { padding: %4px %5px; }\n"
        "QPushButton#agentSendBtn { background: %6; color: %7; border: none; border-radius: %8px; padding: %4px %9px; font-weight: bold; }\n"
        "QPushButton#agentSendBtn:disabled { background: %10; color: %11; }\n"
    )
    .arg(fontSizeSM)
    .arg(colorTextPrimary.name())
    .arg(colorTextBright.name())
    .arg(spaceSM)
    .arg(spaceMD)
    .arg(colorAccentPressed.name())
    .arg(colorTextInverted.name())
    .arg(radiusMD)
    .arg(spaceXL)
    .arg(colorSurface.name())
    .arg(colorTextDisabled.name());
}

QString Theme::agentConfigDialogStyleSheet() const
{
    return QStringLiteral(
        "QGroupBox { background: %1; }\n"
        "QCheckBox { color: %2; spacing: %3px; }\n"
        "QCheckBox::indicator { width: %10px; height: %10px; }\n"
        "QPushButton { background: %4; color: %2; border: 1px solid %5; border-radius: %6px; padding: %3px %7px; }\n"
        "QPushButton:hover { background: %8; }\n"
        "QPlainTextEdit { background: %1; color: %2; border: 1px solid %5; border-radius: %6px; font: %9px monospace; }\n"
    )
    .arg(colorBackgroundPrimary.name())
    .arg(colorTextPrimary.name())
    .arg(spaceSM)
    .arg(colorSurface.name())
    .arg(colorBorder.name())
    .arg(radiusMD)
    .arg(spaceMD)
    .arg(colorSurfaceHover.name())
    .arg(fontSizeSM)
    .arg(spaceXL);
}

QString Theme::brushDynamicsStyleSheet() const
{
    return QStringLiteral(
        "QWidget { background: %1; color: %2; }\n"
        "QGroupBox { margin-top: %3px; padding-top: %4px; }\n"
        "QSlider::groove:horizontal { background: %1; }\n"
    )
    .arg(colorSurface.name())
    .arg(colorTextPrimary.name())
    .arg(spaceMD)
    .arg(spaceLG);
}

QString Theme::exportDialogStyleSheet() const
{
    return QStringLiteral(
        "QGroupBox { margin-top: %1px; padding-top: %2px; }\n"
        "QComboBox { min-height: 20px; padding: %3px %4px; }\n"
        "QSpinBox { min-height: 20px; padding: %3px; }\n"
    )
    .arg(spaceLG)
    .arg(spaceXL)
    .arg(spaceSM)
    .arg(spaceMD);
}

// Shared look for the two dialog families that only report something or ask
// for a decision: AppMessageDialog and ProgressDialog. Deliberately partial —
// the dialog surface, QPushButton#okBtn and QPushButton#cancelBtn already come
// from globalStyleSheet(), and restating them here would make this a second
// place to edit. Geometry (widths, margins, button metrics) is not here
// either; it lives in AppDialogChrome, which both dialogs build against.
QString Theme::appDialogStyleSheet() const
{
    return QStringLiteral(
        "QLabel#appDialogTitle {\n"
        "    color: %1; font-size: %2px; font-weight: 600; background: transparent;\n"
        "}\n"
        "QLabel#appDialogMessage {\n"
        "    color: %5; font-size: %2px; background: transparent;\n"
        "}\n"
        "QLabel#appDialogDescription {\n"
        "    color: %3; font-size: %4px; background: transparent;\n"
        "}\n"
        "QLabel#appDialogStatus {\n"
        "    color: %5; font-size: %4px; background: transparent;\n"
        "}\n"
        "QPlainTextEdit#appDialogDetails {\n"
        "    background: %6; color: %3; border: %7px solid %8; border-radius: %9px;\n"
        "    font-family: '%10'; font-size: %11px;\n"
        "}\n"
    )
    .arg(colorTextBright.name())
    .arg(fontSizeLG)
    .arg(colorTextSecondary.name())
    .arg(fontSizeMD)
    .arg(colorTextPrimary.name())
    .arg(colorBackgroundTertiary.name())
    .arg(borderNormalWidth)
    .arg(colorBorder.name())
    .arg(radiusSM)
    .arg(fontFamilyMono)
    .arg(fontSizeSM)
    + QStringLiteral(
        "QProgressBar#appDialogProgress {\n"
        "    background: %1; border: %2px solid %3; border-radius: %4px;\n"
        "}\n"
        "QProgressBar#appDialogProgress::chunk {\n"
        "    background: %5; border-radius: %4px;\n"
        "}\n"
    )
    .arg(colorSurfaceDark.name())
    .arg(borderNormalWidth)
    .arg(colorBorder.name())
    .arg(radiusSM)
    .arg(colorAccent.name())
    // A destructive action carries the secondary button's look, label colour
    // included: in a dialog whose whole point is the decision, the wording is
    // what says "Discard", and a red label there only adds noise. The rule
    // exists at all because the base QPushButton is borderless, so without it
    // the button would read as plain text next to the bordered ones.
    + QStringLiteral(
        "QPushButton#destructiveBtn {\n"
        "    background: %1; color: %2; border: %3px solid %4; border-radius: %5px;\n"
        "    padding: %6px %7px;\n"
        "}\n"
        "QPushButton#destructiveBtn:hover { background: %8; }\n"
        "QPushButton#destructiveBtn:pressed { background: %9; }\n"
        "QPushButton#destructiveBtn:disabled { color: %10; }\n"
    )
    .arg(colorSurface.name())
    .arg(colorTextPrimary.name())
    .arg(borderNormalWidth)
    .arg(colorBorder.name())
    .arg(radiusMD)
    .arg(spaceSM)
    .arg(spaceXL)
    .arg(colorSurfaceHover.name())
    .arg(colorSurfacePressed.name())
    .arg(colorTextDisabled.name())
    // The details disclosure is not one of the decision actions, so it must not
    // look like one: flat, no border, left of the action group.
    + QStringLiteral(
        "QPushButton#appDialogDetailsToggle {\n"
        "    background: transparent; border: none; border-radius: 0; padding: 0;\n"
        "    color: %1; font-size: %2px; text-align: left;\n"
        "}\n"
        "QPushButton#appDialogDetailsToggle:hover { color: %3; }\n"
    )
    .arg(colorTextSecondary.name())
    .arg(fontSizeMD)
    .arg(colorTextBright.name());
}
