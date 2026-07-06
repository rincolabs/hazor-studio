#include "DarkerTheme.hpp"

DarkerTheme::DarkerTheme(QObject *parent)
    : ClassicTheme(parent)
{
    // colorBackgroundPrimary = QColor("#232323");
    // colorBackgroundSecondary = QColor("#323232");
    // colorBackgroundTertiary = QColor("#333333");

    // // ── Surface ────────────────────────────────────────
    // colorSurface = QColor("#424242");
    // colorSurfaceDark = QColor("#303030");
    // colorSurfaceHover = QColor("#464646");
    // colorSurfacePressed = QColor("#505050");
    // // colorSurfaceSelected = QColor("#4059b3");
    // colorSurfaceSelected = QColor("#2e2e2e");
    // colorSurfaceRow = QColor("#404040");
    // colorPanelBackground = QColor("#353535");

    // // ── Text ───────────────────────────────────────────
    // colorTextPrimary = QColor("#E6E6E6");
    // colorTextBright = QColor("#FFFFFF");
    // colorTextSecondary = QColor("#C2C2C2");
    // colorTextDisabled = QColor("#8A8A8A");
    // colorTextInverted = QColor("#1F1F1F");

    // // ── Border ─────────────────────────────────────────
    // // colorBorder      = QColor("#4A4A4A");
    // // colorBorder      = QColor("#242424");
    // colorBorder = QColor("#2b2b2b");
    // colorBorderFocus = QColor("#6CA0DC");

    // // ── Accent ─────────────────────────────────────────
    // colorAccent = QColor("#5C8FD8");
    // colorAccentHover = QColor("#76A7EA");
    // colorAccentPressed = QColor("#4B7CC2");

    // colorTextSelection = QColor(92, 143, 216, 90);

    // // ── Splash ─────────────────────────────────────────
    // colorSplashBackground = QColor("#2B2B2B");
    // colorSplashBorder = QColor("#4A4A4A");
    // colorSplashProgressTrack = QColor("#3A3A3A");

    // colorSplashProgressStart = QColor("#365d94");
    // colorSplashProgressEnd = QColor("#8BC3FF");

    // colorSplashText = QColor("#C2C2C2");
    // colorSplashVersion = QColor("#8A8A8A");

    // // ── Status ─────────────────────────────────────────
    // colorDanger = QColor("#D96C6C");
    // colorDangerHover = QColor("#E47C7C");

    // colorWarning = QColor("#D7A65A");

    // colorSuccess = QColor("#6FBF73");

    // canvasBackground = QColor("#272727");

    // ##################################################################################

    // ============================================================
    // Theme Base Colors
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
    populateComponentTheme();

    // Keep checkbox metrics explicit in Darker so it can diverge independently later.
    {
        auto &s = m_componentTheme.QCheckBox;
        s.minWidth = 17;
        s.minHeight = 22;
    }
}
