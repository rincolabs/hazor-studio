#include "GradientStopEditor.hpp"

#include "gradient/GradientRenderer.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QLineF>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kHandleRadius = 6;

QPolygonF colorStopShape(QPointF center)
{
    QPolygonF poly;
    poly << QPointF(center.x() - 6, center.y() + 7)
         << QPointF(center.x() + 6, center.y() + 7)
         << QPointF(center.x() + 6, center.y() - 2)
         << QPointF(center.x(), center.y() - 8)
         << QPointF(center.x() - 6, center.y() - 2);
    return poly;
}

QPolygonF opacityStopShape(QPointF center)
{
    QPolygonF poly;
    poly << QPointF(center.x() - 6, center.y() - 7)
         << QPointF(center.x() + 6, center.y() - 7)
         << QPointF(center.x() + 6, center.y() + 2)
         << QPointF(center.x(), center.y() + 8)
         << QPointF(center.x() - 6, center.y() + 2);
    return poly;
}

double clampStopPosition(double value, double current, double lower, double upper)
{
    if (lower > upper)
        return current;
    return std::clamp(value, lower, upper);
}
}

GradientStopEditor::GradientStopEditor(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(96);
    setMouseTracking(true);
    m_definition.colorStops = {
        {Qt::black, 0.0, 0.5},
        {Qt::white, 1.0, 0.5},
    };
    m_definition.opacityStops = {
        {1.0, 0.0, 0.5},
        {1.0, 1.0, 0.5},
    };
    m_definition.normalize();
    m_activeIndex = 0;
    m_selectedColor = m_definition.colorStops.first().color;
    m_selectedColor.setAlpha(255);
}

void GradientStopEditor::setGradient(const GradientDefinition& definition)
{
    m_definition = definition;
    m_definition.normalize();
    m_activeType = StopType::Color;
    m_activeIndex = 0;
    m_selectedColor = m_definition.colorStops.first().color;
    m_selectedColor.setAlpha(255);
    update();
    emit activeStopChanged(m_activeType, m_activeIndex);
}

QColor GradientStopEditor::activeColor() const
{
    if (m_activeType == StopType::Color
        && m_activeIndex >= 0
        && m_activeIndex < m_definition.colorStops.size()) {
        return m_definition.colorStops[m_activeIndex].color;
    }
    return Qt::black;
}

double GradientStopEditor::activeOpacity() const
{
    if (m_activeType == StopType::Opacity
        && m_activeIndex >= 0
        && m_activeIndex < m_definition.opacityStops.size()) {
        return m_definition.opacityStops[m_activeIndex].opacity;
    }
    return 1.0;
}

double GradientStopEditor::activeLocation() const
{
    if (m_activeType == StopType::Color
        && m_activeIndex >= 0
        && m_activeIndex < m_definition.colorStops.size()) {
        return m_definition.colorStops[m_activeIndex].position;
    }
    if (m_activeType == StopType::Opacity
        && m_activeIndex >= 0
        && m_activeIndex < m_definition.opacityStops.size()) {
        return m_definition.opacityStops[m_activeIndex].position;
    }
    return 0.0;
}

void GradientStopEditor::setActiveColor(const QColor& color)
{
    if (m_activeType != StopType::Color
        || m_activeIndex < 0
        || m_activeIndex >= m_definition.colorStops.size()) {
        return;
    }
    m_selectedColor = color.toRgb();
    m_selectedColor.setAlpha(255);
    m_definition.colorStops[m_activeIndex].color = m_selectedColor;
    emitChanged();
}

void GradientStopEditor::setActiveOpacity(double opacity)
{
    if (m_activeType != StopType::Opacity
        || m_activeIndex < 0
        || m_activeIndex >= m_definition.opacityStops.size()) {
        return;
    }
    m_definition.opacityStops[m_activeIndex].opacity = std::clamp(opacity, 0.0, 1.0);
    emitChanged();
}

void GradientStopEditor::setActiveLocation(double position)
{
    moveActiveStop(position);
}

void GradientStopEditor::deleteActiveStop()
{
    if (m_activeType == StopType::Color && m_definition.colorStops.size() > 2) {
        m_definition.colorStops.removeAt(m_activeIndex);
        m_activeIndex = std::clamp(m_activeIndex, 0,
                                   static_cast<int>(m_definition.colorStops.size()) - 1);
        emitChanged();
    } else if (m_activeType == StopType::Opacity && m_definition.opacityStops.size() > 2) {
        m_definition.opacityStops.removeAt(m_activeIndex);
        m_activeIndex = std::clamp(m_activeIndex, 0,
                                   static_cast<int>(m_definition.opacityStops.size()) - 1);
        emitChanged();
    }
}

void GradientStopEditor::paintEvent(QPaintEvent*)
{
    auto* theme = ThemeManager::instance()->current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect bar = previewRect();
    const int checker = 8;
    for (int y = bar.top(); y <= bar.bottom(); y += checker) {
        for (int x = bar.left(); x <= bar.right(); x += checker) {
            const bool even = (((x - bar.left()) / checker) + ((y - bar.top()) / checker)) % 2 == 0;
            painter.fillRect(QRect(x, y, checker, checker).intersected(bar),
                             even ? QColor(235, 235, 235) : QColor(190, 190, 190));
        }
    }
    painter.drawImage(bar, GradientRenderer::generateThumbnail(m_definition, bar.size()));
    painter.setPen(theme ? theme->colorBorder : QColor(90, 90, 90));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bar, 3, 3);

    auto drawMidpoints = [&](StopType type) {
        const bool color = type == StopType::Color;
        const int count = color ? m_definition.colorStops.size() : m_definition.opacityStops.size();
        for (int i = 0; i < count - 1; ++i) {
            const double p0 = color ? m_definition.colorStops[i].position
                                    : m_definition.opacityStops[i].position;
            const double p1 = color ? m_definition.colorStops[i + 1].position
                                    : m_definition.opacityStops[i + 1].position;
            const double midpoint = color ? m_definition.colorStops[i].midpoint
                                          : m_definition.opacityStops[i].midpoint;
            QPointF c(xFromPosition(p0 + (p1 - p0) * midpoint),
                      color ? bar.bottom() + 22 : bar.top() - 22);
            QPolygonF diamond;
            diamond << QPointF(c.x(), c.y() - 4)
                    << QPointF(c.x() + 4, c.y())
                    << QPointF(c.x(), c.y() + 4)
                    << QPointF(c.x() - 4, c.y());
            painter.setPen(QPen(QColor(0, 0, 0, 180), 1.0));
            painter.setBrush(theme ? theme->colorTextSecondary : QColor(190, 190, 190));
            painter.drawPolygon(diamond);
        }
    };
    drawMidpoints(StopType::Opacity);
    drawMidpoints(StopType::Color);

    auto drawColorStop = [&](int i) {
        const auto& stop = m_definition.colorStops[i];
        QPointF c(xFromPosition(stop.position), bar.bottom() + 14);
        QPolygonF poly = colorStopShape(c);
        painter.setPen(QPen(i == m_activeIndex && m_activeType == StopType::Color
            ? (theme ? theme->colorAccent : QColor(80, 145, 255))
            : QColor(0, 0, 0, 190), 1.5));
        painter.setBrush(stop.color);
        painter.drawPolygon(poly);
    };
    auto drawOpacityStop = [&](int i) {
        const auto& stop = m_definition.opacityStops[i];
        QPointF c(xFromPosition(stop.position), bar.top() - 14);
        QPolygonF poly = opacityStopShape(c);
        const int gray = std::clamp(static_cast<int>(std::round(stop.opacity * 255.0)), 0, 255);
        painter.setPen(QPen(i == m_activeIndex && m_activeType == StopType::Opacity
            ? (theme ? theme->colorAccent : QColor(80, 145, 255))
            : QColor(0, 0, 0, 190), 1.5));
        painter.setBrush(QColor(gray, gray, gray));
        painter.drawPolygon(poly);
    };

    for (int i = 0; i < m_definition.opacityStops.size(); ++i)
        drawOpacityStop(i);
    for (int i = 0; i < m_definition.colorStops.size(); ++i)
        drawColorStop(i);
}

void GradientStopEditor::mousePressEvent(QMouseEvent* event)
{
    StopType type = StopType::None;
    int index = hitStop(event->position(), &type);
    if (index >= 0) {
        selectStop(type, index);
        m_dragType = type;
        m_dragIndex = index;
        m_draggingMidpoint = false;
        return;
    }

    index = hitMidpoint(event->position(), &type);
    if (index >= 0) {
        m_dragType = type;
        m_dragIndex = index;
        m_draggingMidpoint = true;
        return;
    }

    const QRect bar = previewRect();
    const double position = positionFromX(event->position().x());
    if (event->position().y() > bar.bottom()) {
        GradientColorStop stop{m_selectedColor, position, 0.5};
        m_definition.colorStops.append(stop);
        m_definition.normalize();
        int newIndex = 0;
        for (int i = 0; i < m_definition.colorStops.size(); ++i) {
            if (std::abs(m_definition.colorStops[i].position - position) < 0.0001) {
                newIndex = i;
                break;
            }
        }
        selectStop(StopType::Color, newIndex);
        emitChanged();
    } else if (event->position().y() < bar.top()) {
        const QColor sampled = GradientRenderer::sampleGradientAt(m_definition, position);
        GradientOpacityStop stop{sampled.alphaF(), position, 0.5};
        m_definition.opacityStops.append(stop);
        m_definition.normalize();
        int newIndex = 0;
        for (int i = 0; i < m_definition.opacityStops.size(); ++i) {
            if (std::abs(m_definition.opacityStops[i].position - position) < 0.0001) {
                newIndex = i;
                break;
            }
        }
        selectStop(StopType::Opacity, newIndex);
        emitChanged();
    }
}

void GradientStopEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragType == StopType::None || m_dragIndex < 0)
        return;
    if (m_draggingMidpoint)
        moveMidpoint(positionFromX(event->position().x()));
    else
        moveActiveStop(positionFromX(event->position().x()));
}

void GradientStopEditor::mouseReleaseEvent(QMouseEvent*)
{
    m_dragType = StopType::None;
    m_dragIndex = -1;
    m_draggingMidpoint = false;
}

void GradientStopEditor::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
}

QRect GradientStopEditor::previewRect() const
{
    return QRect(16, 34, std::max(20, width() - 32), 26);
}

double GradientStopEditor::positionFromX(double x) const
{
    const QRect bar = previewRect();
    return std::clamp((x - bar.left()) / std::max(1.0, static_cast<double>(bar.width())),
                      0.0, 1.0);
}

double GradientStopEditor::xFromPosition(double position) const
{
    const QRect bar = previewRect();
    return bar.left() + std::clamp(position, 0.0, 1.0) * bar.width();
}

int GradientStopEditor::hitStop(QPointF point, StopType* type) const
{
    const QRect bar = previewRect();
    for (int i = 0; i < m_definition.colorStops.size(); ++i) {
        QPointF c(xFromPosition(m_definition.colorStops[i].position), bar.bottom() + 14);
        if (QLineF(point, c).length() <= kHandleRadius + 4) {
            *type = StopType::Color;
            return i;
        }
    }
    for (int i = 0; i < m_definition.opacityStops.size(); ++i) {
        QPointF c(xFromPosition(m_definition.opacityStops[i].position), bar.top() - 14);
        if (QLineF(point, c).length() <= kHandleRadius + 4) {
            *type = StopType::Opacity;
            return i;
        }
    }
    *type = StopType::None;
    return -1;
}

int GradientStopEditor::hitMidpoint(QPointF point, StopType* type) const
{
    const QRect bar = previewRect();
    for (int i = 0; i < m_definition.colorStops.size() - 1; ++i) {
        const double p0 = m_definition.colorStops[i].position;
        const double p1 = m_definition.colorStops[i + 1].position;
        QPointF c(xFromPosition(p0 + (p1 - p0) * m_definition.colorStops[i].midpoint),
                  bar.bottom() + 22);
        if (QLineF(point, c).length() <= 7.0) {
            *type = StopType::Color;
            return i;
        }
    }
    for (int i = 0; i < m_definition.opacityStops.size() - 1; ++i) {
        const double p0 = m_definition.opacityStops[i].position;
        const double p1 = m_definition.opacityStops[i + 1].position;
        QPointF c(xFromPosition(p0 + (p1 - p0) * m_definition.opacityStops[i].midpoint),
                  bar.top() - 22);
        if (QLineF(point, c).length() <= 7.0) {
            *type = StopType::Opacity;
            return i;
        }
    }
    *type = StopType::None;
    return -1;
}

void GradientStopEditor::selectStop(StopType type, int index)
{
    m_activeType = type;
    m_activeIndex = index;
    if (m_activeType == StopType::Color
        && m_activeIndex >= 0
        && m_activeIndex < m_definition.colorStops.size()) {
        m_selectedColor = m_definition.colorStops[m_activeIndex].color.toRgb();
        m_selectedColor.setAlpha(255);
    }
    update();
    emit activeStopChanged(type, index);
}

void GradientStopEditor::moveActiveStop(double position)
{
    if (m_activeType == StopType::Color
        && m_activeIndex >= 0
        && m_activeIndex < m_definition.colorStops.size()) {
        const double lower = m_activeIndex > 0
            ? m_definition.colorStops[m_activeIndex - 1].position + 0.001
            : 0.0;
        const double upper = m_activeIndex < m_definition.colorStops.size() - 1
            ? m_definition.colorStops[m_activeIndex + 1].position - 0.001
            : 1.0;
        position = clampStopPosition(position,
                                     m_definition.colorStops[m_activeIndex].position,
                                     lower,
                                     upper);
        m_definition.colorStops[m_activeIndex].position = position;
        emitChanged();
    } else if (m_activeType == StopType::Opacity
               && m_activeIndex >= 0
               && m_activeIndex < m_definition.opacityStops.size()) {
        const double lower = m_activeIndex > 0
            ? m_definition.opacityStops[m_activeIndex - 1].position + 0.001
            : 0.0;
        const double upper = m_activeIndex < m_definition.opacityStops.size() - 1
            ? m_definition.opacityStops[m_activeIndex + 1].position - 0.001
            : 1.0;
        position = clampStopPosition(position,
                                     m_definition.opacityStops[m_activeIndex].position,
                                     lower,
                                     upper);
        m_definition.opacityStops[m_activeIndex].position = position;
        emitChanged();
    }
}

void GradientStopEditor::moveMidpoint(double position)
{
    if (m_dragType == StopType::Color
        && m_dragIndex >= 0
        && m_dragIndex < m_definition.colorStops.size() - 1) {
        const double p0 = m_definition.colorStops[m_dragIndex].position;
        const double p1 = m_definition.colorStops[m_dragIndex + 1].position;
        m_definition.colorStops[m_dragIndex].midpoint = std::clamp((position - p0) / std::max(0.001, p1 - p0), 0.001, 0.999);
        emitChanged();
    } else if (m_dragType == StopType::Opacity
               && m_dragIndex >= 0
               && m_dragIndex < m_definition.opacityStops.size() - 1) {
        const double p0 = m_definition.opacityStops[m_dragIndex].position;
        const double p1 = m_definition.opacityStops[m_dragIndex + 1].position;
        m_definition.opacityStops[m_dragIndex].midpoint = std::clamp((position - p0) / std::max(0.001, p1 - p0), 0.001, 0.999);
        emitChanged();
    }
}

void GradientStopEditor::emitChanged()
{
    m_definition.normalize();
    update();
    emit gradientChanged(m_definition);
    emit activeStopChanged(m_activeType, m_activeIndex);
}
