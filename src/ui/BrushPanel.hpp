#pragma once

#include <QWidget>
#include "brush/BrushTypes.hpp"
#include "brush/BrushPreset.hpp"

class BrushPresetManager;
class BrushLibraryListView;
class BrushTipPreview;
class QLabel;
class QSlider;

// Brush browser panel:
//   1. Brush quick settings — interactive tip preview (drag = size/hardness).
//   2. Preset library — the shared BrushLibraryListView (search + folder tree),
//      reused as-is here and in the Brush Settings panel.
class BrushPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrushPanel(QWidget* parent = nullptr);
    ~BrushPanel() override;

    void setPresetManager(BrushPresetManager* manager);

    // Pre-render every preset's thumbnails into the cache on a worker thread so
    // they are ready before the user scrolls/opens the panel. Safe to call
    // repeatedly (no-op while a warm is already running).
    void warmCacheAsync();

    // External sync (e.g. toolbar sliders / a preset chosen elsewhere).
    void setBrush(const BrushSettings& settings);
    void setSize(float size);
    void setHardness(float hardness);
    void setCurrentPreset(const QString& name);
    void revealPreset(const QString& name);

signals:
    void presetSelected(const BrushPreset& preset);
    void brushSizeChanged(float size);
    void brushHardnessChanged(float hardness);

    // Raised by the panel's header menu ("Import Brushes…") and by dropping
    // brush files onto the panel. The host opens the import dialog.
    void importBrushesRequested();
    void brushFilesDropped(const QStringList& paths);
    void openInSettingsRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void applyTheme();
    // Single update path for size/hardness: syncs the tip preview, the slider
    // and its value label, and emits only when the change is user-initiated.
    void applySize(float size, bool emitSignal);
    void applyHardness(float hardness, bool emitSignal);

    BrushLibraryListView* m_list = nullptr;

    BrushTipPreview* m_tipPreview = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QLabel* m_sizeValue = nullptr;
    QSlider* m_hardnessSlider = nullptr;
    QLabel* m_hardnessValue = nullptr;

    static constexpr int kSizeSliderMax = 1000; // matches BrushTipPreview/toolbar
};
