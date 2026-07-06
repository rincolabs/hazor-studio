#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include "QtTheme.hpp"

class Theme : public QObject
{
    Q_OBJECT

public:
    // ── Background ──
    QColor colorBackgroundPrimary;
    QColor colorBackgroundSecondary;
    QColor colorBackgroundTertiary;

    // ── Surface ──
    QColor colorSurface;
    QColor colorSurfaceDark;
    QColor colorSurfaceHover;
    QColor colorSurfacePressed;
    QColor colorSurfaceSelected;
    QColor colorSurfaceRow;
    QColor colorPanelBackground;

    // ── Text ──
    QColor colorTextPrimary;
    QColor colorTextBright;
    QColor colorTextSecondary;
    QColor colorTextDisabled;
    QColor colorTextInverted;

    // ── Border ──
    QColor colorBorder;
    QColor colorBorderLight;
    QColor colorBorderFocus;

    // ── Accent ──
    QColor colorAccent;
    QColor colorAccentHover;
    QColor colorAccentPressed;
    QColor colorTextSelection;

    // ── Status ──
    QColor colorDanger;
    QColor colorDangerHover;
    QColor colorWarning;
    QColor colorSuccess;

    // Widgets
    QColor colorHistogramBackdrop;


    // ── Spacing (px) ──
    int spaceXS = 2;
    int spaceSM = 6;
    int spaceMD = 8;
    int spaceLG = 12;
    int spaceXL = 16;
    int spaceXXL = 24;

    // ── Radius (px) ──
    int radiusSM = 2;
    int radiusMD = 4;
    int radiusLG = 6;

    // ── Border width ──
    int borderThinWidth = 1;
    int borderNormalWidth = 1;

    // ── Typography ──
    QString fontFamily = QStringLiteral("Inter");
    QString fontFamilyMono = QStringLiteral("JetBrains Mono");

    int fontSizeXS = 10;
    int fontSizeSM = 11;
    int fontSizeMD = 12;
    int fontSizeLG = 13;

    // int fontSizeXS = 8;
    // int fontSizeSM = 8;
    // int fontSizeMD = 9;
    // int fontSizeLG = 10;

    // ── Splash screen ──
    QColor colorSplashBackground;
    QColor colorSplashBorder;
    QColor colorSplashProgressTrack;
    QColor colorSplashProgressStart;
    QColor colorSplashProgressEnd;
    QColor colorSplashText;
    QColor colorSplashVersion;

    // ── Icon ──
    float iconOpacity = 0.90f;
    // float iconOpacity = 0.65f;

    // ── Canvas (OpenGL clear color) ──
    QColor canvasBackground = QColor("#111315");

    // ── Component-based theme (populated by subclasses) ──
    QtTheme m_componentTheme;

    virtual void populateComponentTheme();

    const QtTheme& componentTheme() const { return m_componentTheme; }

    // ── Generated stylesheets ──
    QString globalStyleSheet() const;
    QString titleBarStyleSheet() const;
    QString layerPanelStyleSheet() const;
    QString layerItemDelegateEditorStyleSheet() const;
    QString historyPanelStyleSheet() const;
    QString agentPanelStyleSheet() const;
    QString agentConfigDialogStyleSheet() const;
    QString brushDynamicsStyleSheet() const;
    QString exportDialogStyleSheet() const;

signals:
    void changed();

protected:
    explicit Theme(QObject* parent = nullptr);
};
