#pragma once

#include <QDialog>
#include <QColor>

class SaturationBrightnessArea;
class ColorHueSlider;
class ColorPreviewWidget;
class ColorFieldsWidget;
class QPushButton;

enum class ColorPickerMode {
    Foreground,
    Background
};

class ColorPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ColorPickerDialog(const QColor& initialColor,
                               ColorPickerMode mode,
                               QWidget* parent = nullptr);

    QColor selectedColor() const { return m_selectedColor; }

signals:
    void colorPreviewChanged(const QColor& color);
    void colorAccepted(const QColor& color);
    void swatchAddRequested(const QColor& color);
    void colorLibrariesRequested();

private slots:
    void onVisualColorChanged(const QColor& color);
    void onHueSliderChanged(double hue);
    void onFieldsColorChanged(const QColor& color);

private:
    void syncAllWidgets();

    ColorPickerMode m_mode;
    QColor m_initialColor;
    QColor m_selectedColor;
    bool m_updatingUI = false;

    SaturationBrightnessArea* m_satBrightArea = nullptr;
    ColorHueSlider* m_hueSlider = nullptr;
    ColorPreviewWidget* m_previewWidget = nullptr;
    ColorFieldsWidget* m_fieldsWidget = nullptr;
};
