#pragma once

#include <QWidget>

class QToolButton;

// Reusable bottom toolbar shared by every Adjustment Layer properties panel
// (Curves, Color Balance, …): Clip to layer, Preview, Reset, Visibility and
// Delete. Extracted from the original Curves editor so the buttons, icons and
// layout stay identical across panels and the editing logic lives in the host
// widget (which reacts to the signals below).
//
// The bar is purely presentational: it emits intent signals and exposes
// setters to reflect state without re-emitting. Hosts own the controller calls.
class AdjustmentPanelBar : public QWidget {
    Q_OBJECT
public:
    explicit AdjustmentPanelBar(QWidget* parent = nullptr);

    // Reflect external state without emitting the matching signal.
    void setClipChecked(bool single);
    void setPreviewChecked(bool on);
    void setVisibilityChecked(bool visible);

    bool isClipChecked() const;
    bool isPreviewChecked() const;
    bool isVisibilityChecked() const;

signals:
    void clipToggled(bool single);       // Single Layer Mode on/off
    void previewToggled(bool on);         // live bypass off/on
    void resetClicked(bool shiftHeld);    // Shift = reset all (host decides)
    void visibilityToggled(bool visible);
    void deleteClicked();

private:
    QToolButton* m_clipBtn = nullptr;
    QToolButton* m_previewBtn = nullptr;
    QToolButton* m_resetBtn = nullptr;
    QToolButton* m_visBtn = nullptr;
    QToolButton* m_deleteBtn = nullptr;
};
