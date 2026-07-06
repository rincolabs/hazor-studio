#pragma once

#include <QWidget>
#include <QList>
#include <cstdint>

#include "histogram/HistogramTypes.hpp"

class QComboBox;
class QToolButton;
class QTimer;
class ImageController;
class HistogramWidget;
class HistogramStatsWidget;
class HistogramCacheManager;

// Dockable histogram panel. Owns a HistogramCacheManager and
// drives it from the active document's composition revision. Decoupled from any
// single document — MainWindow re-points it via setController() on tab change.
class HistogramPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistogramPanel(QWidget* parent = nullptr);

    // (Re)binds the panel to a document controller; nullptr clears it.
    void setController(ImageController* ctrl);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void buildUi();
    void applyTheme();
    void scheduleUpdate();
    void recompute(bool force);
    void onCacheReady();
    void onComputingChanged(bool computing);
    void onChannelChanged(int index);
    void onLevelHovered(int level);
    void updateStatsFromData();
    void updateProbe();
    void updateWarning();

    static HistogramChannel channelForIndex(int index);
    static QString sampleLevelText(HistogramSampleLevel level);

    ImageController* m_ctrl = nullptr;
    QList<QMetaObject::Connection> m_conns;

    QComboBox*            m_channelCombo = nullptr;
    QToolButton*          m_refreshBtn = nullptr;
    HistogramWidget*      m_graph = nullptr;
    HistogramStatsWidget* m_stats = nullptr;
    HistogramCacheManager* m_cache = nullptr;
    QTimer*               m_debounce = nullptr;

    HistogramChannel m_channel = HistogramChannel::Colors;
    bool     m_dirty = true;
    int      m_hoverLevel = -1;
    uint64_t m_lastGeneration = ~0ull;
    // True while an adjustment layer is being live-dragged: the histogram
    // recompute (a full CPU compositeImage) must not fire mid-gesture — pausing
    // the mouse would otherwise let the debounce trigger it and stall the UI.
    bool     m_liveEditActive = false;
};
