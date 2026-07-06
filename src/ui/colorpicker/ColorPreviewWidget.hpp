#pragma once

#include <QWidget>
#include <QColor>

class ColorPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorPreviewWidget(QWidget* parent = nullptr);

    void setCurrentColor(const QColor& color);
    void setNewColor(const QColor& color);

    QSize sizeHint() const override { return QSize(64, 80); }
    QSize minimumSizeHint() const override { return QSize(48, 60); }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QColor m_currentColor = Qt::black;
    QColor m_newColor = Qt::black;
};
