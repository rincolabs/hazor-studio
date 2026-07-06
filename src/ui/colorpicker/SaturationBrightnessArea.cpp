#include "SaturationBrightnessArea.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

SaturationBrightnessArea::SaturationBrightnessArea(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void SaturationBrightnessArea::setHue(double hue)
{
    hue = qBound(0.0, hue, 360.0);
    if (qFuzzyCompare(m_hue, hue)) return;
    m_hue = hue;
    rebuildGradient();
    update();
}

void SaturationBrightnessArea::setColor(const QColor& color)
{
    double h = color.hueF() * 360.0;
    if (h < 0) h = 0.0;
    double s = color.saturationF() * 100.0;
    double b = color.valueF() * 100.0;
    bool hueChanged = !qFuzzyCompare(m_hue, h);
    m_saturation = s;
    m_brightness = b;
    m_hue = h;
    if (hueChanged)
        rebuildGradient();
    update();
}

void SaturationBrightnessArea::resizeEvent(QResizeEvent*)
{
    rebuildGradient();
}

void SaturationBrightnessArea::rebuildGradient()
{
    const int w = width() - 4;
    const int h = height() - 4;
    if (w < 2 || h < 2) {
        m_gradient = QImage();
        return;
    }

    QColor hueColor = QColor::fromHsvF(m_hue / 360.0, 1.0, 1.0);

    m_gradient = QImage(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        double b = 1.0 - static_cast<double>(y) / (h - 1);
        QRgb* row = reinterpret_cast<QRgb*>(m_gradient.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double s = static_cast<double>(x) / (w - 1);

            int r = static_cast<int>((1.0 - s) * 255 + s * hueColor.red() * b);
            int g = static_cast<int>((1.0 - s) * 255 + s * hueColor.green() * b);
            int bl = static_cast<int>((1.0 - s) * 255 + s * hueColor.blue() * b);

            row[x] = qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, bl, 255));
        }
    }
}

void SaturationBrightnessArea::updateFromMouse(const QPoint& pos)
{
    const int w = width() - 4;
    const int h = height() - 4;
    if (w < 1 || h < 1) return;

    int x = pos.x() - 2;
    int y = pos.y() - 2;

    double s = qBound(0.0, static_cast<double>(x) / (w - 1), 1.0);
    double b = qBound(0.0, 1.0 - static_cast<double>(y) / (h - 1), 1.0);

    double newS = s * 100.0;
    double newB = b * 100.0;

    if (qFuzzyCompare(newS, m_saturation) && qFuzzyCompare(newB, m_brightness))
        return;

    m_saturation = newS;
    m_brightness = newB;

    QColor c = QColor::fromHsvF(m_hue / 360.0, s, b);
    emit colorChanged(c);
    update();
}

void SaturationBrightnessArea::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto* t = ThemeManager::instance()->current();

    const int areaX = 2;
    const int areaY = 2;
    const int areaW = width() - 4;
    const int areaH = height() - 4;

    if (!m_gradient.isNull())
        p.drawImage(QRect(areaX, areaY, areaW, areaH), m_gradient);

    p.setPen(t->colorBorder);
    p.setBrush(Qt::NoBrush);
    p.drawRect(areaX, areaY, areaW - 1, areaH - 1);

    double sx = (m_saturation / 100.0) * (areaW - 1);
    double sy = (1.0 - m_brightness / 100.0) * (areaH - 1);
    int cx = static_cast<int>(areaX + sx);
    int cy = static_cast<int>(areaY + sy);
    int cr = 6;

    QColor markerColor = QColor::fromHsvF(m_hue / 360.0,
                                          m_saturation / 100.0,
                                          m_brightness / 100.0);
    int grayVal = qGray(markerColor.red(), markerColor.green(), markerColor.blue());
    QColor ringColor = grayVal > 128 ? Qt::black : Qt::white;

    p.setPen(QPen(ringColor, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), cr, cr);
}

void SaturationBrightnessArea::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        updateFromMouse(e->pos());
    }
}

void SaturationBrightnessArea::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging)
        updateFromMouse(e->pos());
}

void SaturationBrightnessArea::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}
