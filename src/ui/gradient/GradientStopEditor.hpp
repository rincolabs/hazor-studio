#pragma once

#include "gradient/GradientTypes.hpp"

#include <QWidget>

class GradientStopEditor : public QWidget {
    Q_OBJECT

public:
    enum class StopType { None, Color, Opacity };

    explicit GradientStopEditor(QWidget* parent = nullptr);

    void setGradient(const GradientDefinition& definition);
    GradientDefinition gradient() const { return m_definition; }
    StopType activeStopType() const { return m_activeType; }
    int activeStopIndex() const { return m_activeIndex; }
    QColor activeColor() const;
    double activeOpacity() const;
    double activeLocation() const;

    void setActiveColor(const QColor& color);
    void setActiveOpacity(double opacity);
    void setActiveLocation(double position);
    void deleteActiveStop();

signals:
    void gradientChanged(const GradientDefinition& definition);
    void activeStopChanged(GradientStopEditor::StopType type, int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect previewRect() const;
    double positionFromX(double x) const;
    double xFromPosition(double position) const;
    int hitStop(QPointF point, StopType* type) const;
    int hitMidpoint(QPointF point, StopType* type) const;
    void selectStop(StopType type, int index);
    void moveActiveStop(double position);
    void moveMidpoint(double position);
    void emitChanged();

    GradientDefinition m_definition;
    StopType m_activeType = StopType::Color;
    int m_activeIndex = 0;
    QColor m_selectedColor = Qt::black;
    StopType m_dragType = StopType::None;
    int m_dragIndex = -1;
    bool m_draggingMidpoint = false;
};
