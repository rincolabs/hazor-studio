#pragma once

#include <QWidget>
#include "brush/DynamicsConfig.hpp"   // ResponseCurve, Sensor, CurveOption

class QComboBox;
class QSlider;
class QLabel;
class AppCheckBox;

// ── Editable response curve (Layer B) ─────────────────────────────────────────
// Edits a ResponseCurve directly in its own 0..1 × 0..1 space with piecewise-
// linear segments, so what you draw is exactly what evaluate() computes (no
// spline/256 mismatch). Endpoints stay pinned to x=0 and x=1; interior points are
// draggable; double-click adds a point on an empty spot and removes an interior
// one. An empty/identity curve is shown as the diagonal.
class ResponseCurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit ResponseCurveEditor(QWidget* parent = nullptr);

    void setCurve(const ResponseCurve& c);   // no signal
    ResponseCurve curve() const;

    QSize sizeHint() const override { return QSize(180, 120); }
    QSize minimumSizeHint() const override { return QSize(140, 90); }

signals:
    void editBegan();
    void changed();        // live, while dragging
    void editCommitted();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QRect plotRect() const;
    QPointF toWidget(const QPointF& p) const;   // curve(0..1) → pixels
    QPointF toCurve(const QPointF& w) const;     // pixels → curve(0..1)
    int hitTest(const QPointF& w) const;
    void sortPoints();

    QVector<QPointF> m_points{ {0.0, 0.0}, {1.0, 1.0} };
    int m_selected = -1;
    bool m_dragging = false;
};

// ── One curve option ──────────────────────────────────────────────────────────
// Enable + a single primary sensor (combo) + its response curve + min/max range.
// Extra sensors from an imported preset are preserved across set()/value() even
// though only the primary one is edited here.
class CurveOptionEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveOptionEditor(const QString& title, QWidget* parent = nullptr);

    void set(const CurveOption& o);   // no signal
    CurveOption value() const;

signals:
    void changed();

private:
    CurveOption m_opt;            // backing store (keeps combine + extra sensors)
    AppCheckBox* m_enable = nullptr;
    QComboBox* m_sensor = nullptr;
    ResponseCurveEditor* m_editor = nullptr;
    QSlider* m_min = nullptr;   QLabel* m_minLabel = nullptr;
    QSlider* m_max = nullptr;   QLabel* m_maxLabel = nullptr;
    bool m_loading = false;
};
