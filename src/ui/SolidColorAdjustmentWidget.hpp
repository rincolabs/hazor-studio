#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QWidget>

#include "core/SolidColorData.hpp"

class QLabel;
class QLineEdit;
class QSpinBox;
class ImageController;
class AdjustmentPanelBar;

// ── Large color swatch button ────────────────────────────────────────────────
// A clickable swatch that paints the current colour over a checkerboard (only
// visible where the colour has alpha < 255). Inherits QAbstractButton, so it
// already provides clicked(); the host opens the colour picker on click.
class ColorSwatchButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit ColorSwatchButton(QWidget* parent = nullptr);
    void setColor(const QColor& c);
    QColor color() const { return m_color; }

protected:
    void paintEvent(QPaintEvent* e) override;
    QSize sizeHint() const override { return QSize(120, 48); }

private:
    QColor m_color = Qt::white;
};

// ── Solid Color adjustment editor ────────────────────────────────────────────
// Interactive editor for a Solid Color fill/adjustment layer. Hosted by
// PropertiesPanel (and reusable in a standalone dialog later — it depends only
// on ImageController + a flat index). Reads/writes the active node's colour
// through the controller: live, undo-less updates while dragging in the colour
// picker and a single AdjustmentParamsCommand per committed change. Mirrors the
// controller contract of ColorBalanceAdjustmentWidget and shares the
// AdjustmentPanelBar.
class SolidColorAdjustmentWidget : public QWidget {
    Q_OBJECT
public:
    explicit SolidColorAdjustmentWidget(QWidget* parent = nullptr);

    void setController(ImageController* ctrl);

    // Binds the editor to the solidcolor adjustment at `flatIndex` and reloads
    // its state. Pass -1 to detach.
    void showNode(int flatIndex);
    int currentFlatIndex() const { return m_flatIndex; }

    // Opens the colour picker bound to the current node (used by the swatch and
    // by a double-click on the layer's colour thumbnail). No-op when detached.
    void openColorPicker();

protected:
    void hideEvent(QHideEvent* e) override;

private:
    void buildUi();
    void reloadFromNode();
    void refreshHeader();
    void syncControls();            // swatch + hex + RGB spins ← m_data

    QVariantMap paramsFromData() const { return m_data.toParams(); }
    void setColorLive(const QColor& c);     // updates working data + UI + canvas
    void applyLive();
    void beginGesture();
    void commitGesture(const QString& label);
    void applyAndCommit(const QString& label);

    void onHexEdited();
    void onRgbSpinEdited();
    void onReset(bool shiftHeld);
    void onClipToggled(bool single);
    void onPreviewToggled(bool on);
    void onVisibilityToggled(bool visible);
    void onDelete();
    void onDocumentChanged();

    ImageController* m_ctrl = nullptr;
    int m_flatIndex = -1;

    solidcolor::SolidColorData m_data;      // authoritative working copy
    bool m_inGesture = false;
    QVariantMap m_gestureBefore;
    bool m_previewOn = true;
    bool m_loading = false;

    // Header
    QLabel* m_iconLabel = nullptr;
    QLabel* m_maskThumb = nullptr;
    QLabel* m_titleLabel = nullptr;

    ColorSwatchButton* m_swatch = nullptr;
    QLineEdit* m_hexEdit = nullptr;
    QSpinBox* m_rSpin = nullptr;
    QSpinBox* m_gSpin = nullptr;
    QSpinBox* m_bSpin = nullptr;

    AdjustmentPanelBar* m_bar = nullptr;
};
