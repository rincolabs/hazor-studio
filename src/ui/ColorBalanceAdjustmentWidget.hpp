#pragma once

#include <QWidget>

#include "core/ColorBalanceData.hpp"

#include <array>

class QComboBox;
class QLabel;
class QSpinBox;
class GradientSlider;
class ImageController;
class AdjustmentPanelBar;
class AppCheckBox;

// ── Color Balance adjustment editor ──────────────────────────────────────────
// Interactive editor for a Color Balance adjustment layer. Hosted by
// PropertiesPanel (and reusable in a standalone dialog later — it depends only
// on ImageController + a flat index). Reads/writes the active adjustment node's
// params through the controller: live, undo-less updates while dragging and a
// single AdjustmentParamsCommand per committed gesture. Mirrors
// CurvesEditorWidget's controller contract and shares the AdjustmentPanelBar.
class ColorBalanceAdjustmentWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorBalanceAdjustmentWidget(QWidget* parent = nullptr);

    void setController(ImageController* ctrl);

    // Binds the editor to the colorbalance adjustment at `flatIndex` and reloads
    // its state. Pass -1 to detach.
    void showNode(int flatIndex);
    int currentFlatIndex() const { return m_flatIndex; }

protected:
    void hideEvent(QHideEvent* e) override;

private:
    void buildUi();
    void reloadFromNode();          // working data + UI from the bound node
    void refreshHeader();
    void reloadControlsForTone();   // sliders + spins ← current tone's values

    colorbalance::Tone currentTone() const;

    // Param plumbing (same contract as CurvesEditorWidget).
    QVariantMap paramsFromData() const;
    void applyLive();
    void beginGesture();
    void commitGesture(const QString& label);
    void applyAndCommit(const QString& label);

    // Callbacks.
    void onToneActivated(int index);
    void onSliderChanged(int axis, int value);
    void onSliderReleased(int axis);
    void onSpinEdited(int axis, int value);
    void onPreserveLumToggled(bool on);
    void onReset(bool shiftHeld);
    void onClipToggled(bool single);
    void onPreviewToggled(bool on);
    void onVisibilityToggled(bool visible);
    void onDelete();
    void onDocumentChanged();

    ImageController* m_ctrl = nullptr;
    int m_flatIndex = -1;

    colorbalance::ColorBalanceData m_data;   // authoritative working copy
    bool m_inGesture = false;
    QVariantMap m_gestureBefore;
    bool m_previewOn = true;
    bool m_loading = false;

    // Header
    QLabel* m_iconLabel = nullptr;
    QLabel* m_maskThumb = nullptr;
    QLabel* m_titleLabel = nullptr;

    QComboBox* m_toneCombo = nullptr;
    std::array<GradientSlider*, colorbalance::kAxisCount> m_sliders{};
    AppCheckBox* m_preserveLumCheck = nullptr;

    AdjustmentPanelBar* m_bar = nullptr;
};
