#pragma once

#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

class QTimer;

// The Hazor Develop viewport: the dotted pasteboard, the composed frame, the
// Before/After split and drag panning, in one widget.
//
// It deliberately does not scale anything itself — RawDevelopWindow composes
// the frame (clipping markers, the engine's 1:1 slice) and hands over finished
// pixmaps, so there is exactly one place where preview pixels are produced.
// This widget only places them, and reports where the viewport sits inside
// them so the window can ask the engine for the matching detail slice.
class RawPreviewView : public QWidget {
    Q_OBJECT

public:
    explicit RawPreviewView(QWidget* parent = nullptr);

    // The window composes only what is on screen, in two steps, because the
    // pan has to settle *before* the visible rectangle is known: first the
    // new frame size (which re-anchors and clamps the pan), then the tile
    // for the content rectangle scrollOffset() reports. Composing against
    // the old pan and re-anchoring afterwards would leave the tile off the
    // viewport — visibly, as bare pasteboard where the frame should be.
    void setContentSize(const QSize& contentSize);
    // `tile` covers the content rectangle that starts at `tileOrigin`;
    // `splitTile` is the comparison frame for the same rectangle, used only
    // while split view is on.
    void setTile(const QPoint& tileOrigin, const QPixmap& tile,
                 const QPixmap& splitTile = QPixmap());
    QSize contentSize() const { return m_contentSize; }

    // Top-left of the viewport in content coordinates — 0 on an axis where
    // the frame is smaller than the viewport and therefore centered.
    QPoint scrollOffset() const;

    void setSplitEnabled(bool enabled);
    bool splitEnabled() const { return m_split; }
    double splitRatio() const { return m_splitRatio; }

    void setPlaceholderText(const QString& text);

    // Transient notice in the top-left corner. Never takes the mouse, and
    // disappears on its own; the status bar keeps the lasting copy.
    void showNotice(const QString& text, const QColor& accent, int milliseconds = 4000);
    void clearNotice();

signals:
    // The visible region moved (drag, wheel, resize or a new frame size).
    void scrolled();
    // Ctrl + wheel: one zoom step per notch, in the owner's own stops.
    void zoomStepRequested(int steps);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void rebuildDotTile();
    QPoint contentOrigin() const;
    void clampPan();
    void applyPan(const QPointF& pan);
    int dividerX() const;
    bool overDivider(const QPoint& position) const;
    bool pannable() const;
    void updateCursor(const QPoint& position);
    QRect noticeRect(const QFontMetrics& metrics) const;

    QSize m_contentSize;
    QPoint m_tileOrigin;
    QPixmap m_tile;
    QPixmap m_splitTile;
    QPixmap m_dotTile;
    QString m_placeholder;

    // Viewport top-left in content pixels, before clamping/centering.
    QPointF m_pan;
    bool m_split = false;
    double m_splitRatio = 0.5;

    bool m_draggingPan = false;
    bool m_draggingDivider = false;
    QPoint m_dragOrigin;
    QPointF m_dragPanOrigin;

    QString m_noticeText;
    QColor m_noticeAccent;
    QTimer* m_noticeTimer = nullptr;
};
