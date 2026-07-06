#pragma once

#include <QColor>
#include <QString>

struct QtComponentStyle
{
    // ── Colors ──────────────────────────────────────────
    QColor background;
    QColor text;
    QColor border;

    QColor backgroundHover;
    QColor backgroundPressed;
    QColor backgroundDisabled;
    QColor backgroundSelected;
    QColor backgroundFocused;

    QColor textDisabled;

    QColor borderHover;
    QColor borderFocused;

    // ── Geometry (negative = not set) ───────────────────
    int padding      = -1;
    int paddingTop   = -1;
    int paddingRight = -1;
    int paddingBottom= -1;
    int paddingLeft  = -1;

    int margin       = -1;
    int marginTop    = -1;
    int marginRight  = -1;
    int marginBottom = -1;
    int marginLeft   = -1;

    int borderRadius = -1;
    int borderWidth  = -1;

    int minHeight = -1;
    int minWidth  = -1;

    // ── Typography (negative = not set) ──────────────────
    int     fontSize   = -1;
    int     fontWeight = -1;
    QString fontFamily;
    bool    bold       = false;

    bool isEmpty() const;
    void inherit(const QtComponentStyle& base);
};
