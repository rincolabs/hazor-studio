#pragma once

#include <QWidget>

#include "histogram/HistogramTypes.hpp"

// The dark plot area. Renders the histogram for the active channel, handles the
// "No document" / zeroed states, shows a warning glyph when the result is not
// full-precision (downsampled or stale), and reports the level under the cursor.
class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    void setHasDocument(bool has);
    void setData(const HistogramData& data);
    void setChannel(HistogramChannel ch);
    void setStale(bool stale);
    void setWarningReason(const QString& reason); // empty => no warning

    QSize sizeHint() const override { return QSize(220, 130); }
    QSize minimumSizeHint() const override { return QSize(160, 96); }

signals:
    // level == -1 when the cursor leaves the plot.
    void levelHovered(int level);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QRectF plotRect() const;
    bool warningVisible() const;

    HistogramData    m_data;
    HistogramChannel m_channel = HistogramChannel::Colors;
    bool             m_hasDoc = false;
    bool             m_stale = false;
    QString          m_warningReason;
    int              m_hoverLevel = -1;
};
