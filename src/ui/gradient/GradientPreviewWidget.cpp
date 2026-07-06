#include "GradientPreviewWidget.hpp"

#include "gradient/GradientRenderer.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>

GradientPreviewWidget::GradientPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    m_definition.name = tr("Black to White");
    m_definition.colorStops = {
        {Qt::black, 0.0, 0.5},
        {Qt::white, 1.0, 0.5},
    };
    m_definition.opacityStops = {
        {1.0, 0.0, 0.5},
        {1.0, 1.0, 0.5},
    };
    m_definition.normalize();
    setMinimumHeight(22);
}

void GradientPreviewWidget::setGradient(const GradientDefinition& definition)
{
    m_definition = definition;
    m_definition.normalize();
    update();
}

QSize GradientPreviewWidget::sizeHint() const
{
    return QSize(160, 24);
}

void GradientPreviewWidget::paintEvent(QPaintEvent*)
{
    auto* theme = ThemeManager::instance()->current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect rect = this->rect().adjusted(1, 1, -1, -1);
    drawCheckerboard(painter, rect);

    const QImage thumbnail = GradientRenderer::generateThumbnail(m_definition, rect.size());
    painter.drawImage(rect, thumbnail);

    painter.setPen(theme ? theme->colorBorder : QColor(80, 80, 80));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, 3, 3);
}

void GradientPreviewWidget::drawCheckerboard(QPainter& painter, const QRect& rect) const
{
    const int tile = 6;
    const QColor a(235, 235, 235);
    const QColor b(190, 190, 190);
    for (int y = rect.top(); y <= rect.bottom(); y += tile) {
        for (int x = rect.left(); x <= rect.right(); x += tile) {
            const bool even = (((x - rect.left()) / tile) + ((y - rect.top()) / tile)) % 2 == 0;
            painter.fillRect(QRect(x, y, tile, tile).intersected(rect), even ? a : b);
        }
    }
}

