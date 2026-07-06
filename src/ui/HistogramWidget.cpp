#include "HistogramWidget.hpp"

#include "histogram/HistogramRenderer.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <algorithm>

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("histogramWidget"));
    setAttribute(Qt::WA_StyledBackground, false);
    setMouseTracking(true);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() { update(); });
}

void HistogramWidget::setHasDocument(bool has)
{
    if (m_hasDoc == has) return;
    m_hasDoc = has;
    update();
}

void HistogramWidget::setData(const HistogramData& data)
{
    m_data = data;
    update();
}

void HistogramWidget::setChannel(HistogramChannel ch)
{
    if (m_channel == ch) return;
    m_channel = ch;
    update();
}

void HistogramWidget::setStale(bool stale)
{
    if (m_stale == stale) return;
    m_stale = stale;
    update();
}

void HistogramWidget::setWarningReason(const QString& reason)
{
    if (m_warningReason == reason) return;
    m_warningReason = reason;
    setToolTip(reason);
    update();
}

QRectF HistogramWidget::plotRect() const
{
    return QRectF(rect()).adjusted(1, 1, -1, -1);
}

bool HistogramWidget::warningVisible() const
{
    return m_hasDoc && (m_stale ||
                        !m_warningReason.isEmpty() ||
                        (m_data.valid && m_data.sampleLevel != HistogramSampleLevel::Full));
}

void HistogramWidget::paintEvent(QPaintEvent*)
{
    auto* t = ThemeManager::instance()->current();
    QPainter p(this);

    const QRectF area = plotRect();
    const QColor bg = t->colorBackgroundPrimary.darker(108);
    const QColor frame = t->colorBorder;

    // Plot background + subtle frame.
    p.fillRect(rect(), bg);
    p.setPen(QPen(frame, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(area.adjusted(0, 0, -1, -1));

    if (!m_hasDoc) {
        p.setPen(t->colorTextSecondary);
        p.drawText(rect(), Qt::AlignCenter, tr("No document"));
        return;
    }

    // Histogram shape.
    HistogramRenderer::Style style;
    style.background = bg;
    style.frame = frame;
    style.singleFill = t->colorTextPrimary;
    style.smooth = true;
    HistogramRenderer::render(p, area.adjusted(1, 1, -1, -1), m_data, m_channel, style);

    // Vertical probe line at the hovered level.
    if (m_hoverLevel >= 0 && m_data.valid) {
        const QRectF inner = area.adjusted(1, 1, -1, -1);
        const double x = inner.left() + (inner.width() * m_hoverLevel) / 255.0;
        QColor line = t->colorTextSecondary;
        line.setAlpha(150);
        p.setPen(QPen(line, 1));
        p.drawLine(QPointF(x, inner.top()), QPointF(x, inner.bottom()));
    }

    // Warning glyph (top-right): cached / downsampled / stale result.
    if (warningVisible()) {
        const qreal s = 11.0;
        const QPointF tip(area.right() - s - 3, area.top() + 4);
        QPainterPath tri;
        tri.moveTo(tip.x() + s * 0.5, tip.y());
        tri.lineTo(tip.x() + s, tip.y() + s);
        tri.lineTo(tip.x(), tip.y() + s);
        tri.closeSubpath();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillPath(tri, t->colorWarning);
        p.setPen(QPen(t->colorWarning.darker(160), 1));
        p.drawPath(tri);
        // Exclamation mark.
        p.setPen(QPen(QColor(30, 25, 0), 1.2));
        const qreal cx = tip.x() + s * 0.5;
        p.drawLine(QPointF(cx, tip.y() + s * 0.32), QPointF(cx, tip.y() + s * 0.66));
        p.drawPoint(QPointF(cx, tip.y() + s * 0.80));
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_hasDoc || !m_data.valid) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    const QRectF inner = plotRect().adjusted(1, 1, -1, -1);
    int level = -1;
    if (inner.width() > 0 && e->position().x() >= inner.left() &&
        e->position().x() <= inner.right()) {
        const double frac = (e->position().x() - inner.left()) / inner.width();
        level = std::clamp(static_cast<int>(frac * 255.0 + 0.5), 0, 255);
    }
    if (level != m_hoverLevel) {
        m_hoverLevel = level;
        emit levelHovered(level);
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void HistogramWidget::leaveEvent(QEvent* e)
{
    if (m_hoverLevel != -1) {
        m_hoverLevel = -1;
        emit levelHovered(-1);
        update();
    }
    QWidget::leaveEvent(e);
}
