#pragma once

#include <QWidget>
#include <QFutureWatcher>

#include "core/CurvesData.hpp"

#include <array>
#include <vector>

class QComboBox;
class QLabel;
class QToolButton;
class QPushButton;
class QSlider;
class QSpinBox;
class ImageController;
class CurveGraphWidget;
class AdjustmentPanelBar;

// Interactive editor for a Curves adjustment layer. Hosted by PropertiesPanel
// (and reusable in a standalone dialog later — it depends only on
// ImageController + a flat index). Reads/writes the active adjustment node's
// params through the controller: live, undo-less updates while dragging and a
// single AdjustmentParamsCommand per committed gesture.
class CurvesEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurvesEditorWidget(QWidget* parent = nullptr);

    void setController(ImageController* ctrl);

    // Binds the editor to the curves adjustment at `flatIndex` and reloads its
    // state. Pass -1 to detach.
    void showNode(int flatIndex);
    int currentFlatIndex() const { return m_flatIndex; }

    // Canvas eyedropper pick at a document position (mode 1=black/2=gray/3=white):
    // samples the curve's INPUT (own effect bypassed) and applies the change.
    void pickInputColor(const QPointF& docPos, int mode);

    // Target (on-image) tool drag from the canvas: begin samples the input tone
    // and grabs/creates its curve point, drag moves that point's output by the
    // vertical delta, end commits one undo step.
    void targetBegan(const QPointF& docPos);
    void targetDragged(int dyPixels);
    void targetEnded();

signals:
    // The editor wants the canvas armed to sample one pixel for an eyedropper /
    // target tool. MainWindow routes this to CanvasView::setCurvesPickMode and
    // feeds the sampled color back through applyPickedColor().
    void requestImagePick(int mode);

protected:
    void hideEvent(QHideEvent* e) override;

private:
    void buildUi();
    // Reload working data + UI from the bound node (no signals emitted).
    void reloadFromNode();
    void refreshHeader();
    void refreshHistogram();
    void updateChannelView();          // points → graph + sliders + spins
    void updateSliderRange();
    void updateInputOutputFields();

    curves::Channel currentChannel() const;

    // Param plumbing.
    QVariantMap paramsFromData() const;
    void applyLive();                                  // push live to controller
    void beginGesture();                               // capture undo "before"
    void commitGesture(const QString& label);          // push one undo step
    void applyAndCommit(const QString& label);         // immediate (begin+commit)
    void markCustomPreset();

    // Graph callbacks.
    void onGraphEditBegan();
    void onGraphPointsChanged();
    void onGraphEditCommitted();
    void onGraphSelectionChanged(int index);

    // Control callbacks.
    void onPresetActivated(int index);
    void onChannelActivated(int index);
    void onBlackSlider(int value);
    void onWhiteSlider(int value);
    void onSliderReleased();
    void onInputEdited(int value);
    void onOutputEdited(int value);
    void onReset(bool shiftHeld);
    void onClipToggled(bool single);
    void onPreviewToggled(bool on);
    void onVisibilityToggled(bool visible);
    void onDelete();
    void onAuto();
    void onEditMode();                 // edit-points mode
    void onPencilMode();               // freehand mode
    void onSmooth();
    void onHistRefresh();
    void onEyedropper(int mode, bool on);  // arm/disarm a pick (1/2/3/4)
    void disarmPicks();                    // uncheck pick buttons + disarm canvas
    void applyPickedColor(const QColor& color, int mode);  // black/gray/white
    int sampledChannelValue(const QColor& c) const;  // value for current channel
    // Composites the document with THIS adjustment bypassed and reads the pixel
    // at `docPos` — the curve's input color (no self-accumulation).
    QColor sampleInputColor(const QPointF& docPos);
    // Ensure a point exists at input x on the current channel (snapping to a
    // nearby one within tolerance); returns its index.
    int ensurePointAt(int x);
    // Shows the "histogram stale" indicator when the composite changed since the
    // backdrop histogram was last computed.
    void updateHistWarning();
    // External document mutation (undo/redo, etc.): reload if still bound to a
    // curves node and not mid-gesture.
    void onDocumentChanged();

    ImageController* m_ctrl = nullptr;
    int m_flatIndex = -1;

    curves::CurvesData m_data;          // authoritative working copy
    bool m_inGesture = false;
    QVariantMap m_gestureBefore;        // committed state at gesture start
    bool m_previewOn = true;            // false → effect bypassed (identity) live
    bool m_loading = false;             // guard against feedback while syncing UI

    // Cached backdrop histogram bins (recomputed only when binding a different
    // node — stable during editing, not recomputed per-frame).
    std::array<int, 256> m_histR{}, m_histG{}, m_histB{}, m_histLum{};
    bool m_hasHistogram = false;
    int m_histNodeIndex = -2;
    unsigned long long m_histGeneration = 0;  // compositionGeneration at compute

    // The backdrop histogram needs a full-document composite, which is far too
    // expensive to run on the UI thread on a large document (it froze the panel
    // when a Curves layer was created). It is computed off the UI thread from a
    // cheap copy-on-write snapshot; results land back here via m_histWatcher.
    struct HistJob {
        std::array<int, 256> r{}, g{}, b{}, lum{};
        bool valid = false;
        int nodeIndex = -1;
        unsigned long long generation = 0;
        unsigned long long token = 0;
    };
    QFutureWatcher<HistJob>* m_histWatcher = nullptr;
    unsigned long long m_histToken = 0;   // bumped per request; stale results dropped
    void onHistogramReady();
    // Blocking composite+bin, used only as a fallback for the explicit Auto action
    // when the async backdrop histogram has not landed yet. Returns m_hasHistogram.
    bool computeHistogramSync();

    // Target (on-image) drag session.
    int m_targetPointIndex = -1;
    int m_targetStartOutput = 0;

    // Header
    QLabel* m_iconLabel = nullptr;
    QLabel* m_maskThumb = nullptr;
    QLabel* m_titleLabel = nullptr;

    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_channelCombo = nullptr;
    QPushButton* m_autoBtn = nullptr;
    QToolButton* m_targetBtn = nullptr;

    QToolButton* m_eyeBlack = nullptr;
    QToolButton* m_eyeGray = nullptr;
    QToolButton* m_eyeWhite = nullptr;

    QToolButton* m_editCurveBtn = nullptr;
    QToolButton* m_pencilBtn = nullptr;
    QToolButton* m_smoothBtn = nullptr;
    QToolButton* m_histWarnBtn = nullptr;

    CurveGraphWidget* m_graph = nullptr;

    QSlider* m_blackSlider = nullptr;
    QSlider* m_whiteSlider = nullptr;

    QSpinBox* m_inputSpin = nullptr;
    QSpinBox* m_outputSpin = nullptr;

    // Shared adjustment-layer bottom toolbar (Clip/Preview/Reset/Vis/Delete).
    AdjustmentPanelBar* m_bar = nullptr;
};

// ── Interactive curve graph ──────────────────────────────────────────────────
// Edits a single channel's control points. Self-contained: it owns a copy of
// the points and emits begin/change/commit so the editor can drive live preview
// + undo. Drawing: dark grid, gray histogram backdrop, diagonal baseline, the
// monotone-spline curve, and draggable control points (selected one highlighted).
class CurveGraphWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Points, Pencil };

    explicit CurveGraphWidget(QWidget* parent = nullptr);

    void setMode(Mode m) { m_mode = m; update(); }
    Mode mode() const { return m_mode; }

    void setPoints(const std::vector<QPoint>& pts);   // no signals
    const std::vector<QPoint>& points() const { return m_points; }

    void setSelectedIndex(int idx);                   // no signals
    int selectedIndex() const { return m_selected; }

    void setChannelColor(const QColor& c);
    void setHistogram(const std::array<int, 256>& bins);
    void clearHistogram();

    QSize sizeHint() const override { return QSize(256, 220); }
    QSize minimumSizeHint() const override { return QSize(200, 160); }

signals:
    void editBegan();
    void pointsChanged();        // live (read points())
    void editCommitted();
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRect plotRect() const;
    QPointF toWidget(const QPoint& curvePt) const;     // curve(0..255) → pixels
    QPoint toCurve(const QPointF& widgetPt) const;      // pixels → curve(0..255)
    int hitTest(const QPointF& widgetPt) const;         // point index or -1
    void removePoint(int idx);                          // interior only
    void pencilStroke(const QPoint& cp);                // freehand paint to cp

    std::vector<QPoint> m_points{ {0, 0}, {255, 255} };
    int m_selected = -1;
    bool m_dragging = false;
    Mode m_mode = Mode::Points;
    std::array<int, 256> m_pencilBuf{};                 // freehand sample buffer
    int m_pencilLastX = -1;
    QColor m_color{ 220, 220, 220 };
    std::array<int, 256> m_hist{};
    int m_histMax = 0;
    bool m_hasHist = false;
};
