#pragma once

#include <QDialog>
#include <QColor>
#include <QPointer>

class SaturationBrightnessArea;
class ColorHueSlider;
class ColorPreviewWidget;
class ColorFieldsWidget;
class CanvasView;
class QPushButton;
class QToolButton;

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

    ~ColorPickerDialog() override;

    // Shows the picker truly modeless. Use this, never QDialog::open(): open()
    // shows a dialog WINDOW-modal, which blocks every click on the main window —
    // the canvas included — so the eyedropper could never reach a pixel and the
    // user could not even scroll the image to find the colour they wanted.
    void showModeless();

    QColor selectedColor() const { return m_selectedColor; }

signals:
    // Every intermediate colour, including eyedropper samples. Call sites whose
    // colour is visible on the canvas (text, shapes, Solid Color, layer styles)
    // connect this to an undo-less live preview; Cancel/Esc/close re-emits the
    // original colour through it, so a cancelled dialog leaves nothing behind.
    void colorPreviewChanged(const QColor& color);
    void colorAccepted(const QColor& color);
    // The eyedropper was armed / released. A caller whose colour covers the
    // canvas (a Solid Color fill layer) hides it while this is true, so the
    // eyedropper can reach the image underneath.
    void eyedropperArmedChanged(bool armed);
    void swatchAddRequested(const QColor& color);
    void colorLibrariesRequested();

public slots:
    // Cancel hands the original colour back to every live-preview listener, so
    // closing the dialog by its window button or with Esc restores the canvas
    // exactly like the Cancel button does.
    void reject() override;
    void done(int result) override;

protected:
    void showEvent(QShowEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onVisualColorChanged(const QColor& color);
    void onHueSliderChanged(double hue);
    void onFieldsColorChanged(const QColor& color);

    // ── Eyedropper (samples the canvas, see CanvasColorPickSource) ──
    void onEyedropperToggled(bool on);
    void onCanvasColorSampled(const QColor& color);
    void onCanvasPickFinished(const QColor& color);
    void onCanvasPickCancelled();

private:
    void syncAllWidgets();
    void setColorFromPick(const QColor& color);
    void armEyedropper();
    void disarmEyedropper();        // un-presses the button + releases the canvas
    void updateEyedropperAvailability();

    ColorPickerMode m_mode;
    QColor m_initialColor;
    QColor m_selectedColor;
    bool m_updatingUI = false;

    // The colour the eyedropper was armed on: a cancelled pick (Esc, or the
    // canvas disarming itself on a tool switch) goes back to it.
    QColor m_colorBeforePick;
    QPointer<CanvasView> m_pickCanvas;

    SaturationBrightnessArea* m_satBrightArea = nullptr;
    ColorHueSlider* m_hueSlider = nullptr;
    ColorPreviewWidget* m_previewWidget = nullptr;
    ColorFieldsWidget* m_fieldsWidget = nullptr;
    QToolButton* m_eyedropperBtn = nullptr;
};
