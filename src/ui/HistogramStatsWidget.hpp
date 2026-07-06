#pragma once

#include <QWidget>

class QLabel;

// Compact two-column statistics block beneath the histogram graph.
//   Left:  Mean / Std Dev / Median / Pixels
//   Right: Level / Count / Percentile / Cache Level
class HistogramStatsWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramStatsWidget(QWidget* parent = nullptr);

    void setStats(float mean, float stdDev, float median, int pixels);
    // Probe values for the hovered level; level < 0 clears them.
    void setProbe(int level, int count, double percentile);
    void setCacheLevel(const QString& text);
    void clear();

private:
    QLabel* makeValue(QLabel** out);
    void applyLabelStyles();

    QLabel* m_meanVal = nullptr;
    QLabel* m_stdDevVal = nullptr;
    QLabel* m_medianVal = nullptr;
    QLabel* m_pixelsVal = nullptr;

    QLabel* m_levelVal = nullptr;
    QLabel* m_countVal = nullptr;
    QLabel* m_percentileVal = nullptr;
    QLabel* m_cacheLevelVal = nullptr;
};
