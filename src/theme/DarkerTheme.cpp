#include "DarkerTheme.hpp"

DarkerTheme::DarkerTheme(QObject *parent)
    : ClassicTheme(parent)
{
    // ============================================================
    // Theme Base Colors
    // ============================================================


    QColor baseColor = QColor("#2c2e31");
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
