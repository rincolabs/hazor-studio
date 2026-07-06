#pragma once

#include <QWidget>
#include "brush/BrushTypes.hpp"

// Brush tip preview: a circle sized to the brush, a hardness
// falloff, a central cross and (for non-circular tips) an outer angle marker.
// It is also an editor:
//   - horizontal drag → brush size
//   - vertical drag   → brush hardness
//   - drag the outer marker → brush angle (hidden for a pure round circle tip)
//
// In "orientation mode" (setOrientationEditing(true)) it instead behaves like the
// conventional Brush Tip Shape angle/roundness control: a fixed reference circle
// holding a flattened, rotated dab ellipse with draggable angle handles (the two
// ends of the major axis) and a roundness handle (the end of the minor axis).
// Size/hardness are then driven by the page sliders, not by dragging the body.
class BrushTipPreview : public QWidget {
    Q_OBJECT

public:
    explicit BrushTipPreview(QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(96, 96); }

    // Sync display from the active brush (does not emit).
    void setBrush(const BrushSettings& settings);
    void setSize(float size);          // brush radius, px
    void setHardness(float hardness);  // 0..1
    void setAngle(float radians);      // dab rotation
    void setRoundness(float roundness);// 0..1 (1 = circular)

    // When on, the widget is an angle/roundness editor with a fixed reference
    // circle (see class comment). Defaults to off (size/hardness drag).
    void setOrientationEditing(bool on);

    float brushSize() const { return m_size; }
    float hardness() const { return m_hardness; }
    float angle() const { return m_angle; }
    float roundness() const { return m_roundness; }

signals:
    void sizeChanged(float size);
    void hardnessChanged(float hardness);
    void angleChanged(float radians);     // dab orientation
    void roundnessChanged(float roundness); // 0..1

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragMode { None, SizeHardness, Angle, Roundness };

    bool hasAngleControl() const;
    QPointF angleMarkerPos(float displayRadius) const;
    QPointF roundnessHandlePos(float displayRadius) const;
    float displayRadius() const;
    float orientationRadius() const;

    float m_size = 20.0f;
    float m_hardness = 0.8f;
    float m_angle = 0.0f; // radians
    float m_roundness = 1.0f; // 0..1, 1 = circular
    bool m_orientation = false;
    BrushType m_type = BrushType::Round;
    BrushTipSource m_tipSource = BrushTipSource::Circle;

    DragMode m_drag = DragMode::None;
    QPointF m_lastPos;
    QPointF m_dragStartPos;
    float m_dragStartSize = 20.0f;
    float m_dragStartHardness = 0.8f;

    static constexpr float kSizeMin = 1.0f;
    static constexpr float kSizeMax = 1000.0f;
    static constexpr float kRoundnessMin = 0.05f;
};
