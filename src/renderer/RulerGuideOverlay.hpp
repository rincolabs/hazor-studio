#pragma once

#include "core/GuideTypes.hpp"
#include "transform/SnapEngine.hpp"

#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <optional>

class Document;

class RulerGuideOverlay : public QWidget {
public:
    explicit RulerGuideOverlay(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    void setCanvasHalfExtents(const QPointF& halfExtents);
    void setViewportRect(const QRectF& rect);
    void reloadSettings();
    const RulerGuideSettings& settings() const { return m_settings; }
    qreal effectiveRulerSize() const;

    QPointF screenToDocument(const QPointF& screenPos) const;
    QPointF documentToScreen(const QPointF& docPos) const;
    QRectF canvasScreenRect() const;
    QRectF horizontalRulerRect() const;
    QRectF verticalRulerRect() const;
    bool isPointInCanvas(const QPointF& screenPos) const;
    bool hitHorizontalRuler(const QPointF& screenPos) const;
    bool hitVerticalRuler(const QPointF& screenPos) const;
    int hitGuide(const QPointF& screenPos, qreal toleranceScreenPx = 6.0) const;

    void setMouseDocumentPos(const QPointF& pos, bool visible);
    void clearMouseDocumentPos();
    void setGuidePreview(GuideOrientation orientation, qreal position, bool visible);
    void clearGuidePreview();
    void setSelectedGuideIndex(int index);
    void setSnapIndicators(const QVector<SnapIndicator>& indicators);
    void clearSnapIndicators();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal screenPixelsPerDocumentX() const;
    qreal screenPixelsPerDocumentY() const;
    qreal adaptiveMajorSpacing(qreal pixelsPerDocument, qreal configuredSpacing) const;
    QString labelForPosition(qreal pos, qreal axisLength) const;
    void drawRulers(QPainter& p);
    void drawGuides(QPainter& p);
    void drawRulerTicks(QPainter& p, bool horizontal, const QRectF& rect);
    void drawMouseIndicator(QPainter& p);

    Document* m_doc = nullptr;
    QPointF m_canvasHalfExtents{1.0, 1.0};
    QRectF m_viewportRect;
    RulerGuideSettings m_settings;
    std::optional<QPointF> m_mouseDocPos;
    bool m_previewVisible = false;
    GuideOrientation m_previewOrientation = GuideOrientation::Vertical;
    qreal m_previewPosition = 0.0;
    int m_selectedGuideIndex = -1;
    QVector<SnapIndicator> m_snapIndicators;
};
