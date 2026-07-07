#include "ClassicTheme.hpp"

#include <algorithm>

ClassicTheme::ClassicTheme(QObject* parent)
    : Theme(parent)
{
    // ============================================================
    // Theme Base Colors — every structural tone is derived from a
    // single base color via tone() offsets (same model as Darker).
    // ============================================================

    QColor baseColor = QColor("#444647");
    QColor selectionColor = QColor("#36383a");

    // ============================================================
    // Helper
    // ============================================================

    auto tone = [](const QColor &c, int delta)
    {
        return QColor(
            std::clamp(c.red() + delta, 0, 255),
            std::clamp(c.green() + delta, 0, 255),
            std::clamp(c.blue() + delta, 0, 255));
    };

    // ============================================================
    // Background
    // ============================================================

    colorBackgroundPrimary = tone(baseColor, -31);
    colorBackgroundSecondary = tone(baseColor, -16);
    colorBackgroundTertiary = tone(baseColor, -15);

    // ============================================================
    // Surface
    // ============================================================

    colorSurface = baseColor;
    colorSurfaceDark = tone(baseColor, -18);
    colorSurfaceHover = tone(baseColor, +4);
    colorSurfacePressed = tone(baseColor, +14);
    colorSurfaceSelected = selectionColor;
    colorSurfaceRow = tone(baseColor, -2);
    colorPanelBackground = tone(baseColor, -13);

    // ============================================================
    // Text
    // ============================================================

    colorTextPrimary = QColor("#E6E6E6");
    colorTextBright = QColor("#FFFFFF");
    colorTextSecondary = QColor("#C2C2C2");
    colorTextDisabled = QColor("#8A8A8A");
    colorTextInverted = QColor("#1F1F1F");

    // ============================================================
    // Border
    // ============================================================

    colorBorder = tone(baseColor, -23);
    colorBorderLight = tone(baseColor, +23);
    colorBorderFocus = QColor("#6CA0DC");

    // ============================================================
    // Accent
    // ============================================================

    colorAccent = QColor("#5C8FD8");
    colorAccentHover = QColor("#76A7EA");
    colorAccentPressed = QColor("#4B7CC2");

    colorTextSelection = QColor(
        colorAccent.red(),
        colorAccent.green(),
        colorAccent.blue(),
        90);

    // ============================================================
    // Splash
    // ============================================================

    colorSplashBackground = tone(baseColor, -23);
    colorSplashBorder = tone(baseColor, +8);
    colorSplashProgressTrack = tone(baseColor, -8);

    colorSplashProgressStart = QColor("#365d94");
    colorSplashProgressEnd = QColor("#8BC3FF");

    colorSplashText = colorTextSecondary;
    colorSplashVersion = colorTextDisabled;

    // ============================================================
    // Status
    // ============================================================

    colorDanger = QColor("#D96C6C");
    colorDangerHover = QColor("#E47C7C");

    colorWarning = QColor("#D7A65A");

    colorSuccess = QColor("#6FBF73");

    // ============================================================
    // Canvas
    // ============================================================

    canvasBackground = tone(baseColor, -27);

    m_componentTheme = QtTheme{};

    // // ── Background ─────────────────────────────────────
    // colorBackgroundPrimary   = tone(baseColor, -22);
    // colorBackgroundSecondary = tone(baseColor, -15);
    // colorBackgroundTertiary  = tone(baseColor, -7);

    // // ── Surface ────────────────────────────────────────
    // colorSurface         = baseColor;
    // colorSurfaceDark     = tone(baseColor, -8);
    // colorSurfaceHover    = tone(baseColor, +8);
    // colorSurfacePressed  = tone(baseColor, +15);
    // colorSurfaceSelected = selectionColor;
    // colorSurfaceRow      = tone(baseColor, +4);
    // colorPanelBackground = tone(baseColor, -16);

    // // ── Text ───────────────────────────────────────────
    // colorTextPrimary   = QColor("#abb5c2");
    // colorTextBright    = QColor("#ccd4df");
    // colorTextSecondary = QColor("#A7B0BC");
    // colorTextDisabled  = QColor("#6B7480");
    // colorTextInverted  = tone(baseColor, -22);

    // // ── Border ─────────────────────────────────────────
    // colorBorder      = tone(baseColor, +19);
    // colorBorderLight = tone(baseColor, +84);
    // colorBorderFocus = QColor("#4C8DFF");

    // // ── Accent ─────────────────────────────────────────
    // colorAccent        = QColor("#4C8DFF");
    // colorAccentHover   = QColor("#6AA3FF");
    // colorAccentPressed = QColor("#3B78E7");

    // colorTextSelection = QColor(
    //     colorAccent.red(),
    //     colorAccent.green(),
    //     colorAccent.blue(),
    //     90);

    // // ── Splash ─────────────────────────────────────────
    // colorSplashBackground    = tone(baseColor, -22);
    // colorSplashBorder        = tone(baseColor, +8);
    // colorSplashProgressTrack = baseColor;

    // colorSplashProgressStart = QColor("#4C8DFF");
    // colorSplashProgressEnd   = QColor("#35D6FF");

    // colorSplashText    = colorTextSecondary;
    // colorSplashVersion = colorTextDisabled;

    // // ── Status ─────────────────────────────────────────
    // colorDanger      = QColor("#FF5D73");
    // colorDangerHover = QColor("#FF7A8C");

    // colorWarning = QColor("#FFB547");

    // colorSuccess = QColor("#2ED47A");

    // colorHistogramBackdrop =  QColor("#979797");

    populateComponentTheme();
}

void ClassicTheme::populateComponentTheme()
{
    // ── QWidget (global font defaults) ──
    {
        auto& s = m_componentTheme.QWidget;
        s.fontFamily = fontFamily;
        s.fontSize   = fontSizeMD;
    }

    // ── QMainWindow ──
    {
        auto& s = m_componentTheme.QMainWindow;
        s.background = colorBackgroundPrimary;
    }

    // ── QMenuBar ──
    {
        auto& s = m_componentTheme.QMenuBar;
        s.background       = colorSurface;
        s.text             = colorTextPrimary;
        s.border           = colorBackgroundPrimary;
        s.backgroundHover  = colorSurfaceHover;
        s.borderWidth      = 0;
    }

    // ── QMenu ──
    {
        auto& s = m_componentTheme.QMenu;
        s.background       = colorSurface;
        s.text             = colorTextPrimary;
        s.border           = colorBorder;
        s.backgroundHover  = colorSurfaceHover;
        s.textDisabled     = colorTextDisabled;
        s.padding          = spaceSM;
        s.paddingLeft      = spaceXL;
        s.paddingRight     = spaceXL;
        s.margin           = spaceSM;
        s.borderWidth      = 0;
    }

    // ── QToolBar ──
    {
        auto& s = m_componentTheme.QToolBar;
        s.background = colorSurface;
        s.border     = colorBackgroundPrimary;
        s.text       = colorTextPrimary;
        s.borderWidth = borderNormalWidth;
    }

    // ── QStatusBar ──
    {
        auto& s = m_componentTheme.QStatusBar;
        s.background = colorBackgroundTertiary;
        s.text       = colorTextSecondary;
        s.border     = colorBackgroundTertiary;
        s.borderWidth = borderNormalWidth;
        s.padding    = spaceXS;
    }

    // ── QDockWidget ──
    {
        auto& s = m_componentTheme.QDockWidget;
        s.text      = colorTextPrimary;
        s.background = colorSurface;
        s.border     = colorBorder;
        s.borderWidth = 2;
        s.padding   = spaceSM;
    }

    // ── QTabWidget (pane styling) ──
    {
        auto& s = m_componentTheme.QTabWidget;
        s.background = colorBackgroundPrimary;
        s.border     = colorBorder;
        s.borderWidth = borderNormalWidth;
    }

    // ── QTabBar (tab styling via sub-control) ──
    {
        auto& s = m_componentTheme.QTabBar;
        s.backgroundFocused = colorPanelBackground;
        s.background        = colorSurfaceDark;
        s.text              = colorTextPrimary;
        s.textDisabled      = colorTextDisabled;
        s.padding           = spaceSM;
        s.paddingLeft       = 8;
        s.paddingRight      = 8;
        s.border            = colorBackgroundTertiary;
        s.backgroundHover   = colorSurfaceHover;
        s.backgroundPressed = colorSurface;
        s.borderWidth       = borderNormalWidth;
        s.fontWeight        = 700;
    }

    // ── QToolButton ──
    {
        auto& s = m_componentTheme.QToolButton;
        s.background        = colorSurface;
        s.text              = colorTextPrimary;
        s.borderRadius      = radiusMD;
        s.padding           = spaceXS;
        s.margin            = spaceXS;
        s.backgroundHover   = colorSurfaceHover;
        s.backgroundSelected= colorSurfaceSelected;
        s.borderWidth       = 1;
    }

    // ── QPushButton ──
    {
        auto& s = m_componentTheme.QPushButton;
        s.background        = colorSurface;
        s.text              = colorTextPrimary;
        s.border            = colorBorder;
        s.borderRadius      = radiusMD;
        s.padding           = spaceSM;
        s.paddingLeft       = spaceLG;
        s.paddingRight      = spaceLG;
        s.backgroundHover   = colorSurfaceHover;
        s.backgroundPressed = colorSurfacePressed;
        s.backgroundSelected= colorSurfaceSelected;
        s.textDisabled      = colorTextDisabled;
        s.borderHover       = colorAccentHover;
        s.borderWidth       = 0;
    }

    // ── QListWidget ──
    {
        auto& s = m_componentTheme.QListWidget;
        s.background        = colorSurfacePressed;
        s.text              = colorTextPrimary;
        s.border            = colorBorder;
        s.borderRadius      = radiusMD;
        s.backgroundHover   = colorSurfaceHover;
        s.borderWidth       = borderNormalWidth;
    }

    // ── QSlider ──
    {
        auto& s = m_componentTheme.QSlider;
        s.background        = colorSurfaceHover;
        s.text              = colorTextPrimary;
        s.textDisabled      = colorTextDisabled;
        s.borderRadius      = radiusSM;
    }

    // ── QComboBox ──
    {
        auto& s = m_componentTheme.QComboBox;
        s.background        = colorBackgroundTertiary;
        s.text              = colorTextPrimary;
        s.border            = colorBorderLight;
        s.borderRadius      = radiusSM;
        s.minHeight         = 18;
        s.padding           = 2;
        s.paddingLeft       = 8;
        s.paddingRight      = 8;
        s.borderFocused     = colorBorderFocus;
        s.borderWidth       = borderNormalWidth;
    }

    // ── QSpinBox ──
    {
        auto& s = m_componentTheme.QSpinBox;
        s.background        = colorBackgroundTertiary;
        s.text              = colorTextPrimary;
        s.border            = colorBorderLight;
        s.borderRadius      = radiusMD;
        s.minHeight         = 18;
        s.padding           = 1;
        s.borderFocused     = colorBorderFocus;
        s.borderWidth       = borderNormalWidth;
    }

    // ── QDoubleSpinBox (mirrors QSpinBox) ──
    m_componentTheme.QDoubleSpinBox = m_componentTheme.QSpinBox;

    // ── QLineEdit ──
    {
        auto& s = m_componentTheme.QLineEdit;
        s.background        = colorBackgroundTertiary;
        s.text              = colorTextPrimary;
        s.border            = colorBorder;
        s.borderRadius      = radiusMD;
        s.padding           = 4;
        s.paddingLeft       = 4;
        s.paddingRight      = 4;
        s.borderFocused     = colorBorderFocus;
        s.borderWidth       = borderNormalWidth;
    }

    // ── QLabel ──
    {
        auto& s = m_componentTheme.QLabel;
        s.text = colorTextPrimary;
    }

    // ── QGroupBox ──
    {
        auto& s = m_componentTheme.QGroupBox;
        s.text         = colorTextPrimary;
        s.border       = colorBorder;
        s.borderRadius = radiusMD;
        s.padding      = spaceLG;
        s.borderWidth  = borderNormalWidth;
    }

    // ── QScrollBar ──
    {
        auto& s = m_componentTheme.QScrollBar;
        s.background        = colorBackgroundPrimary;
        s.backgroundHover   = colorSurfaceHover;
        s.borderRadius      = radiusLG;
    }

    // ── QDialog ──
    {
        auto& s = m_componentTheme.QDialog;
        s.background = colorSurface;
    }

    // ── QFrame ──
    {
        auto& s = m_componentTheme.QFrame;
        s.border       = colorBorder;
        s.borderRadius = radiusMD;
        s.background   = colorSurface;
        s.borderWidth  = 0;
    }

    // ── QTextEdit ──
    {
        auto& s = m_componentTheme.QTextEdit;
        s.background = colorBackgroundTertiary;
        s.text       = colorTextPrimary;
        s.border     = colorBorder;
        s.borderWidth = borderNormalWidth;
    }

    // ── QPlainTextEdit ──
    {
        auto& s = m_componentTheme.QPlainTextEdit;
        s.background = colorBackgroundTertiary;
        s.text       = colorTextPrimary;
        s.border     = colorBorder;
        s.borderWidth = borderNormalWidth;
    }

    // ── QCheckBox ──
    {
        auto& s = m_componentTheme.QCheckBox;
        s.text               = colorTextPrimary;
        s.textDisabled       = colorTextDisabled;
        s.background         = colorBackgroundTertiary;
        s.backgroundHover    = colorSurfaceHover;
        s.backgroundPressed  = colorSurfacePressed;
        s.backgroundDisabled = colorSurface;
        s.border             = colorBorder;
        s.borderHover        = colorTextSecondary;
        s.borderFocused      = colorBorderFocus;
        s.borderRadius       = radiusSM;
        s.borderWidth        = borderThinWidth;
        s.paddingLeft        = 6;
        s.minWidth           = 17;
        s.minHeight          = 22;
        s.fontSize           = fontSizeSM;
        s.fontWeight         = 400;
    }

    // ── QRadioButton ──
    {
        auto& s = m_componentTheme.QRadioButton;
        s.text = colorTextPrimary;
    }

    // ── QTreeWidget ──
    {
        auto& s = m_componentTheme.QTreeWidget;
        s.background        = colorBackgroundPrimary;
        s.text              = colorTextPrimary;
        s.border            = colorBorder;
        s.backgroundHover   = colorSurfaceHover;
        s.borderWidth       = borderNormalWidth;
    }

    // ── QTableWidget ──
    {
        auto& s = m_componentTheme.QTableWidget;
        s.background = colorBackgroundPrimary;
        s.text       = colorTextPrimary;
        s.border     = colorBorder;
        s.borderWidth = borderNormalWidth;
    }

    // ── QSplitter ──
    {
        auto& s = m_componentTheme.QSplitter;
        s.background = colorBackgroundPrimary;
    }

    // ── Named widget overrides ──

    // QFrame#dimFrame — dedicated dimming frame
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QFrame#dimFrame")];
        s.border       = colorBorder;
        s.borderRadius = radiusMD;
        s.background   = colorSurface;
        s.borderWidth  = borderNormalWidth;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QWidget#layerPanel")];
        s.borderRadius = 0;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QWidget#propertiesPanel")];
        s.borderRadius = 0;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QWidget#colorPanel")];
        s.borderRadius = 0;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QWidget#historyPanel")];
        s.borderRadius = 0;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QWidget#agentPanel")];
        s.background   = colorSurface;
        s.borderRadius = 0;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QListWidget#historyPanelList")];
        s.background   = colorSurfaceDark;
        s.text         = colorTextPrimary;
        s.border       = colorBorder;
        s.borderWidth  = borderNormalWidth;
        s.borderRadius = 0;
    }

    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QTextEdit#agentLogView")];
        s.background   = colorBackgroundTertiary;
        s.text         = colorTextPrimary;
        s.border       = colorBorder;
        s.borderWidth  = borderNormalWidth;
        s.borderRadius = 0;
    }

    // QPushButton#okBtn — primary action button (accent background)
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QPushButton#okBtn")];
        s.background   = colorAccentPressed;
        s.text         = QColor("#FFFFFF");
        s.borderRadius = radiusMD;
        s.padding      = spaceSM;
        s.paddingLeft  = spaceXL;
        s.paddingRight = spaceXL;
        s.fontWeight   = 700;
    }

    // QPushButton#cancelBtn — secondary action button
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QPushButton#cancelBtn")];
        s.border       = colorBorder;
        s.borderRadius = radiusMD;
        s.padding      = spaceSM;
        s.paddingLeft  = spaceXL;
        s.paddingRight = spaceXL;
        s.borderWidth  = borderNormalWidth;
    }

    // QPushButton#savePresetBtn, QPushButton#deletePresetBtn — shared border/padding
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QPushButton#savePresetBtn, QPushButton#deletePresetBtn")];
        s.border       = colorBorder;
        s.borderRadius = radiusMD;
        s.padding      = spaceSM;
        s.paddingLeft  = spaceLG;
        s.paddingRight = spaceLG;
        s.borderWidth  = borderNormalWidth;
    }

    // QPushButton#deletePresetBtn — individual color override
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QPushButton#deletePresetBtn")];
        s.text = colorTextDisabled;
    }

    // QPushButton#bgColorBtn — background color picker button
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QPushButton#bgColorBtn")];
        s.border       = colorBorder;
        s.borderRadius = radiusSM;
        s.borderWidth  = borderNormalWidth;
    }

    // QPushButton#colorBtn — foreground color picker button
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QPushButton#colorBtn")];
        s.border       = colorBorder;
        s.borderRadius = radiusSM;
        s.borderWidth  = borderNormalWidth;
    }

    // QLabel#infoLabel — informational text in dialogs
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QLabel#infoLabel")];
        s.text = colorTextSecondary;
    }

    // QLabel#footerLabel — footer text in dialogs
    {
        auto& s = m_componentTheme.namedStyles[QStringLiteral("QLabel#footerLabel")];
        s.text = colorTextDisabled;
    }
}
