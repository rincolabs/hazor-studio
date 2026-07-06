#pragma once

#include <QWidget>

#include "core/HueSaturationData.hpp"

#include <array>

class QComboBox;
class GradientSlider;
class QLabel;
class QToolButton;
class QPointF;
class ImageController;
class AdjustmentPanelBar;
class AppCheckBox;

// ── Interactive hue range bar ────────────────────────────────────────────────
// The hue range control at the bottom of the panel: two stacked rainbow
// strips (top = original hue spectrum, bottom = a preview of the active range's
// Hue/Saturation/Lightness shift) plus, when a hue band is active, four
// draggable handles between them:
//   outerStart (0)  innerStart (1)  innerEnd (2)  outerEnd (3)
// The region between the inner handles gets 100% of the adjustment; the
// shoulders out to the outer handles get a linear falloff; outside gets none.
// The bar maps hue 0..360 left→right and is fully wrap-aware (Reds straddling
// 0/360 renders its coverage across the seam). It reports handle drags as a
// signed degree delta from the press position so dragging never jumps across
// the wrap; the editor clamps the ordering and writes the result into the model.
class HueRangeBar : public QWidget {
    Q_OBJECT
public:
    explicit HueRangeBar(QWidget* parent = nullptr);

    // active == false (Master selected, or Colorize on) → strips only, no
    // handles and no coverage overlay.
    void setActive(bool active);
    // The four band edges in the continuous degree frame (see HueRange).
    void setEdges(float outerStart, float innerStart, float innerEnd, float outerEnd);
    // The active range's Hue/Saturation/Lightness sliders, used only to render
    // the bottom (adjusted) preview strip.
    void setAdjustment(int hueShift, int satAdj, int lightAdj);

signals:
    void handlePressed(int idx);                 // idx 0..3
    void handleDragged(int idx, float deltaDeg); // signed degrees from press
    void handleReleased();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF topStripRect() const;
    QRectF bottomStripRect() const;
    QRectF handleGutterRect() const;
    double handleXForEdge(float edgeDeg) const;  // x pixel for a continuous edge
    // Drawn centre x of each handle (outerStart, innerStart, innerEnd, outerEnd),
    // shared by paintEvent and hitTestHandle so visuals and grab zones agree.
    std::array<double, 4> handleCenters() const;
    int hitTestHandle(const QPointF& pos) const; // nearest handle within tol, else -1

    bool m_active = false;
    float m_edges[4] = { -45.0f, -20.0f, 20.0f, 45.0f };
    int m_hueShift = 0, m_satAdj = 0, m_lightAdj = 0;

    int m_dragHandle = -1;     // 0..3 while dragging
    double m_dragStartX = 0.0;
};

// ── Hue/Saturation adjustment editor ─────────────────────────────────────────
// Interactive editor for a Hue/Saturation adjustment layer. Hosted by
// PropertiesPanel and reusable in a standalone dialog later (depends only on
// ImageController + a flat index). Reads/writes the active adjustment node's
// params through the controller: live, undo-less updates while dragging and a
// single AdjustmentParamsCommand per committed gesture. Mirrors
// ColorBalanceAdjustmentWidget's controller contract and shares AdjustmentPanelBar.
class HueSaturationAdjustmentWidget : public QWidget {
    Q_OBJECT
public:
    explicit HueSaturationAdjustmentWidget(QWidget* parent = nullptr);

    void setController(ImageController* ctrl);

    // Binds the editor to the huesaturation adjustment at `flatIndex` and
    // reloads its state. Pass -1 to detach.
    void showNode(int flatIndex);
    int currentFlatIndex() const { return m_flatIndex; }

signals:
    // The editor wants the active canvas armed to sample the adjustment's INPUT
    // colour: 0 = disarm, 1 = main (recenter), 2 = add, 3 = subtract. MainWindow
    // routes this to CanvasView::setHueSatPickMode.
    void requestImagePick(int mode);

public slots:
    // Canvas eyedropper callbacks (wired through MainWindow). docPos is in
    // document pixels.
    void onPickClicked(const QPointF& docPos, int mode);   // main, one-shot
    void onPickDragBegan(const QPointF& docPos, int mode); // add/subtract press
    void onPickDragMoved(const QPointF& docPos);
    void onPickDragEnded();
    void onPickCancelled();                                // tool switch / Esc

protected:
    void hideEvent(QHideEvent* e) override;

private:
    void buildUi();
    void reloadFromNode();
    void refreshHeader();
    void reloadControlsForRange();   // sliders + spins ← current range's values
    void updateColorizeUiState();    // enable/disable range combo + relabel sliders
    void refreshRangeBar();          // push the active band's geometry to the bar
    void disarmEyedroppers();        // uncheck the 3 buttons + disarm the canvas

    huesaturation::Range currentRange() const;

    // Param plumbing (same contract as ColorBalanceAdjustmentWidget).
    QVariantMap paramsFromData() const;
    void applyLive();
    void beginGesture();
    void commitGesture(const QString& label);
    void applyAndCommit(const QString& label);
    void markCustomPreset();         // manual edit → preset becomes "Custom"
    void syncPresetCombo();

    // Eyedropper helpers.
    QImage captureInputComposite();                   // bypasses this adjustment
    bool sampleHue(const QImage& input, const QPointF& docPos, float& hueOut) const;
    void ensureBandActive(float hueDeg);              // switch Master → nearest band

    // Callbacks.
    void onPresetActivated(int index);
    void onRangeActivated(int index);
    void onSliderChanged(int which, int value);   // which: 0=Hue 1=Sat 2=Light
    void onSliderReleased(int which);
    void onSpinEdited(int which, int value);
    void onColorizeToggled(bool on);
    void onReset(bool shiftHeld);
    void onClipToggled(bool single);
    void onPreviewToggled(bool on);
    void onVisibilityToggled(bool visible);
    void onDelete();
    void onDocumentChanged();
    void onEyedropperToggled(int mode, bool on);
    void onHandlePressed(int idx);
    void onHandleDragged(int idx, float deltaDeg);
    void onHandleReleased();

    void setHslValue(int which, int value);        // writes into m_data's active range
    void refreshSliderBaseHue();

    ImageController* m_ctrl = nullptr;
    int m_flatIndex = -1;

    huesaturation::HueSaturationData m_data;   // authoritative working copy
    bool m_inGesture = false;
    QVariantMap m_gestureBefore;
    bool m_previewOn = true;
    bool m_loading = false;

    // Eyedropper drag state.
    QImage m_eyeInputCache;          // input composite snapshot for a drag
    float m_handleDragBase[4] = {};  // edge baseline captured at handle press

    // Header
    QLabel* m_iconLabel = nullptr;
    QLabel* m_maskThumb = nullptr;
    QLabel* m_titleLabel = nullptr;

    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_rangeCombo = nullptr;
    std::array<GradientSlider*, 3> m_sliders{};
    AppCheckBox* m_colorizeCheck = nullptr;
    HueRangeBar* m_rangeBar = nullptr;
    QToolButton* m_eyeMain = nullptr;
    QToolButton* m_eyeAdd = nullptr;
    QToolButton* m_eyeRemove = nullptr;

    AdjustmentPanelBar* m_bar = nullptr;
};
