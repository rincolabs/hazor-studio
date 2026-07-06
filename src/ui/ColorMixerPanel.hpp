#pragma once

#include <QWidget>
#include <QColor>

class ColorEngine;
class ColorSwapWidget;
class SaturationBrightnessArea;
class ColorHueSlider;

// "Color" panel: foreground/background swatches, a
// saturation/value area and a vertical hue slider. It reuses the same
// sub-widgets as ColorPickerDialog and keeps no independent colour state —
// ColorEngine is the single source of truth. The Swatches panel (legacy
// ColorPanel) shares the same ColorEngine and stays in sync automatically.
class ColorMixerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ColorMixerPanel(ColorEngine* engine, QWidget* parent = nullptr);

private slots:
    void onSvAreaChanged(const QColor& color);
    void onHueChanged(double hue);
    void onActiveTargetChanged(bool foreground);

private:
    void pushColor(const QColor& color);
    void syncFromEngine();
    void applyTheme();
    QColor activeColor() const;

    ColorEngine* m_engine = nullptr;

    ColorSwapWidget* m_swap = nullptr;
    SaturationBrightnessArea* m_svArea = nullptr;
    ColorHueSlider* m_hueSlider = nullptr;

    bool m_activeForeground = true;
    double m_hue = 0.0;        // preserved hue (avoids reset on grays)
    bool m_syncing = false;    // guard against feedback loops
};
