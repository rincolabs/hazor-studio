#pragma once

#include "gradient/GradientTypes.hpp"

#include <QWidget>

class GradientPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit GradientPreviewWidget(QWidget* parent = nullptr);

    void setGradient(const GradientDefinition& definition);
    GradientDefinition gradient() const { return m_definition; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawCheckerboard(QPainter& painter, const QRect& rect) const;

    GradientDefinition m_definition;
};

