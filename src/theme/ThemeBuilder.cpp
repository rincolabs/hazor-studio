#include "ThemeBuilder.hpp"
#include "ScrollBarStyle.hpp"
#include <QStringList>
#include <QtGlobal>

// ── Helpers ──────────────────────────────────────────────────────────

static QString c(const QColor& color)
{
    return color.isValid() ? color.name() : QStringLiteral("transparent");
}

static QStringList baseProps(const QtComponentStyle& s)
{
    QStringList out;

    if (s.background.isValid())
        out << QStringLiteral("background: %1").arg(c(s.background));
    if (s.text.isValid())
        out << QStringLiteral("color: %1").arg(c(s.text));

    int bw = s.borderWidth >= 0 ? s.borderWidth : 0;
    if (s.border.isValid())
        out << QStringLiteral("border: %1px solid %2").arg(bw).arg(c(s.border));

    if (s.borderRadius >= 0)
        out << QStringLiteral("border-radius: %1px").arg(s.borderRadius);

    if (s.padding >= 0)
        // out << QStringLiteral("padding: %1px").arg(s.padding);
        out << QStringLiteral("padding: %1px").arg(s.padding);
    if (s.paddingLeft >= 0)
        out << QStringLiteral("padding-left: %1px").arg(s.paddingLeft);
    if (s.paddingRight >= 0)
        out << QStringLiteral("padding-right: %1px").arg(s.paddingRight);
    if (s.margin >= 0)
        out << QStringLiteral("margin: %1px").arg(s.margin);

    if (s.minHeight >= 0)
        out << QStringLiteral("min-height: %1px").arg(s.minHeight);
    if (s.minWidth >= 0)
        out << QStringLiteral("min-width: %1px").arg(s.minWidth);
    if (s.fontSize >= 0)
        out << QStringLiteral("font-size: %1px").arg(s.fontSize);
    if (s.fontWeight >= 0)
        out << QStringLiteral("font-weight: %1").arg(s.fontWeight);
    if (!s.fontFamily.isEmpty())
        out << QStringLiteral("font-family: '%1'").arg(s.fontFamily);

    return out;
}

static QString block(const QString& selector, const QtComponentStyle& s)
{
    QStringList props = baseProps(s);
    if (props.isEmpty())
        return {};

    return QStringLiteral("%1 {\n    %2;\n}\n")
        .arg(selector, props.join(QStringLiteral(";\n    ")));
}

static QString subBlock(const QString& selector, const QtComponentStyle& s,
                        const QtComponentStyle& base)
{
    QStringList props;

    if (s.background.isValid() && s.background != base.background)
        props << QStringLiteral("background: %1").arg(c(s.background));
    if (s.text.isValid() && s.text != base.text)
        props << QStringLiteral("color: %1").arg(c(s.text));
    if (s.border.isValid() && s.border != base.border)
        props << QStringLiteral("border-color: %1").arg(c(s.border));

    if (props.isEmpty())
        return {};

    return QStringLiteral("%1 {\n    %2;\n}\n")
        .arg(selector, props.join(QStringLiteral(";\n    ")));
}

// ── Sub-control generators ───────────────────────────────────────────

static QStringList menuBarExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.backgroundHover.isValid() && s.backgroundHover != s.background)
        out << QStringLiteral("QMenuBar::item:selected { background: %1; }").arg(c(s.backgroundHover));
    return out;
}

static QStringList menuExtra(const QtComponentStyle& s)
{
    QStringList out;

    int p = s.padding >= 0 ? s.padding : 6;
    int pSide = s.paddingLeft >= 0 ? s.paddingLeft : (s.paddingRight >= 0 ? s.paddingRight : 16);

    out << QStringLiteral("QMenu::item { padding: %1px %2px; }").arg(p).arg(pSide);

    if (s.backgroundHover.isValid())
        out << QStringLiteral("QMenu::item:selected { background: %1; }").arg(c(s.backgroundHover));

    if (s.textDisabled.isValid())
        out << QString("QMenu::item:disabled { color: %1; background: transparent; padding: %2px %3px; }")
            .arg(c(s.textDisabled)).arg(p).arg(pSide);

    if (s.border.isValid())
        out << QStringLiteral("QMenu::separator { background: %1; height: 1px; margin: %2px %3px; }")
            .arg(c(s.border)).arg(s.margin >= 0 ? s.margin : 6).arg(s.padding >= 0 ? s.padding : 8);

    return out;
}

static QStringList toolBarExtra(const QtComponentStyle& s)
{
    QStringList out;
    int bw = s.borderWidth >= 0 ? s.borderWidth : 1;
    if (s.border.isValid())
        out << QStringLiteral("QToolBar { border-bottom: %1px solid %2; spacing: 4px; padding: 4px; }").arg(bw).arg(c(s.border));
    if (s.text.isValid())
        out << QStringLiteral("QToolBar QLabel { color: %1; }").arg(c(s.text));
    return out;
}

static QStringList statusBarExtra(const QtComponentStyle& s)
{
    QStringList out;
    int bw = s.borderWidth >= 0 ? s.borderWidth : 1;
    if (s.background.isValid() && s.border.isValid())
        out << QStringLiteral("QStatusBar { background: %1; color: %2; border-top: %3px solid %4; }")
            .arg(c(s.background), c(s.text)).arg(bw).arg(c(s.border));
    return out;
}

static QStringList dockWidgetExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.background.isValid() && s.text.isValid())
        out << QStringLiteral("QDockWidget::title { background: %1; padding: %2px; color: %3; }")
            .arg(c(s.background))
            .arg(s.padding >= 0 ? s.padding : 6)
            .arg(c(s.text));
    return out;
}

static QStringList tabWidgetExtra(const QtComponentStyle& s)
{
    QStringList out;
    int bw = s.borderWidth >= 0 ? s.borderWidth : 1;
    if (s.background.isValid() && s.border.isValid())
        out << QStringLiteral("QTabWidget::pane { background: %1; border: %2px solid %3; border-top: none; border-bottom: none; }")
            .arg(c(s.background)).arg(bw).arg(c(s.border));
    return out;
}

static QStringList tabBarExtra(const QtComponentStyle& s)
{
    QStringList out;
    int p = s.padding >= 0 ? s.padding : 8;
    int pLeft = s.paddingLeft >= 0 ? s.paddingLeft : p;
    int pRight = s.paddingRight >= 0 ? s.paddingRight : p;
    int bw = s.borderWidth >= 0 ? s.borderWidth : 1;
    QStringList tabProps;

    // QTabBar bar background (uses backgroundFocused as the bar container color)
    if (s.backgroundFocused.isValid())
        out << QStringLiteral("QTabBar { qproperty-drawBase: 0; background: %1; border: none; outline: none; }").arg(c(s.backgroundFocused));
    else
        out << QStringLiteral("QTabBar { qproperty-drawBase: 0; border: none; outline: none; }");

    if (s.background.isValid())
        tabProps << QStringLiteral("background: %1").arg(c(s.background));
    if (s.textDisabled.isValid())
        tabProps << QStringLiteral("color: %1").arg(c(s.textDisabled));
    else if (s.text.isValid())
        tabProps << QStringLiteral("color: %1").arg(c(s.text));
    tabProps << QStringLiteral("padding: %1px %2px %1px %3px").arg(p).arg(pRight).arg(pLeft);
    if (s.border.isValid())
        tabProps << QStringLiteral("border: %1px solid %2").arg(bw).arg(c(s.border));
    tabProps << QStringLiteral("border-bottom: none");
    tabProps << QStringLiteral("margin: 0");
    tabProps << QStringLiteral("outline: none");
    if (s.fontSize >= 0)
        tabProps << QStringLiteral("font-size: %1px").arg(s.fontSize);
    if (s.fontWeight >= 0)
        tabProps << QStringLiteral("font-weight: %1").arg(s.fontWeight);
    if (!s.fontFamily.isEmpty())
        tabProps << QStringLiteral("font-family: '%1'").arg(s.fontFamily);

    if (!tabProps.isEmpty())
        out << QStringLiteral("QTabBar::tab { %1; }").arg(tabProps.join(QStringLiteral("; ")));

    if (s.backgroundHover.isValid())
        out << QStringLiteral("QTabBar::tab:hover { background: %1; }").arg(c(s.backgroundHover));

    if (s.backgroundPressed.isValid() && s.text.isValid())
        out << QStringLiteral("QTabBar::tab:selected { background: %1; color: %2; border-bottom: none }")
            .arg(c(s.backgroundPressed), c(s.text));

    out << QStringLiteral("QTabBar::close-button { image: url(:/icons/close-tab.png); subcontrol-position: right; }");

    return out;
}

static QStringList pushButtonExtra(const QtComponentStyle& s)
{
    QStringList out;
    QColor checkedBg = s.backgroundSelected.isValid() ? s.backgroundSelected : s.background;
    if (checkedBg.isValid() && s.text.isValid())
        out << QStringLiteral("QPushButton:checked { background: %1; color: %2; border-color: %3; }")
            .arg(c(checkedBg), c(s.text))
            .arg(c(s.borderHover.isValid() ? s.borderHover : s.border));

    if (s.textDisabled.isValid() && s.background.isValid())
        out << QStringLiteral("QPushButton:disabled { color: %1; border-color: %2; }")
            .arg(c(s.textDisabled), c(s.background));

    return out;
}

static QStringList toolButtonExtra(const QtComponentStyle& s)
{
    QStringList out;
    int bw = s.borderWidth >= 0 ? s.borderWidth : 1;
    QString bg = c(s.background);
    QString txt = c(s.text);
    QString hover = c(s.backgroundHover.isValid() ? s.backgroundHover : s.background);
    QString sel = c(s.backgroundSelected.isValid() ? s.backgroundSelected : s.background);
    QString txtBr = c(s.backgroundHover.isValid() && s.text.isValid() ? s.text : s.text);
    QString bd = c(s.border);

    if (s.backgroundSelected.isValid() && s.text.isValid())
        out << QStringLiteral("QToolButton:checked { background: %1; color: %2; border: %3px solid %4; }")
            .arg(sel, txtBr).arg(bw).arg(bd);
    if (s.backgroundHover.isValid() && s.text.isValid())
        out << QStringLiteral("QToolButton:hover { background: %1; color: %2; border: %3px solid %4; }")
            .arg(hover, txtBr).arg(bw).arg(bd);
    if (s.backgroundSelected.isValid() && s.text.isValid())
        out << QStringLiteral("QToolButton:checked:hover { background: %1; color: %2; border: %3px solid %4; }")
            .arg(sel, txtBr).arg(bw).arg(bd);

    return out;
}

static QStringList treeWidgetExtra(const QtComponentStyle& s)
{
    QStringList out;
    out << QStringLiteral("QTreeWidget::item { padding: 2px 0; }");
    if (s.backgroundHover.isValid())
        out << QStringLiteral("QTreeWidget::item:hover { background: %1; }").arg(c(s.backgroundHover));
    if (s.background.isValid() && s.text.isValid())
        out << QStringLiteral("QTreeWidget::item:selected { background: %1; color: %2; }")
            .arg(c(s.background), c(s.text));
    return out;
}

static QStringList checkBoxExtra(const QtComponentStyle& s)
{
    QStringList out;
    int bw = s.borderWidth >= 0 ? s.borderWidth : 1;
    if (s.border.isValid() && s.background.isValid())
        out << QStringLiteral("QCheckBox::indicator { width: 14px; height: 14px; border: %1px solid %2; border-radius: 2px; background: %3; }")
            .arg(bw).arg(c(s.border), c(s.background));
    if (s.text.isValid())
        out << QStringLiteral("QCheckBox { color: %1; spacing: 4px; }").arg(c(s.text));
    return out;
}

static QStringList headerViewExtra()
{
    QStringList out;
    out << QStringLiteral("QHeaderView::section { background: palette(window); color: palette(text); border: none; border-bottom: 1px solid palette(mid); padding: 4px 8px; font-weight: bold; }");
    return out;
}

static QStringList listWidgetExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.background.isValid() && s.text.isValid())
        out << QStringLiteral("QListWidget::item:selected { background: %1; color: %2; }")
            .arg(c(s.background), c(s.text));
    if (s.backgroundHover.isValid())
        out << QStringLiteral("QListWidget::item:hover { background: %1; }").arg(c(s.backgroundHover));

    // Provide outline: none for list widgets
    out << QStringLiteral("QListWidget { outline: none; }");

    return out;
}

static QStringList sliderExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.background.isValid())
        out << QStringLiteral("QSlider::groove:horizontal { background: %1; height: 4px; border-radius: %2px; }")
            .arg(c(s.background)).arg(s.borderRadius >= 0 ? s.borderRadius : 2);

    if (s.text.isValid())
        out << QStringLiteral("QSlider::handle:horizontal { background: %1; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }")
            .arg(c(s.text));

    if (s.textDisabled.isValid())
        out << QStringLiteral("QSlider::sub-page:horizontal { background: %1; border-radius: %2px; }")
            .arg(c(s.textDisabled)).arg(s.borderRadius >= 0 ? s.borderRadius : 2);

    return out;
}

static QStringList comboBoxExtra(const QtComponentStyle& s)
{
    QStringList out;

    int arrowWidth = 22;
    int popupRadius = s.borderRadius >= 0 ? s.borderRadius : 0;
    if (s.paddingRight >= 0)
        arrowWidth = qMax(arrowWidth, s.paddingRight + 10);

    out << QStringLiteral("QComboBox::drop-down { border: none; background: transparent; width: %1px; }")
        .arg(arrowWidth);
    out << QStringLiteral("QComboBox::down-arrow { image: url(:/icons/combo-arrow.xpm); width: 11px; height: 8px; }");

    if (s.background.isValid() && s.text.isValid() && s.border.isValid())
        out << QStringLiteral("QComboBox QAbstractItemView { margin: 0; padding: 6px; background: %1; color: %2; border: 1px solid %3; border-radius: %5px; outline: none; selection-background-color: %4; selection-color: %2; }")
            .arg(c(s.background), c(s.text), c(s.border), c(s.backgroundSelected.isValid() ? s.backgroundSelected : s.background), QString::number(popupRadius));
    out << QStringLiteral("QComboBox QAbstractItemView::QWidget { background: %1; border: none; }")
        .arg(c(s.background));
    out << QStringLiteral("QComboBox QAbstractItemView::item { margin: 0; padding: 4px; border: none; border-radius: %1px; background: transparent; }")
        .arg(qMax(0, popupRadius - 1));
    out << QStringLiteral("QComboBox QAbstractItemView::item:selected { background: %1; color: %2; }")
        .arg(c(s.backgroundSelected.isValid() ? s.backgroundSelected : s.background), c(s.text));

    return out;
}

static QStringList spinBoxExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.background.isValid())
        out << QStringLiteral("QSpinBox::up-button, QDoubleSpinBox::up-button,\nQSpinBox::down-button, QDoubleSpinBox::down-button { background: %1; border: none; width: 16px; }")
            .arg(c(s.background));
    return out;
}

static QStringList lineEditExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.borderFocused.isValid())
        out << QStringLiteral("QLineEdit:focus { border-color: %1; }").arg(c(s.borderFocused));
    return out;
}

static QStringList groupBoxExtra(const QtComponentStyle& s)
{
    QStringList out;
    if (s.text.isValid())
        out << QStringLiteral("QGroupBox::title { color: %1; subcontrol-origin: margin; left: 10px; padding: 0 %2px; }")
            .arg(c(s.text)).arg(s.padding >= 0 ? s.padding : 6);
    return out;
}

static QStringList scrollBarExtra(const QtComponentStyle& s)
{
    // Delegate to the shared generator so the global sheet and the per-panel
    // sheets emit an identical, COMPLETE set of subcontrol rules (track + arrows
    // included) — see ScrollBarStyle.hpp for why the completeness matters.
    if (!s.background.isValid() || !s.backgroundHover.isValid())
        return {};

    ScrollBarQssOptions opts;
    opts.borderRadius = s.borderRadius >= 0 ? s.borderRadius : 6;
    return { scrollBarQss(s.background, s.backgroundHover, opts) };
}

// ── Named widget generators ──────────────────────────────────────────

static QString namedBtn(const QString& id, const QtComponentStyle& s)
{
    return block(QStringLiteral("QPushButton#%1").arg(id), s);
}

static QString namedLabel(const QString& id, const QtComponentStyle& s)
{
    return block(QStringLiteral("QLabel#%1").arg(id), s);
}

static QString namedFrame(const QString& id, const QtComponentStyle& s)
{
    return block(QStringLiteral("QFrame#%1").arg(id), s);
}

// ── Main entry point ─────────────────────────────────────────────────

QString ThemeBuilder::buildStyleSheet(const QtTheme& t)
{
    QString out;

    // ── QWidget (font default) ──
    out += block(QStringLiteral("QWidget"), t.QWidget);

    // ── QMainWindow ──
    out += block(QStringLiteral("QMainWindow"), t.QMainWindow);

    // ── QMenuBar ──
    out += block(QStringLiteral("QMenuBar"), t.QMenuBar);
    for (auto& e : menuBarExtra(t.QMenuBar))
        out += e + QStringLiteral("\n");

    // ── QMenu ──
    out += block(QStringLiteral("QMenu"), t.QMenu);
    for (auto& e : menuExtra(t.QMenu))
        out += e + QStringLiteral("\n");

    // ── QToolBar ──
    out += block(QStringLiteral("QToolBar"), t.QToolBar);
    for (auto& e : toolBarExtra(t.QToolBar))
        out += e + QStringLiteral("\n");

    // ── QStatusBar ──
    for (auto& e : statusBarExtra(t.QStatusBar))
        out += e + QStringLiteral("\n");

    // ── QDockWidget ──
    out += block(QStringLiteral("QDockWidget"), t.QDockWidget);
    for (auto& e : dockWidgetExtra(t.QDockWidget))
        out += e + QStringLiteral("\n");
    if (t.QDockWidget.background.isValid() && t.QDockWidget.border.isValid()) {
        out += QStringLiteral(
            "QDockWidgetGroupWindow { background: %1; border: %2px solid %3; }\n"
            "QDockWidgetGroupWindow > QWidget { background: %1; }\n")
            .arg(c(t.QDockWidget.background))
            .arg(t.QDockWidget.borderWidth >= 0 ? t.QDockWidget.borderWidth : 0)
            .arg(c(t.QDockWidget.border));
    }

    // ── QTabWidget + QTabBar ──
    for (auto& e : tabWidgetExtra(t.QTabWidget))
        out += e + QStringLiteral("\n");
    for (auto& e : tabBarExtra(t.QTabBar))
        out += e + QStringLiteral("\n");

    // ── QToolButton ──
    out += block(QStringLiteral("QToolButton"), t.QToolButton);
    for (auto& e : toolButtonExtra(t.QToolButton))
        out += e + QStringLiteral("\n");

    // ── QPushButton ──
    out += block(QStringLiteral("QPushButton"), t.QPushButton);
    {
        QtComponentStyle s;
        s.background = t.QPushButton.backgroundHover;
        auto block = subBlock(QStringLiteral("QPushButton:hover"), s, t.QPushButton);
        if (!block.isEmpty()) out += block + QStringLiteral("\n");
    }
    {
        QtComponentStyle s;
        s.background = t.QPushButton.backgroundPressed;
        auto block = subBlock(QStringLiteral("QPushButton:pressed"), s, t.QPushButton);
        if (!block.isEmpty()) out += block + QStringLiteral("\n");
    }
    for (auto& e : pushButtonExtra(t.QPushButton))
        out += e + QStringLiteral("\n");

    // ── QListWidget ──
    out += block(QStringLiteral("QListWidget"), t.QListWidget);
    for (auto& e : listWidgetExtra(t.QListWidget))
        out += e + QStringLiteral("\n");

    // ── QSlider ──
    for (auto& e : sliderExtra(t.QSlider))
        out += e + QStringLiteral("\n");

    // ── QComboBox ──
    out += block(QStringLiteral("QComboBox"), t.QComboBox);
    {
        QtComponentStyle s;
        s.border = t.QComboBox.borderFocused;
        auto block = subBlock(QStringLiteral("QComboBox:hover"), s, t.QComboBox);
        if (!block.isEmpty()) out += block + QStringLiteral("\n");
    }
    for (auto& e : comboBoxExtra(t.QComboBox))
        out += e + QStringLiteral("\n");

    // ── QSpinBox / QDoubleSpinBox ──
    out += block(QStringLiteral("QSpinBox, QDoubleSpinBox"), t.QSpinBox);
    {
        QtComponentStyle s;
        s.border = t.QSpinBox.borderFocused;
        auto block = subBlock(QStringLiteral("QSpinBox:hover, QDoubleSpinBox:hover"), s, t.QSpinBox);
        if (!block.isEmpty()) out += block + QStringLiteral("\n");
    }
    for (auto& e : spinBoxExtra(t.QSpinBox))
        out += e + QStringLiteral("\n");

    // ── QLineEdit ──
    out += block(QStringLiteral("QLineEdit"), t.QLineEdit);
    for (auto& e : lineEditExtra(t.QLineEdit))
        out += e + QStringLiteral("\n");

    // ── QLabel ──
    out += block(QStringLiteral("QLabel"), t.QLabel);
    out += QStringLiteral("QLabel { background: transparent; border: none; }\n");

    // ── QScrollArea (global default handled in Theme::globalStyleSheet) ──

    // ── QGroupBox ──
    out += block(QStringLiteral("QGroupBox"), t.QGroupBox);
    for (auto& e : groupBoxExtra(t.QGroupBox))
        out += e + QStringLiteral("\n");

    // ── QScrollBar ──
    for (auto& e : scrollBarExtra(t.QScrollBar))
        out += e + QStringLiteral("\n");

    // ── QDialog ──
    out += block(QStringLiteral("QDialog"), t.QDialog);

    // ── QFrame ──
    out += block(QStringLiteral("QFrame"), t.QFrame);

    // ── QTextEdit ──
    out += block(QStringLiteral("QTextEdit"), t.QTextEdit);

    // ── QPlainTextEdit ──
    out += block(QStringLiteral("QPlainTextEdit"), t.QPlainTextEdit);

    // ── QCheckBox ──
    out += block(QStringLiteral("QCheckBox"), t.QCheckBox);
    for (auto& e : checkBoxExtra(t.QCheckBox))
        out += e + QStringLiteral("\n");

    // ── QRadioButton ──
    out += block(QStringLiteral("QRadioButton"), t.QRadioButton);

    // ── QTreeWidget ──
    out += block(QStringLiteral("QTreeWidget"), t.QTreeWidget);
    for (auto& e : treeWidgetExtra(t.QTreeWidget))
        out += e + QStringLiteral("\n");

    // ── QTableWidget ──
    out += block(QStringLiteral("QTableWidget"), t.QTableWidget);

    // ── QHeaderView ──
    for (auto& e : headerViewExtra())
        out += e + QStringLiteral("\n");

    // ── QSplitter ──
    out += block(QStringLiteral("QSplitter"), t.QSplitter);

    // ── Named widget overrides ──
    for (auto it = t.namedStyles.begin(); it != t.namedStyles.end(); ++it) {
        QString rule = block(it.key(), it.value());
        if (!rule.isEmpty())
            out += rule;
    }

    return out;
}
