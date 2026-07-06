#pragma once

#include <QWidget>
#include <QImage>

class SaturationBrightnessArea : public QWidget {
    Q_OBJECT
public:
    explicit SaturationBrightnessArea(QWidget* parent = nullptr);

    void setColor(const QColor& color);
    void setHue(double hue);
    double hue() const { return m_hue; }
    double saturation() const { return m_saturation; }
    double brightness() const { return m_brightness; }

    QSize sizeHint() const override { return QSize(280, 280); }
    QSize minimumSizeHint() const override { return QSize(150, 150); }

signals:
    void colorChanged(const QColor& color);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuildGradient();
    void updateFromMouse(const QPoint& pos);

    QImage m_gradient;
    double m_hue = 0.0;
    double m_saturation = 0.0;
    double m_brightness = 1.0;
    bool m_dragging = false;
};
