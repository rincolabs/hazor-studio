#pragma once

#include <QWidget>
#include <QImage>

class ColorHueSlider : public QWidget {
    Q_OBJECT
public:
    explicit ColorHueSlider(QWidget* parent = nullptr);

    void setHue(double h);
    double hue() const { return m_hue; }

    QSize sizeHint() const override { return QSize(24, 200); }
    QSize minimumSizeHint() const override { return QSize(16, 100); }

signals:
    void hueChanged(double hue);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuildGradient();
    double yToHue(int y) const;
    int hueToY(double h) const;

    QImage m_gradient;
    double m_hue = 0.0;
    bool m_dragging = false;
};
