#include "RawPreviewView.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QCursor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

// Same geometry as the main viewport's pasteboard shader (GPUViewport::
// renderPasteboardDots): a fixed screen-space grid, so the dots stay put
// while the frame pans and zooms under them.
constexpr int kDotSpacing = 20;
constexpr qreal kDotRadius = 1.4;
// How close to the split divider the cursor has to be to grab it.
constexpr int kDividerGrab = 6;

} // namespace

RawPreviewView::RawPreviewView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    rebuildDotTile();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        rebuildDotTile();
        update();
    });
}

void RawPreviewView::rebuildDotTile()
{
    const auto* theme = ThemeManager::instance()->current();
    if (!theme)
        return;

    const QColor background = theme->colorSurfaceDark;
    // The pasteboard colour itself is unchanged; the dots take the same
    // lift the themes define between canvasBackground and canvasDotColor, so
    // the pattern reads the same here as it does around the main canvas.
    const int lift = theme->canvasDotColor.green() - theme->canvasBackground.green();
    const QColor dot(std::clamp(background.red() + lift, 0, 255),
                     std::clamp(background.green() + lift, 0, 255),
                     std::clamp(background.blue() + lift, 0, 255));

    const qreal ratio = devicePixelRatioF();
    QPixmap tile(QSize(qRound(kDotSpacing * ratio), qRound(kDotSpacing * ratio)));
    tile.setDevicePixelRatio(ratio);
    tile.fill(background);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(dot);
    painter.drawEllipse(QPointF(kDotSpacing / 2.0, kDotSpacing / 2.0), kDotRadius, kDotRadius);
    m_dotTile = tile;
}

void RawPreviewView::setContentSize(const QSize& contentSize)
{
    const QSize previous = m_contentSize;
    if (previous == contentSize)
        return;
    m_contentSize = contentSize;
    if (!previous.isEmpty() && !m_contentSize.isEmpty()) {
        // A new frame size means the zoom changed. Keep whatever was at the
        // centre of the viewport there, instead of jumping back to the
        // top-left the way a raw pan value would.
        const QPointF center = m_pan + QPointF(width() / 2.0, height() / 2.0);
        m_pan = QPointF(center.x() * m_contentSize.width() / double(previous.width()),
                        center.y() * m_contentSize.height() / double(previous.height()))
              - QPointF(width() / 2.0, height() / 2.0);
    }
    clampPan();
    // No repaint here: the caller is on its way to setTile() with a tile for
    // the offset this just settled on, and painting the old one against the
    // new pan is exactly the mismatch this split avoids.
}

void RawPreviewView::setTile(const QPoint& tileOrigin, const QPixmap& tile,
                             const QPixmap& splitTile)
{
    m_tileOrigin = tileOrigin;
    m_tile = tile;
    m_splitTile = splitTile;
    updateCursor(mapFromGlobal(QCursor::pos()));
    update();
}

QPoint RawPreviewView::scrollOffset() const
{
    const QPoint origin = contentOrigin();
    return QPoint(std::max(0, -origin.x()), std::max(0, -origin.y()));
}

void RawPreviewView::setSplitEnabled(bool enabled)
{
    if (m_split == enabled)
        return;
    m_split = enabled;
    update();
}

void RawPreviewView::setPlaceholderText(const QString& text)
{
    m_placeholder = text;
    update();
}

void RawPreviewView::showNotice(const QString& text, const QColor& accent)
{
    m_noticeText = text;
    m_noticeAccent = accent;
    update();
}

void RawPreviewView::clearNotice()
{
    if (m_noticeText.isEmpty())
        return;
    m_noticeText.clear();
    update();
}

QPoint RawPreviewView::contentOrigin() const
{
    const QSize content = m_contentSize;
    const int x = content.width() <= width()
        ? (width() - content.width()) / 2
        : -qRound(m_pan.x());
    const int y = content.height() <= height()
        ? (height() - content.height()) / 2
        : -qRound(m_pan.y());
    return QPoint(x, y);
}

void RawPreviewView::clampPan()
{
    const QSize content = m_contentSize;
    const double maxX = std::max(0.0, double(content.width() - width()));
    const double maxY = std::max(0.0, double(content.height() - height()));
    m_pan = QPointF(std::clamp(m_pan.x(), 0.0, maxX), std::clamp(m_pan.y(), 0.0, maxY));
}

void RawPreviewView::applyPan(const QPointF& pan)
{
    const QPointF previous = m_pan;
    m_pan = pan;
    clampPan();
    if (m_pan == previous)
        return;
    update();
    emit scrolled();
}

int RawPreviewView::dividerX() const
{
    return qRound(width() * m_splitRatio);
}

bool RawPreviewView::overDivider(const QPoint& position) const
{
    return m_split && !m_splitTile.isNull()
        && std::abs(position.x() - dividerX()) <= kDividerGrab;
}

bool RawPreviewView::pannable() const
{
    return m_contentSize.width() > width() || m_contentSize.height() > height();
}

void RawPreviewView::updateCursor(const QPoint& position)
{
    if (m_draggingDivider || overDivider(position)) {
        setCursor(Qt::SplitHCursor);
    } else if (m_draggingPan) {
        setCursor(Qt::ClosedHandCursor);
    } else if (pannable()) {
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }
}

QRect RawPreviewView::noticeRect(const QFontMetrics& metrics) const
{
    const auto* theme = ThemeManager::instance()->current();
    const int margin = theme->spaceMD;
    const int padding = theme->spaceSM;
    const int accent = 3;
    const int textWidth = std::min(metrics.horizontalAdvance(m_noticeText),
                                   width() - 2 * margin - 2 * padding - accent);
    return QRect(margin, margin,
                 textWidth + 2 * padding + accent + theme->spaceXS,
                 metrics.height() + 2 * padding);
}

void RawPreviewView::paintEvent(QPaintEvent*)
{
    const auto* theme = ThemeManager::instance()->current();
    QPainter painter(this);
    painter.fillRect(rect(), QBrush(m_dotTile));

    if (m_tile.isNull()) {
        painter.setPen(theme->colorTextSecondary);
        painter.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }

    // Where the composed tile lands: the frame's own origin (centred, or
    // offset by the pan) plus the tile's position inside the frame.
    const QPoint origin = contentOrigin() + m_tileOrigin;
    const bool split = m_split && !m_splitTile.isNull()
        && m_splitTile.size() == m_tile.size();
    if (!split) {
        painter.drawPixmap(origin, m_tile);
    } else {
        // The divider is a screen-space position, not an image one: it stays
        // where the user put it while the frame pans and zooms behind it.
        const int divider = dividerX();
        painter.save();
        painter.setClipRect(QRect(0, 0, divider, height()));
        painter.drawPixmap(origin, m_splitTile);
        painter.restore();
        painter.save();
        painter.setClipRect(QRect(divider, 0, width() - divider, height()));
        painter.drawPixmap(origin, m_tile);
        painter.restore();

        painter.setPen(QPen(theme->colorBorderLight, 1));
        painter.drawLine(divider, 0, divider, height());
        const int grip = theme->spaceLG;
        QRect handle(divider - grip / 2, height() / 2 - grip, grip, 2 * grip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme->colorBorderLight);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawRoundedRect(handle, theme->radiusSM, theme->radiusSM);
        painter.setRenderHint(QPainter::Antialiasing, false);
    }

    if (!m_noticeText.isEmpty()) {
        const QFontMetrics metrics(font());
        const QRect box = noticeRect(metrics);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor backdrop = theme->colorSurface;
        backdrop.setAlpha(235);
        painter.setPen(QPen(theme->colorBorder, theme->borderThinWidth));
        painter.setBrush(backdrop);
        painter.drawRoundedRect(box, theme->radiusMD, theme->radiusMD);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_noticeAccent);
        painter.drawRect(QRect(box.left() + 1, box.top() + 2, 3, box.height() - 4));
        painter.setPen(theme->colorTextPrimary);
        const QRect textRect = box.adjusted(3 + theme->spaceSM, 0, -theme->spaceSM, 0);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         metrics.elidedText(m_noticeText, Qt::ElideRight, textRect.width()));
    }
}

void RawPreviewView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    clampPan();
    emit scrolled();
}

void RawPreviewView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint position = event->position().toPoint();
    if (event->button() == Qt::LeftButton && overDivider(position)) {
        m_draggingDivider = true;
    } else if (pannable()) {
        m_draggingPan = true;
        m_dragOrigin = position;
        m_dragPanOrigin = m_pan;
    }
    updateCursor(position);
    event->accept();
}

void RawPreviewView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    if (m_draggingDivider) {
        const double ratio = std::clamp(position.x() / double(std::max(1, width())), 0.02, 0.98);
        if (!qFuzzyCompare(ratio, m_splitRatio)) {
            m_splitRatio = ratio;
            update();
        }
        event->accept();
        return;
    }
    if (m_draggingPan) {
        applyPan(m_dragPanOrigin - QPointF(position - m_dragOrigin));
        event->accept();
        return;
    }
    updateCursor(position);
    QWidget::mouseMoveEvent(event);
}

void RawPreviewView::mouseReleaseEvent(QMouseEvent* event)
{
    m_draggingDivider = false;
    m_draggingPan = false;
    updateCursor(event->position().toPoint());
    QWidget::mouseReleaseEvent(event);
}

void RawPreviewView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int notches = event->angleDelta().y() / 120;
        if (notches != 0)
            emit zoomStepRequested(notches);
        event->accept();
        return;
    }
    if (!pannable()) {
        QWidget::wheelEvent(event);
        return;
    }
    const QPoint pixels = event->pixelDelta();
    const QPoint degrees = event->angleDelta();
    QPointF delta = pixels.isNull()
        ? QPointF(degrees.x() / 120.0 * kDotSpacing * 2, degrees.y() / 120.0 * kDotSpacing * 2)
        : QPointF(pixels);
    applyPan(m_pan - delta);
    event->accept();
}

void RawPreviewView::leaveEvent(QEvent* event)
{
    if (!m_draggingPan && !m_draggingDivider)
        unsetCursor();
    QWidget::leaveEvent(event);
}
