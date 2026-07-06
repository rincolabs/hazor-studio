#include "RulerGuideOverlay.hpp"

#include "core/Document.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QByteArray>
#include <QDebug>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kMinorRulerTickLengthPx = 5.0;
constexpr qreal kRulerThicknessReductionPx = 5.0;

bool snapDebugEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("IMAGE_EDITOR_SNAP_DEBUG").trimmed().toLower();
        return !value.isEmpty() && value != "0" && value != "false" && value != "off";
    }();
    return enabled;
}

qreal niceUnitSpacing(qreal screenPixelsPerUnit)
{
    if (screenPixelsPerUnit <= 0.0)
        return 1.0;

    const qreal targetUnits = std::max<qreal>(0.0001, 100.0 / screenPixelsPerUnit);
    const qreal magnitude = std::pow(10.0, std::floor(std::log10(targetUnits)));
    const qreal normalized = targetUnits / magnitude;
    const qreal candidates[] = {1.0, 2.0, 5.0, 10.0};
    for (qreal candidate : candidates) {
        if (normalized <= candidate)
            return candidate * magnitude;
    }
    return 10.0 * magnitude;
}

qreal documentPixelsPerUnit(RulerUnit unit, qreal dpi, qreal axisLength)
{
    switch (unit) {
    case RulerUnit::Percent:
        return axisLength > 0.0 ? axisLength / 100.0 : 1.0;
    case RulerUnit::Inches:
        return dpi;
    case RulerUnit::Centimeters:
        return dpi / 2.54;
    case RulerUnit::Millimeters:
        return dpi / 25.4;
    case RulerUnit::Pixels:
        return 1.0;
    }
    return 1.0;
}

QString labelSuffixForUnit(RulerUnit unit)
{
    switch (unit) {
    case RulerUnit::Percent:
        return QStringLiteral("%");
    case RulerUnit::Inches:
        return QStringLiteral(" in");
    case RulerUnit::Centimeters:
        return QStringLiteral(" cm");
    case RulerUnit::Millimeters:
        return QStringLiteral(" mm");
    case RulerUnit::Pixels:
        return QStringLiteral(" px");
    }
    return QStringLiteral(" px");
}
}

RulerGuideOverlay::RulerGuideOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    reloadSettings();
}

void RulerGuideOverlay::setDocument(Document* doc)
{
    m_doc = doc;
    update();
}

void RulerGuideOverlay::setCanvasHalfExtents(const QPointF& halfExtents)
{
    m_canvasHalfExtents = halfExtents;
    update();
}

void RulerGuideOverlay::setViewportRect(const QRectF& rect)
{
    m_viewportRect = rect;
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[SnapOverlay][viewport]"
            << "viewport" << m_viewportRect
            << "overlayRect" << this->rect()
            << "geometry" << geometry()
            << "visible" << isVisible();
    }
    update();
}

void RulerGuideOverlay::reloadSettings()
{
    m_settings = RulerGuideSettings::load();
    update();
}

QPointF RulerGuideOverlay::screenToDocument(const QPointF& screenPos) const
{
    const QRectF viewport = m_viewportRect.isValid() ? m_viewportRect : QRectF(rect());
    if (!m_doc || m_doc->size.isEmpty() || viewport.width() <= 0 || viewport.height() <= 0)
        return {};

    const qreal ndcX = 2.0 * (screenPos.x() - viewport.left()) / viewport.width() - 1.0;
    const qreal ndcY = 1.0 - 2.0 * (screenPos.y() - viewport.top()) / viewport.height();
    const qreal docNdcX = (ndcX - m_doc->panOffset.x()) / std::max<qreal>(m_doc->zoom, 0.0001);
    const qreal docNdcY = (ndcY - m_doc->panOffset.y()) / std::max<qreal>(m_doc->zoom, 0.0001);
    const qreal canvasX = docNdcX / std::max<qreal>(m_canvasHalfExtents.x(), 0.0001);
    const qreal canvasY = docNdcY / std::max<qreal>(m_canvasHalfExtents.y(), 0.0001);

    return QPointF((canvasX + 1.0) * 0.5 * m_doc->size.width(),
                   (1.0 - canvasY) * 0.5 * m_doc->size.height());
}

QPointF RulerGuideOverlay::documentToScreen(const QPointF& docPos) const
{
    const QRectF viewport = m_viewportRect.isValid() ? m_viewportRect : QRectF(rect());
    if (!m_doc || m_doc->size.isEmpty() || viewport.width() <= 0 || viewport.height() <= 0)
        return {};

    const qreal canvasX = docPos.x() / m_doc->size.width() * 2.0 - 1.0;
    const qreal canvasY = 1.0 - docPos.y() / m_doc->size.height() * 2.0;
    const qreal ndcX = m_doc->panOffset.x() + m_doc->zoom * m_canvasHalfExtents.x() * canvasX;
    const qreal ndcY = m_doc->panOffset.y() + m_doc->zoom * m_canvasHalfExtents.y() * canvasY;

    return QPointF(viewport.left() + (ndcX + 1.0) * 0.5 * viewport.width(),
                   viewport.top() + (1.0 - ndcY) * 0.5 * viewport.height());
}

QRectF RulerGuideOverlay::canvasScreenRect() const
{
    if (!m_doc || m_doc->size.isEmpty())
        return {};
    return QRectF(documentToScreen(QPointF(0.0, 0.0)),
                  documentToScreen(QPointF(m_doc->size.width(), m_doc->size.height()))).normalized();
}

qreal RulerGuideOverlay::effectiveRulerSize() const
{
    return std::max<qreal>(1.0, m_settings.rulers.rulerSize - kRulerThicknessReductionPx);
}

QRectF RulerGuideOverlay::horizontalRulerRect() const
{
    if (!m_settings.rulers.showRulers)
        return {};
    const qreal size = effectiveRulerSize();
    const QRectF viewport = m_viewportRect.isValid() ? m_viewportRect : QRectF(rect());
    return QRectF(viewport.left(), 0.0, viewport.width(), size);
}

QRectF RulerGuideOverlay::verticalRulerRect() const
{
    if (!m_settings.rulers.showRulers)
        return {};
    const qreal size = effectiveRulerSize();
    const QRectF viewport = m_viewportRect.isValid() ? m_viewportRect : QRectF(rect());
    return QRectF(0.0, viewport.top(), size, viewport.height());
}

bool RulerGuideOverlay::isPointInCanvas(const QPointF& screenPos) const
{
    const QRectF viewport = m_viewportRect.isValid() ? m_viewportRect : QRectF(rect());
    return viewport.contains(screenPos);
}

bool RulerGuideOverlay::hitHorizontalRuler(const QPointF& screenPos) const
{
    return horizontalRulerRect().contains(screenPos);
}

bool RulerGuideOverlay::hitVerticalRuler(const QPointF& screenPos) const
{
    return verticalRulerRect().contains(screenPos);
}

int RulerGuideOverlay::hitGuide(const QPointF& screenPos, qreal toleranceScreenPx) const
{
    if (!m_doc || !m_settings.guides.showGuides)
        return -1;

    const QRectF viewport = (m_viewportRect.isValid() ? m_viewportRect : QRectF(rect()))
        .adjusted(-toleranceScreenPx, -toleranceScreenPx,
                  toleranceScreenPx, toleranceScreenPx);
    if (!viewport.contains(screenPos))
        return -1;

    const auto& guides = m_doc->guideManager.guides();
    for (int i = static_cast<int>(guides.size()) - 1; i >= 0; --i) {
        const Guide& guide = guides[static_cast<size_t>(i)];
        if (guide.orientation == GuideOrientation::Vertical) {
            const qreal x = documentToScreen(QPointF(guide.position, 0.0)).x();
            if (std::abs(screenPos.x() - x) <= toleranceScreenPx)
                return i;
        } else {
            const qreal y = documentToScreen(QPointF(0.0, guide.position)).y();
            if (std::abs(screenPos.y() - y) <= toleranceScreenPx)
                return i;
        }
    }
    return -1;
}

void RulerGuideOverlay::setMouseDocumentPos(const QPointF& pos, bool visible)
{
    if (visible)
        m_mouseDocPos = pos;
    else
        m_mouseDocPos.reset();
    update();
}

void RulerGuideOverlay::clearMouseDocumentPos()
{
    m_mouseDocPos.reset();
    update();
}

void RulerGuideOverlay::setGuidePreview(GuideOrientation orientation, qreal position, bool visible)
{
    m_previewOrientation = orientation;
    m_previewPosition = position;
    m_previewVisible = visible;
    update();
}

void RulerGuideOverlay::clearGuidePreview()
{
    m_previewVisible = false;
    update();
}

void RulerGuideOverlay::setSelectedGuideIndex(int index)
{
    if (m_selectedGuideIndex == index)
        return;
    m_selectedGuideIndex = index;
    update();
}

void RulerGuideOverlay::setSnapIndicators(const QVector<SnapIndicator>& indicators)
{
    m_snapIndicators = indicators;
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[SnapOverlay][set-indicators]"
            << "count" << m_snapIndicators.size()
            << "showIndicators" << m_settings.snap.showIndicators
            << "overlayRect" << rect()
            << "viewport" << m_viewportRect
            << "visible" << isVisible()
            << "updatesEnabled" << updatesEnabled();
        for (const SnapIndicator& indicator : m_snapIndicators) {
            qInfo().noquote()
                << "[SnapOverlay][indicator]"
                << (indicator.axis == SnapAxis::X ? "X" : "Y")
                << "docPos" << indicator.position
                << "screenPos" << documentToScreen(indicator.axis == SnapAxis::X
                    ? QPointF(indicator.position, 0.0)
                    : QPointF(0.0, indicator.position))
                << "label" << indicator.label;
        }
    }
    update();
}

void RulerGuideOverlay::clearSnapIndicators()
{
    if (m_snapIndicators.isEmpty())
        return;
    if (snapDebugEnabled())
        qInfo().noquote() << "[SnapOverlay][clear-indicators]" << "count" << m_snapIndicators.size();
    m_snapIndicators.clear();
    update();
}

qreal RulerGuideOverlay::screenPixelsPerDocumentX() const
{
    if (!m_doc || m_doc->size.width() <= 0)
        return 1.0;
    const QPointF a = documentToScreen(QPointF(0.0, 0.0));
    const QPointF b = documentToScreen(QPointF(1.0, 0.0));
    return std::max<qreal>(0.0001, std::abs(b.x() - a.x()));
}

qreal RulerGuideOverlay::screenPixelsPerDocumentY() const
{
    if (!m_doc || m_doc->size.height() <= 0)
        return 1.0;
    const QPointF a = documentToScreen(QPointF(0.0, 0.0));
    const QPointF b = documentToScreen(QPointF(0.0, 1.0));
    return std::max<qreal>(0.0001, std::abs(b.y() - a.y()));
}

qreal RulerGuideOverlay::adaptiveMajorSpacing(qreal pixelsPerDocument, qreal configuredSpacing) const
{
    qreal spacing = std::max<qreal>(1.0, configuredSpacing);
    while (spacing * pixelsPerDocument < 64.0)
        spacing *= 2.0;
    while (spacing > 1.0 && spacing * pixelsPerDocument > 180.0)
        spacing *= 0.5;
    return std::max<qreal>(1.0, spacing);
}

QString RulerGuideOverlay::labelForPosition(qreal pos, qreal axisLength) const
{
    const qreal dpi = m_doc ? std::max<qreal>(1.0, m_doc->resolutionDpi) : 300.0;
    const qreal unitSize = documentPixelsPerUnit(m_settings.rulers.unit, dpi, axisLength);
    const qreal value = unitSize > 0.0 ? pos / unitSize : pos;
    const qreal absValue = std::abs(value);
    int decimals = 0;
    if (m_settings.rulers.unit == RulerUnit::Inches)
        decimals = absValue < 10.0 ? 2 : 1;
    else if (m_settings.rulers.unit == RulerUnit::Centimeters)
        decimals = absValue < 10.0 ? 1 : 0;
    else if (m_settings.rulers.unit == RulerUnit::Millimeters)
        decimals = absValue < 10.0 ? 1 : 0;
    else if (m_settings.rulers.unit == RulerUnit::Percent)
        decimals = absValue < 10.0 ? 1 : 0;
    return QStringLiteral("%1%2").arg(value, 0, 'f', decimals)
        .arg(labelSuffixForUnit(m_settings.rulers.unit));
}

void RulerGuideOverlay::paintEvent(QPaintEvent*)
{
    if (!m_doc || m_doc->size.isEmpty())
        return;

    if (snapDebugEnabled() && !m_snapIndicators.isEmpty()) {
        qInfo().noquote()
            << "[SnapOverlay][paint]"
            << "indicatorCount" << m_snapIndicators.size()
            << "overlayRect" << rect()
            << "viewport" << m_viewportRect
            << "visible" << isVisible()
            << "showIndicators" << m_settings.snap.showIndicators;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    drawGuides(p);
    drawRulers(p);
}

void RulerGuideOverlay::drawGuides(QPainter& p)
{
    if (!m_doc)
        return;

    const auto* th = ThemeManager::instance()->current();
    const QRectF viewport = (m_viewportRect.isValid() ? m_viewportRect : QRectF(rect())).intersected(rect());
    if (viewport.isEmpty()) {
        if (snapDebugEnabled() && !m_snapIndicators.isEmpty()) {
            qInfo().noquote()
                << "[SnapOverlay][draw] skipped: empty viewport"
                << "rawViewport" << m_viewportRect
                << "overlayRect" << rect();
        }
        return;
    }

    if (m_settings.guides.showGuides) {
        QColor color = m_settings.guides.guideColor;
        color.setAlphaF(std::clamp(m_settings.guides.guideOpacity, 0.05, 1.0));
        QColor selected = th->colorAccentHover;
        selected.setAlphaF(0.95);

        const auto& guides = m_doc->guideManager.guides();
        for (int i = 0; i < static_cast<int>(guides.size()); ++i) {
            const Guide& guide = guides[static_cast<size_t>(i)];
            QPen pen(i == m_selectedGuideIndex ? selected : color, i == m_selectedGuideIndex ? 1.8 : 1.0);
            pen.setCosmetic(true);
            p.setPen(pen);
            if (guide.orientation == GuideOrientation::Vertical) {
                const qreal x = documentToScreen(QPointF(guide.position, 0.0)).x();
                p.drawLine(QPointF(x, viewport.top()), QPointF(x, viewport.bottom()));
            } else {
                const qreal y = documentToScreen(QPointF(0.0, guide.position)).y();
                p.drawLine(QPointF(viewport.left(), y), QPointF(viewport.right(), y));
            }
        }
    }

    if (m_previewVisible) {
        QColor preview = th->colorAccent;
        preview.setAlpha(220);
        QPen pen(preview, 1.4);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);

        QPointF labelPos;
        if (m_previewOrientation == GuideOrientation::Vertical) {
            const qreal x = documentToScreen(QPointF(m_previewPosition, 0.0)).x();
            p.drawLine(QPointF(x, viewport.top()), QPointF(x, viewport.bottom()));
            labelPos = QPointF(x + 8.0, viewport.top() + 8.0);
        } else {
            const qreal y = documentToScreen(QPointF(0.0, m_previewPosition)).y();
            p.drawLine(QPointF(viewport.left(), y), QPointF(viewport.right(), y));
            labelPos = QPointF(viewport.left() + 8.0, y + 8.0);
        }

        const qreal axisLength = m_previewOrientation == GuideOrientation::Vertical
            ? m_doc->size.width() : m_doc->size.height();
        const QString label = labelForPosition(m_previewPosition, axisLength);
        QFont font = p.font();
        font.setPixelSize(th->fontSizeXS + 1);
        p.setFont(font);
        const QFontMetrics fm(font);
        QRectF bubble(labelPos, QSizeF(fm.horizontalAdvance(label) + 12, fm.height() + 6));
        bubble.moveLeft(std::clamp(bubble.left(), 2.0, std::max(2.0, width() - bubble.width() - 2.0)));
        bubble.moveTop(std::clamp(bubble.top(), 2.0, std::max(2.0, height() - bubble.height() - 2.0)));
        QColor bg = th->colorBackgroundPrimary;
        bg.setAlpha(220);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(bubble, th->radiusSM, th->radiusSM);
        p.setPen(th->colorTextBright);
        p.drawText(bubble, Qt::AlignCenter, label);
    }

    if (m_settings.snap.showIndicators && !m_snapIndicators.isEmpty()) {
        QColor color = th->colorAccentHover;
        color.setAlpha(235);
        QPen pen(color, 1.6);
        pen.setCosmetic(true);
        p.setPen(pen);
        for (const SnapIndicator& indicator : m_snapIndicators) {
            if (indicator.axis == SnapAxis::X) {
                const qreal x = documentToScreen(QPointF(indicator.position, 0.0)).x();
                if (snapDebugEnabled()) {
                    qInfo().noquote()
                        << "[SnapOverlay][draw-indicator]"
                        << "axis X"
                        << "docPos" << indicator.position
                        << "screenX" << x
                        << "viewport" << viewport
                        << "label" << indicator.label;
                }
                p.drawLine(QPointF(x, viewport.top()), QPointF(x, viewport.bottom()));
            } else {
                const qreal y = documentToScreen(QPointF(0.0, indicator.position)).y();
                if (snapDebugEnabled()) {
                    qInfo().noquote()
                        << "[SnapOverlay][draw-indicator]"
                        << "axis Y"
                        << "docPos" << indicator.position
                        << "screenY" << y
                        << "viewport" << viewport
                        << "label" << indicator.label;
                }
                p.drawLine(QPointF(viewport.left(), y), QPointF(viewport.right(), y));
            }
        }
    } else if (snapDebugEnabled() && !m_snapIndicators.isEmpty()) {
        qInfo().noquote()
            << "[SnapOverlay][draw] indicators present but hidden by settings"
            << "showIndicators" << m_settings.snap.showIndicators
            << "count" << m_snapIndicators.size();
    }
}

void RulerGuideOverlay::drawRulers(QPainter& p)
{
    if (!m_settings.rulers.showRulers)
        return;

    const auto* th = ThemeManager::instance()->current();
    const QRectF hRect = horizontalRulerRect().intersected(rect());
    const QRectF vRect = verticalRulerRect().intersected(rect());

    QColor bg = th->colorBackgroundPrimary;
    bg.setAlpha(235);
    QColor border = th->colorBorder;
    border.setAlpha(220);

    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    if (!hRect.isEmpty())
        p.drawRect(hRect);
    if (!vRect.isEmpty())
        p.drawRect(vRect);

    const qreal rulerSize = effectiveRulerSize();
    const QRectF corner(0.0, 0.0, rulerSize, rulerSize);
    if (corner.intersects(rect()))
        p.drawRect(corner.intersected(rect()));

    p.setPen(QPen(border, 1.0));
    if (!hRect.isEmpty())
        p.drawLine(hRect.bottomLeft(), hRect.bottomRight());
    if (!vRect.isEmpty())
        p.drawLine(vRect.topRight(), vRect.bottomRight());

    drawRulerTicks(p, true, horizontalRulerRect());
    drawRulerTicks(p, false, verticalRulerRect());
    drawMouseIndicator(p);
}

void RulerGuideOverlay::drawRulerTicks(QPainter& p, bool horizontal, const QRectF& fullRect)
{
    if (fullRect.isEmpty())
        return;

    const QRectF clipped = fullRect.intersected(rect());
    if (clipped.isEmpty())
        return;

    const auto* th = ThemeManager::instance()->current();
    p.save();
    p.setClipRect(clipped);

    const qreal axisLength = horizontal ? m_doc->size.width() : m_doc->size.height();
    const qreal pixelsPerDocument = horizontal ? screenPixelsPerDocumentX() : screenPixelsPerDocumentY();
    qreal majorSpacing = adaptiveMajorSpacing(pixelsPerDocument, m_settings.rulers.majorTickSpacing);
    if (m_settings.rulers.unit == RulerUnit::Percent) {
        const qreal configuredSpacing = axisLength * std::max<qreal>(1.0, m_settings.rulers.majorTickSpacing) / 100.0;
        majorSpacing = adaptiveMajorSpacing(pixelsPerDocument, configuredSpacing);
    } else if (m_settings.rulers.unit != RulerUnit::Pixels) {
        const qreal dpi = m_doc ? std::max<qreal>(1.0, m_doc->resolutionDpi) : 300.0;
        const qreal docPixelsPerUnit = documentPixelsPerUnit(m_settings.rulers.unit, dpi, axisLength);
        majorSpacing = niceUnitSpacing(docPixelsPerUnit * pixelsPerDocument) * docPixelsPerUnit;
    }
    const qreal minorSpacing = majorSpacing / std::max(1, m_settings.rulers.minorTickDivisions);
    const QRectF viewport = m_viewportRect.isValid() ? m_viewportRect : QRectF(rect());
    const qreal minDoc = horizontal ? screenToDocument(QPointF(clipped.left(), viewport.top())).x()
                                    : screenToDocument(QPointF(viewport.left(), clipped.top())).y();
    const qreal maxDoc = horizontal ? screenToDocument(QPointF(clipped.right(), viewport.top())).x()
                                    : screenToDocument(QPointF(viewport.left(), clipped.bottom())).y();
    const qreal from = std::min(minDoc, maxDoc);
    const qreal to = std::max(minDoc, maxDoc);
    const qreal first = std::floor(from / minorSpacing) * minorSpacing;

    QColor tick = th->colorTextDisabled;
    tick.setAlpha(190);
    QColor label = th->colorTextDisabled;
    label.setAlpha(230);

    QFont font = p.font();
    font.setPixelSize(th->fontSizeXS);
    p.setFont(font);

    for (qreal value = first; value <= to + minorSpacing; value += minorSpacing) {
        const qreal ratio = majorSpacing > 0.0 ? value / majorSpacing : 0.0;
        const bool isMajor = std::abs(ratio - std::round(ratio)) < 0.001;
        const qreal screen = horizontal
            ? documentToScreen(QPointF(value, 0.0)).x()
            : documentToScreen(QPointF(0.0, value)).y();

        const qreal rulerThickness = horizontal ? fullRect.height() : fullRect.width();
        const qreal tickLength = isMajor ? rulerThickness * 0.72 : kMinorRulerTickLengthPx;
        p.setPen(QPen(tick, 1.0));
        if (horizontal) {
            p.drawLine(QPointF(screen, fullRect.bottom()),
                       QPointF(screen, fullRect.bottom() - tickLength));
        } else {
            p.drawLine(QPointF(fullRect.right(), screen),
                       QPointF(fullRect.right() - tickLength, screen));
        }

        if (isMajor && majorSpacing * pixelsPerDocument >= 42.0) {
            p.setPen(label);
            const QString text = labelForPosition(value, axisLength);
            if (horizontal) {
                p.drawText(QRectF(screen + 4.0, fullRect.top() + 2.0,
                                  90.0, fullRect.height() - 4.0),
                           Qt::AlignLeft | Qt::AlignVCenter, text);
            } else {
                p.save();
                p.translate(fullRect.left() + 2.0, screen - 4.0);
                p.rotate(-90.0);
                p.drawText(QRectF(0.0, 0.0, 90.0, fullRect.width() - 4.0),
                           Qt::AlignLeft | Qt::AlignVCenter, text);
                p.restore();
            }
        }
    }

    p.restore();
}

void RulerGuideOverlay::drawMouseIndicator(QPainter& p)
{
    if (!m_settings.rulers.showMouseIndicator || !m_mouseDocPos || !m_doc)
        return;

    const QPointF doc = *m_mouseDocPos;
    if (doc.x() < 0.0 || doc.y() < 0.0
        || doc.x() > m_doc->size.width() || doc.y() > m_doc->size.height())
        return;

    const auto* th = ThemeManager::instance()->current();
    QColor color = th->colorAccent;
    color.setAlpha(235);
    p.setBrush(color);
    p.setPen(Qt::NoPen);

    const QRectF h = horizontalRulerRect();
    const QRectF v = verticalRulerRect();
    const qreal x = documentToScreen(QPointF(doc.x(), 0.0)).x();
    const qreal y = documentToScreen(QPointF(0.0, doc.y())).y();

    if (h.intersects(rect())) {
        QPainterPath tri;
        tri.moveTo(x, h.bottom());
        tri.lineTo(x - 5.0, h.bottom() - 7.0);
        tri.lineTo(x + 5.0, h.bottom() - 7.0);
        tri.closeSubpath();
        p.drawPath(tri);
        p.setPen(QPen(color, 1.0));
        p.drawLine(QPointF(x, h.top()), QPointF(x, h.bottom()));
    }

    if (v.intersects(rect())) {
        QPainterPath tri;
        tri.moveTo(v.right(), y);
        tri.lineTo(v.right() - 7.0, y - 5.0);
        tri.lineTo(v.right() - 7.0, y + 5.0);
        tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.drawPath(tri);
        p.setPen(QPen(color, 1.0));
        p.drawLine(QPointF(v.left(), y), QPointF(v.right(), y));
    }
}
