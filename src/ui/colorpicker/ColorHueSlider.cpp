#include "ColorHueSlider.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

ColorHueSlider::ColorHueSlider(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void ColorHueSlider::setHue(double h)
{
    h = qBound(0.0, h, 360.0);
    if (qFuzzyCompare(m_hue, h)) return;
    m_hue = h;
    update();
}

void ColorHueSlider::resizeEvent(QResizeEvent*)
{
    rebuildGradient();
}

void ColorHueSlider::rebuildGradient()
{
    const int w = width() - 8;
    const int h = height() - 4;
    if (w <= 0 || h <= 0) {
        m_gradient = QImage();
        return;
    }

    m_gradient = QImage(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        double hue = (1.0 - static_cast<double>(y) / (h - 1)) * 360.0;
        QColor c = QColor::fromHsvF(hue / 360.0, 1.0, 1.0);
        QRgb* row = reinterpret_cast<QRgb*>(m_gradient.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = c.rgb();
    }
}

double ColorHueSlider::yToHue(int y) const
{
    const int sliderH = height() - 4;
    const int adjY = y - 2;
    if (sliderH < 1) return m_hue;
    double hue = 360.0 * (1.0 - static_cast<double>(adjY) / (sliderH - 1));
    return qBound(0.0, hue, 360.0);
}

int ColorHueSlider::hueToY(double h) const
{
    const int sliderH = height() - 4;
    if (sliderH < 1) return 2;
    return 2 + static_cast<int>((1.0 - h / 360.0) * (sliderH - 1));
}

void ColorHueSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto* t = ThemeManager::instance()->current();

    const int barX = 4;
    const int barW = width() - 8;
    const int barY = 2;
    const int barH = height() - 4;

    if (!m_gradient.isNull())
        p.drawImage(QRect(barX, barY, barW, barH), m_gradient);

    p.setPen(t->colorBorder);
    p.setBrush(Qt::NoBrush);
    p.drawRect(barX, barY, barW - 1, barH - 1);

    const int markerY = hueToY(m_hue);
    const int markerW = barW + 6;

    p.setPen(QPen(Qt::white, 2));
    p.drawLine(barX - 3, markerY, barX + barW + 2, markerY);

    p.setPen(QPen(Qt::black, 1));
    p.drawLine(barX - 3, markerY + 1, barX + barW + 2, markerY + 1);
    p.drawLine(barX - 3, markerY - 1, barX + barW + 2, markerY - 1);
}

void ColorHueSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        double hue = yToHue(e->pos().y());
        if (!qFuzzyCompare(hue, m_hue)) {
            m_hue = hue;
            emit hueChanged(m_hue);
            update();
        }
    }
}

void ColorHueSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        double hue = yToHue(e->pos().y());
        if (!qFuzzyCompare(hue, m_hue)) {
            m_hue = hue;
            emit hueChanged(m_hue);
            update();
        }
    }
}

void ColorHueSlider::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}
