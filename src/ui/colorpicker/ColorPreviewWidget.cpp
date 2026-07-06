#include "ColorPreviewWidget.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QPainterPath>

ColorPreviewWidget::ColorPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void ColorPreviewWidget::setCurrentColor(const QColor& color)
{
    if (m_currentColor == color) return;
    m_currentColor = color;
    update();
}

void ColorPreviewWidget::setNewColor(const QColor& color)
{
    if (m_newColor == color) return;
    m_newColor = color;
    update();
}

void ColorPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto* t = ThemeManager::instance()->current();
    p.fillRect(rect(), t->colorSurface);

    const int w = width();
    const int h = height();
    const int labelH = 14;
    const int boxX = 4;
    const int boxW = w - 8;

    p.setFont(QFont(t->fontFamily, t->fontSizeXS));

    // Two swatches stacked vertically (new on top, current below), touching with
    // no gap between them so the colour difference reads at a glance. Labels sit
    // above and below the pair.
    const int swatchTop = labelH;
    const int swatchH = (h - labelH * 2) / 2;

    QRect newRect(boxX, swatchTop, boxW, swatchH);
    QRect curRect(boxX, swatchTop + swatchH, boxW, swatchH);

    p.fillRect(newRect, m_newColor);
    p.fillRect(curRect, m_currentColor);

    p.setPen(t->colorBorder);
    p.setBrush(Qt::NoBrush);
    p.drawRect(newRect);
    p.drawRect(curRect);

    p.setPen(t->colorTextSecondary);
    p.drawText(QRect(boxX, 0, boxW, labelH), Qt::AlignLeft | Qt::AlignVCenter, tr("new"));
    p.drawText(QRect(boxX, swatchTop + swatchH * 2, boxW, labelH),
               Qt::AlignLeft | Qt::AlignVCenter, tr("current"));
}
