#include "CanvasView.hpp"
#include "CanvasInputMapper.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/LayerEffect.hpp"
#include "core/BoxSelection.hpp"
#include "transform/TransformController.hpp"
#include "controller/ImageController.hpp"
#include "controller/Commands.hpp"
#include "engine/ImageEngine.hpp"
#include "engine/ColorSamplerService.hpp"
#include "engine/ShapeRenderer.hpp"
#include "shape/ShapeCommands.hpp"
#include "shape/ShapePresetFactory.hpp"
#include "shape/SvgShapeConverter.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "text/TextRenderer.hpp"
#include "text/TextLayoutEngine.hpp"
#include "TileRenderer.hpp"
#include "core/ViewportCamera.hpp"
#include "GPUViewport.hpp"
#include "DocumentCompositor.hpp"
#include "RulerGuideOverlay.hpp"
#include "transform/SnapEngine.hpp"
#include "ui/ShortcutManager.hpp"
#include "ai/tool/AiObjectSelectionController.hpp"
#include "ai/tool/AiRemoveObjectController.hpp"
#include "io/ImageCodec.hpp"

#include <QMatrix4x4>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QUrl>
#include <QByteArray>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QBitmap>
#include <QRegion>
#include <QTransform>
#include <QApplication>
#include <QLayout>
#include <QScrollBar>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

static bool snapDebugEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("IMAGE_EDITOR_SNAP_DEBUG").trimmed().toLower();
        return !value.isEmpty() && value != "0" && value != "false" && value != "off";
    }();
    return enabled;
}

// A QMouseEvent the OS synthesised from a pen reports a Pen/Eraser pointer type
// (a physical mouse reports Generic). We drive non-paint tools from tabletEvent
// directly, so such OS-synthetic mouse twins must be dropped to avoid double input.
static bool isPenSyntheticMouse(const QMouseEvent* e)
{
    const auto t = e->pointerType();
    return t == QPointingDevice::PointerType::Pen
        || t == QPointingDevice::PointerType::Eraser;
}

static bool pointInsideMaskBounds(const Layer* layer, const QPointF& imagePos)
{
    if (!layer || layer->maskImage.isNull())
        return false;
    const QRectF bounds(QPointF(layer->maskOrigin), QSizeF(layer->maskImage.size()));
    return imagePos.x() >= bounds.left()
        && imagePos.y() >= bounds.top()
        && imagePos.x() < bounds.right()
        && imagePos.y() < bounds.bottom();
}

static QTransform unitRectToCanvasTransform(const QRectF& canvasBounds)
{
    QTransform xf;
    xf.setMatrix(canvasBounds.width(), 0.0, 0.0,
                 0.0, canvasBounds.height(), 0.0,
                 canvasBounds.left(), canvasBounds.top(), 1.0);
    return xf;
}

static QTransform svgBoundsToCanvasTransform(const QRectF& localBounds, const QRectF& canvasBounds)
{
    if (localBounds.width() == 0.0 || localBounds.height() == 0.0)
        return QTransform();

    const double sx = canvasBounds.width() / localBounds.width();
    const double sy = canvasBounds.height() / localBounds.height();
    QTransform xf;
    xf.setMatrix(sx, 0.0, 0.0,
                 0.0, sy, 0.0,
                 canvasBounds.left() - sx * localBounds.left(),
                 canvasBounds.top() - sy * localBounds.top(),
                 1.0);
    return xf;
}

static ShapeData makeShapeDataFromDrag(ShapeToolMode mode,
                                       const QPointF& start,
                                       const QPointF& current,
                                       const QColor& fillColor,
                                       const QColor& strokeColor,
                                       double strokeWidth,
                                       double cornerRadius,
                                       int sides,
                                       bool antiAlias,
                                       const ShapeData* customShapeTemplate,
                                       const QSize& documentSize)
{
    // (docW/2, docH/2): maps canvas [-1,1] space to document pixels so rounded
    // corners can be made circular in pixels, not just in (anisotropic) canvas
    // space. Falls back to square units when the document size is unknown.
    const QPointF canvasToPixelScale(
        std::max(1, documentSize.width()) * 0.5,
        std::max(1, documentSize.height()) * 0.5);

    ShapeData data;
    data.style.fillColor = fillColor;
    data.style.fillEnabled = true;
    data.style.strokeColor = strokeColor;
    data.style.strokeEnabled = true;
    data.style.strokeWidth = std::max(0.0, strokeWidth);
    data.style.antiAlias = antiAlias;

    const QRectF unitBounds(0.0, 0.0, 1.0, 1.0);
    QRectF canvasBounds(start, current);
    canvasBounds = canvasBounds.normalized();

    switch (mode) {
    case ShapeToolMode::Rectangle: {
        // Build the corners directly in canvas space so they are circular for any
        // aspect ratio (cornerRadius is a canvas/NDC radius, not a local one).
        data.transform.localToCanvas = unitRectToCanvasTransform(canvasBounds);
        data.path = cornerRadius > 0.0
            ? ShapePresetFactory::createRoundedRectangleForTransform(
                  unitBounds, cornerRadius, data.transform.localToCanvas,
                  canvasToPixelScale)
            : ShapePresetFactory::createRectangle(unitBounds);
        data.metadata.presetId = QStringLiteral("rectangle");
        data.metadata.parameters.insert(QStringLiteral("cornerRadius"), cornerRadius);
        data.metadata.parametricEditable = true;
        break;
    }
    case ShapeToolMode::Ellipse:
        data.path = ShapePresetFactory::createEllipse(unitBounds);
        data.transform.localToCanvas = unitRectToCanvasTransform(canvasBounds);
        data.metadata.presetId = QStringLiteral("ellipse");
        break;
    case ShapeToolMode::Line: {
        data.path = ShapePresetFactory::createLine(QPointF(0.0, 0.0), QPointF(1.0, 0.0));
        data.style.fillEnabled = false;
        data.style.strokeEnabled = true;
        QTransform xf;
        xf.setMatrix(current.x() - start.x(), current.y() - start.y(), 0.0,
                     0.0, 1.0, 0.0,
                     start.x(), start.y(), 1.0);
        data.transform.localToCanvas = xf;
        data.metadata.presetId = QStringLiteral("line");
        break;
    }
    case ShapeToolMode::Polygon:
        data.path = ShapePresetFactory::createPolygon(unitBounds, sides);
        data.transform.localToCanvas = unitRectToCanvasTransform(canvasBounds);
        data.metadata.presetId = QStringLiteral("polygon");
        data.metadata.parameters.insert(QStringLiteral("sides"), sides);
        data.metadata.parametricEditable = true;
        break;
    case ShapeToolMode::Arrow: {
        ArrowOptions options;
        data.path = ShapePresetFactory::createArrow(unitBounds, options);
        data.transform.localToCanvas = unitRectToCanvasTransform(canvasBounds);
        data.metadata.presetId = QStringLiteral("arrow");
        data.metadata.parameters.insert(QStringLiteral("headLengthRatio"), options.headLengthRatio);
        data.metadata.parameters.insert(QStringLiteral("headWidthRatio"), options.headWidthRatio);
        data.metadata.parameters.insert(QStringLiteral("bodyWidthRatio"), options.bodyWidthRatio);
        data.metadata.parameters.insert(QStringLiteral("doubleHead"), options.doubleHead);
        data.metadata.parametricEditable = true;
        break;
    }
    case ShapeToolMode::Star: {
        StarOptions options;
        data.path = ShapePresetFactory::createStar(unitBounds, options);
        data.transform.localToCanvas = unitRectToCanvasTransform(canvasBounds);
        data.metadata.presetId = QStringLiteral("star");
        data.metadata.parameters.insert(QStringLiteral("points"), options.points);
        data.metadata.parameters.insert(QStringLiteral("innerRadiusRatio"), options.innerRadiusRatio);
        data.metadata.parametricEditable = true;
        break;
    }
    case ShapeToolMode::CustomShape:
        if (customShapeTemplate) {
            data.path = customShapeTemplate->path;
            data.metadata = customShapeTemplate->metadata;
            const QRectF localBounds = data.path.localBounds.isValid()
                ? data.path.localBounds
                : unitBounds;
            data.transform.localToCanvas = svgBoundsToCanvasTransform(localBounds, canvasBounds);
        } else {
            data.path = {};
            data.metadata.presetId = QStringLiteral("custom-svg-icon");
        }
        break;
    }

    return data;
}

// Snap the segment (from -> to) to the nearest multiple of 45 degrees while
// preserving the original length. Used by the polygonal lasso when Shift is held.
static QPointF constrainAngle45(const QPointF& from, const QPointF& to)
{
    QPointF v = to - from;
    double len = std::sqrt(v.x() * v.x() + v.y() * v.y());
    if (len < 1e-6)
        return to;
    constexpr double kPi = 3.14159265358979323846;
    double angle = std::atan2(v.y(), v.x());
    double step = kPi / 4.0;
    double snapped = std::round(angle / step) * step;
    return QPointF(from.x() + std::cos(snapped) * len,
                   from.y() + std::sin(snapped) * len);
}

static QColor sampleCanvasBilinear(const QImage& image, float x, float y)
{
    if (image.isNull() || x < 0.0f || y < 0.0f
        || x > static_cast<float>(image.width() - 1)
        || y > static_cast<float>(image.height() - 1)) {
        return Qt::transparent;
    }

    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, image.width() - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, image.height() - 1);
    const int x1 = std::min(x0 + 1, image.width() - 1);
    const int y1 = std::min(y0 + 1, image.height() - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    const QColor c00 = image.pixelColor(x0, y0);
    const QColor c10 = image.pixelColor(x1, y0);
    const QColor c01 = image.pixelColor(x0, y1);
    const QColor c11 = image.pixelColor(x1, y1);

    auto channel = [&](auto getter) {
        const float a = lerp(getter(c00), getter(c10), tx);
        const float b = lerp(getter(c01), getter(c11), tx);
        return std::clamp(static_cast<int>(std::round(lerp(a, b, ty))), 0, 255);
    };

    return QColor(channel([](const QColor& c) { return static_cast<float>(c.red()); }),
                  channel([](const QColor& c) { return static_cast<float>(c.green()); }),
                  channel([](const QColor& c) { return static_cast<float>(c.blue()); }),
                  channel([](const QColor& c) { return static_cast<float>(c.alpha()); }));
}

class SelectionDragOverlay : public QWidget {
public:
    enum class Shape { None, Rect, Ellipse, Lasso, PolyLasso };

    explicit SelectionDragOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        m_anim.setInterval(66);
        connect(&m_anim, &QTimer::timeout, this, [this]() { update(); });
        hide();
    }

    void beginRect(const QPointF& start, bool ellipse)
    {
        m_shape = ellipse ? Shape::Ellipse : Shape::Rect;
        m_start = start;
        m_current = start;
        m_points.clear();
        show();
        raise();
        if (!m_time.isValid())
            m_time.start();
        if (!m_anim.isActive())
            m_anim.start();
        update();
    }

    void setScreenRect(const QRectF& rect)
    {
        m_shape = Shape::Rect;
        m_start = rect.topLeft();
        m_current = rect.bottomRight();
        m_points.clear();
        show();
        raise();
        if (!m_time.isValid())
            m_time.start();
        if (!m_anim.isActive())
            m_anim.start();
        update();
    }

    void updateRect(const QPointF& current)
    {
        m_current = current;
        update();
    }

    void beginLasso(const QPointF& start)
    {
        m_shape = Shape::Lasso;
        m_points.clear();
        m_points.push_back(start);
        show();
        raise();
        if (!m_time.isValid())
            m_time.start();
        if (!m_anim.isActive())
            m_anim.start();
        update();
    }

    void addLassoPoint(const QPointF& point)
    {
        m_points.push_back(point);
        update();
    }

    // Replaces the whole freehand path (re-projection after mid-gesture zoom).
    void setLassoPath(const QVector<QPointF>& points)
    {
        m_shape = Shape::Lasso;
        m_points = points;
        update();
    }

    // Polygonal lasso: confirmed vertices plus a temporary line to the cursor.
    // All points are in overlay/screen coordinates.
    void setPolyLasso(const QVector<QPointF>& points, const QPointF& preview,
                      bool canClose)
    {
        m_shape = Shape::PolyLasso;
        m_points = points;
        m_previewPoint = preview;
        m_canClose = canClose;
        show();
        raise();
        if (!m_time.isValid())
            m_time.start();
        if (!m_anim.isActive())
            m_anim.start();
        update();
    }

    void finish()
    {
        m_shape = Shape::None;
        m_points.clear();
        m_anim.stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_shape == Shape::None) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        qreal offset = m_time.isValid()
            ? std::fmod(static_cast<qreal>(m_time.elapsed()) / 80.0, 8.0)
            : 0.0;
        QPen dark(QColor(0, 0, 0, 220), 1.0);
        dark.setDashPattern({4.0, 4.0});
        dark.setDashOffset(offset);
        QPen light(QColor(255, 255, 255, 235), 1.0);
        light.setDashPattern({4.0, 4.0});
        light.setDashOffset(offset + 4.0);
        QBrush fill(QColor(80, 145, 255, 32));

        auto drawShape = [&](const QPen& pen) {
            p.setPen(pen);
            p.setBrush(fill);
            if (m_shape == Shape::Rect) {
                p.drawRect(QRectF(m_start, m_current).normalized());
            } else if (m_shape == Shape::Ellipse) {
                p.drawEllipse(QRectF(m_start, m_current).normalized());
            } else if (m_shape == Shape::Lasso && m_points.size() > 1) {
                QPainterPath path(m_points.front());
                for (int i = 1; i < m_points.size(); ++i)
                    path.lineTo(m_points[i]);
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            } else if (m_shape == Shape::PolyLasso && !m_points.isEmpty()) {
                QPainterPath path(m_points.front());
                for (int i = 1; i < m_points.size(); ++i)
                    path.lineTo(m_points[i]);
                path.lineTo(m_previewPoint);  // temporary rubber-band line
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
        };

        drawShape(dark);
        drawShape(light);

        if (m_shape == Shape::PolyLasso && !m_points.isEmpty()) {
            // Vertex handles.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 235));
            for (const QPointF& pt : m_points)
                p.drawEllipse(pt, 2.5, 2.5);

            // Highlight the first vertex; turns green when a click there would
            // close the polygon.
            const qreal r = m_canClose ? 6.0 : 4.0;
            QPen hp(m_canClose ? QColor(80, 200, 120, 255)
                               : QColor(80, 145, 255, 235),
                    m_canClose ? 2.0 : 1.5);
            p.setPen(hp);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(m_points.front(), r, r);
        }
    }

private:
    Shape m_shape = Shape::None;
    QPointF m_start;
    QPointF m_current;
    QPointF m_previewPoint;
    bool m_canClose = false;
    QVector<QPointF> m_points;
    QElapsedTimer m_time;
    QTimer m_anim;
};

class ShapePreviewOverlay : public QWidget {
public:
    explicit ShapePreviewOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void setPreview(const QPainterPath& screenPath,
                    const QColor& fillColor,
                    const QColor& strokeColor,
                    qreal strokeWidth,
                    bool antiAlias)
    {
        m_path = screenPath;
        m_fillColor = fillColor;
        m_strokeColor = strokeColor;
        m_strokeWidth = std::max<qreal>(1.0, strokeWidth);
        m_antiAlias = antiAlias;
        show();
        raise();
        update();
    }

    void finish()
    {
        m_path = QPainterPath();
        hide();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_path.isEmpty()) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, m_antiAlias);

        if (m_fillColor.alpha() > 0) {
            QColor fill = m_fillColor;
            fill.setAlpha(std::clamp(fill.alpha() / 2, 24, 128));
            p.fillPath(m_path, fill);
        }

        if (m_strokeColor.alpha() > 0) {
            QPen stroke(m_strokeColor);
            stroke.setWidthF(m_strokeWidth);
            stroke.setCosmetic(true);
            p.strokePath(m_path, stroke);
        }

        QPen outline(QColor(51, 153, 255, 230));
        outline.setWidthF(1.5);
        outline.setStyle(Qt::DashLine);
        outline.setCosmetic(true);
        p.strokePath(m_path, outline);
    }

private:
    QPainterPath m_path;
    QColor m_fillColor;
    QColor m_strokeColor;
    qreal m_strokeWidth = 1.0;
    bool m_antiAlias = true;
};

// Editing overlay for Distort/Perspective. The warped pixels are shown by the
// layer itself (composited normally); this overlay only draws the editable quad
// outline and corner handles on top, in screen-pixel space.
class DistortPreviewOverlay : public QWidget {
public:
    explicit DistortPreviewOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    // dstQuadScreen: the 4 quad corners in widget px (TL,TR,BR,BL).
    void setQuad(const QPolygonF& dstQuadScreen, int activeCorner)
    {
        m_dstQuad = dstQuadScreen;
        m_activeCorner = activeCorner;
        show();
        raise();
        update();
    }

    void finish()
    {
        m_dstQuad.clear();
        hide();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_dstQuad.size() != 4) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Quad outline (dashed, like the other selection overlays).
        QPainterPath path(m_dstQuad[0]);
        for (int i = 1; i < 4; ++i)
            path.lineTo(m_dstQuad[i]);
        path.closeSubpath();

        QPen dark(QColor(0, 0, 0, 200), 1.0);
        dark.setStyle(Qt::DashLine);
        dark.setCosmetic(true);
        p.setPen(dark);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        QPen light(QColor(255, 255, 255, 230), 1.0);
        light.setDashPattern({4.0, 4.0});
        light.setDashOffset(4.0);
        light.setCosmetic(true);
        p.setPen(light);
        p.drawPath(path);

        // Corner handles (filled squares); active corner highlighted.
        for (int i = 0; i < 4; ++i) {
            const QPointF& c = m_dstQuad[i];
            const qreal s = (i == m_activeCorner) ? 5.0 : 4.0;
            QRectF h(c.x() - s, c.y() - s, 2 * s, 2 * s);
            p.setPen(QPen(QColor(0, 0, 0, 220), 1.0));
            p.setBrush(i == m_activeCorner ? QColor(80, 200, 120, 255)
                                           : QColor(255, 255, 255, 235));
            p.drawRect(h);
        }
    }

private:
    QPolygonF m_dstQuad;
    int m_activeCorner = -1;
};

class BrushPreviewOverlay : public QWidget {
public:
    explicit BrushPreviewOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void setPreview(const QPointF& destinationScreen, qreal radius,
                    bool showDestination, bool squareBrush,
                    const QImage& previewImage = QImage(),
                    const QPointF& sourceScreen = QPointF(),
                    bool showSource = false,
                    bool sourceDefined = false,
                    bool showLine = false)
    {
        m_destinationScreen = destinationScreen;
        m_sourceScreen = sourceScreen;
        m_radius = std::max<qreal>(3.0, radius);
        m_showDestination = showDestination;
        m_squareBrush = squareBrush;
        m_previewImage = previewImage;
        m_showSource = showSource;
        m_sourceDefined = sourceDefined;
        m_showLine = showLine;
        m_overlayText.clear();
        m_outlinePath = QPainterPath();
        show();
        raise();
        update();
    }

    // Krita-style cursor: stroke the brush's real silhouette (already mapped to
    // screen space) instead of a filled tip preview. No tip image, no fill.
    void setOutlinePreview(const QPainterPath& screenPath)
    {
        m_outlinePath = screenPath;
        m_showDestination = true;
        m_previewImage = QImage();
        m_showSource = false;
        m_showLine = false;
        m_overlayText.clear();
        show();
        raise();
        update();
    }

    void setAdjustmentPreview(const QPointF& destinationScreen, qreal radius,
                              bool squareBrush, const QString& text)
    {
        m_destinationScreen = destinationScreen;
        m_radius = std::max<qreal>(3.0, radius);
        m_showDestination = true;
        m_squareBrush = squareBrush;
        m_previewImage = QImage();
        m_showSource = false;
        m_sourceDefined = false;
        m_showLine = false;
        m_overlayText = text;
        m_outlinePath = QPainterPath();
        show();
        raise();
        update();
    }

    void clear()
    {
        m_showLine = false;
        m_showDestination = false;
        m_showSource = false;
        m_previewImage = QImage();
        m_overlayText.clear();
        m_outlinePath = QPainterPath();
        hide();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Krita-style silhouette cursor: a THIN cosmetic outline (Krita uses a 1px
        // green cosmetic pen), no fill, no tip preview. A faint 1px dark halo keeps it
        // legible on light backgrounds (the true XOR-against-canvas effect needs the
        // cursor drawn on the canvas surface, not this transparent overlay).
        if (m_showDestination && !m_outlinePath.isEmpty()) {
            p.setBrush(Qt::NoBrush);
            QPen halo(QColor(0, 0, 0, 110), 1.0);
            halo.setCosmetic(true);
            p.setPen(halo);
            p.drawPath(m_outlinePath);
            QPen pen(QColor(128, 255, 128), 1.0);   // Krita's outline green
            pen.setCosmetic(true);
            p.setPen(pen);
            p.drawPath(m_outlinePath);
            return;
        }

        if (m_showDestination) {
            const QRectF destinationRect(m_destinationScreen.x() - m_radius,
                                         m_destinationScreen.y() - m_radius,
                                         m_radius * 2.0,
                                         m_radius * 2.0);
            QPainterPath clip;
            if (m_squareBrush)
                clip.addRect(destinationRect);
            else
                clip.addEllipse(destinationRect);

            p.save();
            p.setClipPath(clip);
            if (!m_previewImage.isNull()) {
                p.setOpacity(0.82);
                p.drawImage(destinationRect, m_previewImage);
                p.setOpacity(1.0);
            } else {
                p.fillPath(clip, QColor(255, 255, 255, 28));
            }
            p.restore();
        }

        auto drawDestination = [&](const QPen& markerPen) {
            if (!m_showDestination)
                return;
            p.setPen(markerPen);
            p.setBrush(Qt::NoBrush);
            const QRectF r(m_destinationScreen.x() - m_radius,
                           m_destinationScreen.y() - m_radius,
                           m_radius * 2.0,
                           m_radius * 2.0);
            if (m_squareBrush)
                p.drawRect(r);
            else
                p.drawEllipse(r);
        };

        QPen destinationHalo(QColor(0, 0, 0, 185), 3.0);
        destinationHalo.setCosmetic(true);
        QPen destinationPen(QColor(255, 255, 255, 235), 1.4);
        destinationPen.setCosmetic(true);
        drawDestination(destinationHalo);
        drawDestination(destinationPen);

        if (m_showSource) {
            const QColor accent = m_sourceDefined ? QColor(80, 200, 120, 230)
                                                  : QColor(240, 170, 60, 230);
            QPen halo(QColor(0, 0, 0, 180), 3.0);
            halo.setCosmetic(true);
            QPen pen(accent, 1.5);
            pen.setCosmetic(true);

            auto drawMarker = [&](const QPen& markerPen) {
                p.setPen(markerPen);
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(m_sourceScreen, m_radius, m_radius);
                p.drawLine(m_sourceScreen + QPointF(-8, 0), m_sourceScreen + QPointF(8, 0));
                p.drawLine(m_sourceScreen + QPointF(0, -8), m_sourceScreen + QPointF(0, 8));
            };
            drawMarker(halo);
            drawMarker(pen);
        }

        if (m_showLine && m_showDestination && m_showSource) {
            QPen linePen(QColor(255, 255, 255, 150), 1.0, Qt::DashLine);
            linePen.setCosmetic(true);
            p.setPen(linePen);
            p.drawLine(m_sourceScreen, m_destinationScreen);
        }

        if (!m_overlayText.isEmpty()) {
            const QFontMetrics fm(font());
            const QStringList lines = m_overlayText.split(QLatin1Char('\n'));
            int textWidth = 0;
            for (const QString& line : lines)
                textWidth = std::max(textWidth, fm.horizontalAdvance(line));
            const int lineHeight = fm.lineSpacing();
            QRectF box(m_destinationScreen + QPointF(18.0, 18.0),
                       QSizeF(textWidth + 18.0, lineHeight * lines.size() + 14.0));
            if (box.right() > width())
                box.moveRight(m_destinationScreen.x() - m_radius - 14.0);
            if (box.bottom() > height())
                box.moveBottom(m_destinationScreen.y() - m_radius - 14.0);
            box = box.normalized();

            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 20, 20, 215));
            p.drawRoundedRect(box, 5.0, 5.0);
            p.setPen(QColor(255, 255, 255, 235));
            QRectF textRect = box.adjusted(9.0, 7.0, -9.0, -7.0);
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_overlayText);
        }
    }

private:
    QPointF m_sourceScreen;
    QPointF m_destinationScreen;
    qreal m_radius = 3.0;
    bool m_sourceDefined = false;
    bool m_showLine = false;
    bool m_showDestination = false;
    bool m_showSource = false;
    bool m_squareBrush = false;
    QString m_overlayText;
    QImage m_previewImage;
    QPainterPath m_outlinePath;
};

class GradientDragOverlay : public QWidget {
public:
    explicit GradientDragOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void setLine(QPointF start, QPointF end, GradientKind kind)
    {
        m_start = start;
        m_end = end;
        m_kind = kind;
        show();
        raise();
        update();
    }

    void finish()
    {
        hide();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!isVisible())
            return;

        auto* theme = ThemeManager::instance()->current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto drawLine = [&](const QPen& pen) {
            painter.setPen(pen);
            painter.drawLine(m_start, m_end);
        };

        QPen halo(QColor(0, 0, 0, 190), 4.0);
        halo.setCosmetic(true);
        QPen line(theme ? theme->colorAccent : QColor(80, 145, 255), 1.8);
        line.setCosmetic(true);
        drawLine(halo);
        drawLine(line);

        const QColor fill = theme ? theme->colorSurface : QColor(30, 30, 30);
        const QColor accent = theme ? theme->colorAccent : QColor(80, 145, 255);
        painter.setPen(QPen(QColor(0, 0, 0, 210), 2.0));
        painter.setBrush(fill);
        painter.drawEllipse(m_start, 5.5, 5.5);
        painter.drawEllipse(m_end, 5.5, 5.5);
        painter.setPen(QPen(accent, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_start, 5.5, 5.5);
        painter.drawEllipse(m_end, 5.5, 5.5);

        if (m_kind == GradientKind::Radial || m_kind == GradientKind::Diamond) {
            const qreal r = QLineF(m_start, m_end).length();
            QPen guide(QColor(255, 255, 255, 125), 1.0, Qt::DashLine);
            guide.setCosmetic(true);
            painter.setPen(guide);
            if (m_kind == GradientKind::Radial)
                painter.drawEllipse(m_start, r, r);
            else {
                QPolygonF diamond;
                diamond << QPointF(m_start.x(), m_start.y() - r)
                        << QPointF(m_start.x() + r, m_start.y())
                        << QPointF(m_start.x(), m_start.y() + r)
                        << QPointF(m_start.x() - r, m_start.y());
                painter.drawPolygon(diamond);
            }
        }
    }

private:
    QPointF m_start;
    QPointF m_end;
    GradientKind m_kind = GradientKind::Linear;
};

CanvasView::CanvasView(Document* doc, ImageController* controller, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_doc(doc)
    , m_controller(controller)
{
    setMouseTracking(true);
    // Deliver pen hover (proximity) events even when no button is pressed, so the
    // brush cursor follows the pen as it floats above the tablet — the same way
    // mouse tracking feeds hover for the mouse.
    setAttribute(Qt::WA_TabletTracking, true);
    // Watch the whole application so the tablet's blank-cursor override (which is
    // application-wide) can be dropped the moment the pen moves off the canvas onto
    // the UI. The canvas's own leaveEvent / tablet proximity-leave are not reliably
    // delivered for pen input, so this is the dependable release trigger.
    if (qApp)
        qApp->installEventFilter(this);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    m_inputDebug = qEnvironmentVariableIntValue("HAZOR_INPUT_DEBUG") != 0;

    m_overscrollEnabled = core::ViewportCamera::overscrollEnabled();

    // Overlay scrollbars hugging the right / bottom edges. They share the exact
    // pan limits the Hand tool uses (ViewportCamera::panBounds), so dragging
    // either one keeps the canvas inside the same navigable region — including
    // the extra margin when overscroll is enabled.
    m_hScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_vScrollBar = new QScrollBar(Qt::Vertical, this);
    m_hScrollBar->setCursor(Qt::ArrowCursor);
    m_vScrollBar->setCursor(Qt::ArrowCursor);
    m_hScrollBar->hide();
    m_vScrollBar->hide();
    connect(m_hScrollBar, &QScrollBar::valueChanged, this, [this](int) { onScrollBarMoved(); });
    connect(m_vScrollBar, &QScrollBar::valueChanged, this, [this](int) { onScrollBarMoved(); });

    m_eyedropperOverlay = nullptr;
    m_selectDragOverlay = new SelectionDragOverlay(this);
    m_selectDragOverlay->setGeometry(rect());
    m_shapePreviewOverlay = new ShapePreviewOverlay(this);
    m_shapePreviewOverlay->setGeometry(rect());
    m_distortOverlay = new DistortPreviewOverlay(this);
    m_distortOverlay->setGeometry(rect());
    m_brushPreviewOverlay = new BrushPreviewOverlay(this);
    m_brushPreviewOverlay->setGeometry(rect());
    m_gradientOverlay = new GradientDragOverlay(this);
    m_gradientOverlay->setGeometry(rect());
    QWidget* rulerParent = parentWidget() ? parentWidget() : this;
    m_rulerGuideOverlay = new RulerGuideOverlay(rulerParent);
    m_rulerGuideOverlay->setGeometry(rulerParent == this ? rect() : rulerParent->rect());
    m_rulerGuideOverlay->setDocument(m_doc);
    m_rulerGuideOverlay->raise();
    if (rulerParent != this) {
        rulerParent->setMouseTracking(true);
        rulerParent->installEventFilter(this);
    }

    auto* th = ThemeManager::instance()->current();
    QString overlayQss = QStringLiteral(
        "QLabel { background: %1; color: %2; border: 1px solid %3;"
        " padding: %4px %5px; font-size: %6px; font-family: monospace; }"
    )
    .arg(th->colorBackgroundPrimary.name())
    .arg(th->colorTextPrimary.name())
    .arg(th->colorBorder.name())
    .arg(th->spaceXS)
    .arg(th->spaceSM)
    .arg(th->fontSizeXS);

    m_eyedropperOverlay = new QLabel(this);
    m_eyedropperOverlay->setStyleSheet(overlayQss);
    m_eyedropperOverlay->setVisible(false);
    m_eyedropperOverlay->adjustSize();

    m_transformOverlay = new QLabel(this);
    m_transformOverlay->setStyleSheet(overlayQss);
    m_transformOverlay->setVisible(false);
    m_transformOverlay->adjustSize();

    constexpr int cursorCount = 16;
    for (int i = 0; i < cursorCount; ++i)
        m_cursors[i] = Qt::ArrowCursor;

    static const char* cursorPaths[cursorCount] = {
        ":/icons/cursor-move.png",
        ":/icons/cursor-brush.png",
        ":/icons/cursor-eraser.png",
        ":/icons/cursor-select.png",
        ":/icons/cursor-zoom.png",
        ":/icons/cursor-hand.png",
        ":/icons/cursor-text.png",
        ":/icons/cursor-crop.png",
        ":/icons/cursor-fill.png",  // fill bucket — uses CrossCursor below
        ":/icons/cursor-eyedropper.png",  // eyedropper — uses CrossCursor below
        ":/icons/cursor-shape.png",  // shape — uses CrossCursor below
        ":/icons/cursor-brush.png",  // clone stamp (clone + healing modes)
        ":/icons/cursor-gradient.png",  // gradient — uses CrossCursor below
        ":/icons/cursor-move.png",   // skew (distort edit) — overlay sets handle cursors
        ":/icons/cursor-select-ai.png",  // AI select
        "",  // AI remove — uses CrossCursor below
    };
    m_cursors[8] = Qt::CrossCursor;
    m_cursors[9] = Qt::CrossCursor;
    m_cursors[10] = Qt::CrossCursor;
    m_cursors[11] = Qt::CrossCursor;
    m_cursors[12] = Qt::CrossCursor;
    m_cursors[14] = Qt::CrossCursor;  // AI Object Selection
    m_cursors[15] = Qt::CrossCursor;  // AI Remove
    for (int i = 0; i < cursorCount; ++i) {
        QPixmap pm(cursorPaths[i]);
        if (!pm.isNull())
            m_cursors[i] = QCursor(pm, 0, 0);
    }

    // Per-SelectType cursors (Rectangular=0 … PolygonalLasso=6).
    // QuickSelect uses BlankCursor (brush-preview circle acts as cursor).
    static const char* selectCursorPaths[7] = {
        ":/icons/cursor-select.png",   // Rectangular
        ":/icons/cursor-select-elliptical.png",   // Elliptical
        ":/icons/cursor-select-lasso.png",   // Lasso
        ":/icons/cursor-select-magic-wand.png",   // MagicWand
        "",                            // QuickSelect — BlankCursor
        ":/icons/cursor-select-magnetic-lasso.png",   // MagneticLasso
        ":/icons/cursor-select-poligonal-lasso.png",   // PolygonalLasso
    };
    for (int i = 0; i < 7; ++i) {
        if (i == static_cast<int>(SelectType::QuickSelect)) {
            m_selectCursors[i] = Qt::BlankCursor;
            continue;
        }
        QPixmap pm(selectCursorPaths[i]);
        m_selectCursors[i] = pm.isNull() ? QCursor(Qt::CrossCursor) : QCursor(pm, 0, 0);
    }

    connect(&m_textEditor, &TextEditorController::textChanged, this, [this]() {
        rerenderTextLayer();
    });
    connect(&m_textEditor, &TextEditorController::caretChanged, this, [this]() {
        update();
    });

    if (m_controller) {
        connect(m_controller, &ImageController::activeLayerChanged, this, [this](int flatIndex) {
            if (m_doc && m_doc->selectedFlatIndices.size() <= 1)
                invalidateMultiOutlineCache();
            if (m_freeTransformActive && flatIndex != m_freeTransformFlatIndex) {
                cancelFreeTransform();
            }
            // Switching to another layer cancels the in-progress distort (no auto-
            // commit) so the previous layer is restored. With the Skew tool active,
            // immediately begin a fresh session on the newly selected layer — the
            // current Distort/Perspective mode persists across the switch.
            if (m_currentTool == Tool::Skew) {
                if (m_distortActive && flatIndex != m_distortFlatIndex)
                    cancelDistort();
                if (!m_distortActive)
                    beginDistort(m_distortMode);
            } else if (m_distortActive && flatIndex != m_distortFlatIndex) {
                cancelDistort();
            }
            if (m_gradientDragging) {
                cancelGradientDrag();
            }
            // A group keeps whatever tool is active — paint/fill/gradient tools
            // safely no-op on a group (their active-layer guards return early),
            // while Move transforms the group as a unit. No auto-switch to Move.
            // Selecting another layer (by row OR pixel thumbnail) always returns
            // the edit target to pixels — the previous layer's mask never stays
            // active. Re-entering mask editing requires an explicit click on the
            // new layer's mask thumbnail, which re-enables it right after this
            // reset runs (selection is emitted synchronously before that).
            if (m_editingMask)
                setEditingMask(false);
        });
        connect(m_controller, &ImageController::layerChanged, this, [this](int flatIndex) {
            if (m_editingMask && flatIndex == (m_doc ? m_doc->activeFlatIndex : -1)) {
                auto* layer = m_doc ? m_doc->activeLayer() : nullptr;
                if (!layer || layer->maskImage.isNull())
                    setEditingMask(false);
            }
        });
        connect(m_controller, &ImageController::selectionChanged, this, [this]() {
            invalidateMultiOutlineCache();
            // Controller/menu/agent/undo selection edits must re-upload the
            // marching-ants texture and (re)start the animation timer — the
            // canvas gestures are not the only mutation path.
            markSelectMaskDirty();
        });
        // Undo/redo restores node transforms without touching the selection,
        // so the cached multi-selection frame would go stale (handles drawn at
        // the pre-undo position, next gesture starting from a stale pivot).
        // Invalidate and let paintGL recompute it fresh. Guarded against
        // mid-gesture invalidation (gestures keep the cache updated live).
        connect(m_controller, &ImageController::documentChanged, this, [this]() {
            if (!m_moving)
                invalidateMultiOutlineCache();
        });
        // Keep the distort outline in sync when undo/redo (or any external edit)
        // changes the active distort layer's quad while a session is open.
        connect(m_controller, &ImageController::imageChanged, this, [this]() {
            if (!m_distortActive || m_distortDragCorner >= 0) return;
            auto* node = m_doc ? m_doc->nodeAt(m_distortFlatIndex) : nullptr;
            if (node && node->layer && node->layer->distortData) {
                m_distortQuad = node->layer->distortData->quad;
                m_distortLastValidQuad = m_distortQuad;
                updateDistortOverlay();
            }
        });
        connect(m_controller, &ImageController::maskEditingChanged, this, [this](bool editing) {
            setEditingMask(editing);
        });
        connect(m_controller, &ImageController::maskOverlayChanged, this, [this]() {
            update();
        });
    }

    m_previewRenderer = new processing::PreviewRenderer(this);
    connect(m_previewRenderer, &processing::PreviewRenderer::previewReady,
            this, &CanvasView::onPreviewReady);

    m_selectAnimTimer = new QTimer(this);
    m_selectAnimTimer->setInterval(66); // ~15fps for marching ants
    connect(m_selectAnimTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible()) { m_selectAnimTimer->stop(); return; }
        if (m_doc && m_doc->selection.active() && !m_doc->selection.isEmpty())
            update();
        else
            m_selectAnimTimer->stop();
    });

    // Default brush: pen pressure → size on (a Pressure sensor on
    // sizeOption), so an untouched brush behaves like before ("pressure works as it
    // does today"). The "Use Pen Pressure" toggle and presets drive this afterwards.
    setBrushPressureEnabled(true);
}

static bool isSupportedDropImagePath(const QString& path)
{
    return imageCodecRegistry().findReader(path) != nullptr;
}

static QStringList extractValidDropImagePaths(const QMimeData* mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    const auto urls = mime->urls();
    for (const QUrl& u : urls) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        if (isSupportedDropImagePath(path))
            out.push_back(path);
    }
    return out;
}

CanvasView::~CanvasView()
{
    if (qApp)
        qApp->removeEventFilter(this);
    releaseTabletViewportCursor();
    makeCurrent();
    m_brushRenderer.destroyGL();
    delete m_gpuViewport;
    m_gpuViewport = nullptr;
    if (m_previewTexture) {
        auto* gl = QOpenGLContext::currentContext()
                       ? QOpenGLContext::currentContext()->functions()
                       : nullptr;
        if (gl) gl->glDeleteTextures(1, &m_previewTexture);
    }
    doneCurrent();
}

void CanvasView::cleanupDocumentLayers()
{
    if (m_gpuViewport) {
        makeCurrent();
        m_gpuViewport->cleanupDocumentLayers(m_doc);
        doneCurrent();
    }
}

void CanvasView::showPreview(const QString& toolName, const QVariantMap& params)
{
    if (!m_doc) { return; }
    auto* node = m_doc->activeNode();
    if (!node || !node->layer) { return; }

    // Use compositeImage() to get the full base-size image with rasterStorage tiles
    // placed at their correct pixel positions (renderImage() returns only tile extents).
    QImage source = node->layer->compositeImage().convertToFormat(QImage::Format_RGBA8888);
    if (source.isNull()) { return; }

    m_previewSourceNode = node;
    m_previewRenderer->generatePreview(source, toolName.toStdString(), params, size());
}

void CanvasView::onPreviewReady(const QImage& previewImage)
{
    if (previewImage.isNull()) { return; }

    QImage result = previewImage;

    // Composite within selection at preview resolution (fast — already downscaled)
    if (m_doc && m_previewSourceNode
        && m_doc->selection.active() && !m_doc->selection.isEmpty()) {
        auto* layer = m_previewSourceNode->layer.get();
        QImage sourceImg = layer->compositeImage().convertToFormat(QImage::Format_RGBA8888);
        int lw = sourceImg.width(), lh = sourceImg.height();
        int pw = previewImage.width(),    ph = previewImage.height();
        if (lw > 0 && lh > 0) {
            cv::Mat maskFull = m_doc->selectionMaskForLayer(
                lw, lh, m_previewSourceNode->accumulatedTransform());
            if (!maskFull.empty()) {
                cv::Mat origSmall, maskSmall;
                cv::Mat origFull = ImageEngine::toCvMatFast(sourceImg);
                cv::resize(origFull,  origSmall, cv::Size(pw, ph), 0, 0, cv::INTER_AREA);
                cv::resize(maskFull,  maskSmall, cv::Size(pw, ph), 0, 0, cv::INTER_AREA);
                cv::Mat previewCv = ImageEngine::toCvMatFast(previewImage);
                previewCv.copyTo(origSmall, maskSmall);
                result = ImageEngine::toQImageFast(origSmall);
            }
        }
    }

    m_previewImage = result;
    makeCurrent();
    if (m_previewTexture == 0)
        glGenTextures(1, &m_previewTexture);
    glBindTexture(GL_TEXTURE_2D, m_previewTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 result.width(), result.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 result.constBits());
    m_hasPreview = true;
    doneCurrent();
    update();
}

void CanvasView::clearPreview()
{
    m_previewRenderer->cancel();
    m_previewSourceNode = nullptr;

    if (!m_hasPreview) return;

    makeCurrent();
    if (m_previewTexture) {
        glDeleteTextures(1, &m_previewTexture);
        m_previewTexture = 0;
    }
    m_hasPreview = false;
    m_previewImage = QImage();
    doneCurrent();
    update();
}

void CanvasView::syncLayersToGpu()
{
    if (!m_gpuViewport) return;
    // During an adjustment-layer drag only the adjustment params change — no
    // layer pixels or masks. The full sync below re-uploads every raster tile
    // (markAllGpuDirty), recomputes+re-uploads every effected image and re-uploads
    // every mask, all per frame, which stalls the UI on large docs. Skip it: the
    // render path's own lazy needsLayerSync still uploads any layer that's
    // genuinely missing/outdated (first frame) and rebuilds a Single-Layer-Mode
    // adjustment's dirty effected texture, so the preview stays correct.
    if (m_adjustmentLiveEdit) {
        update();
        return;
    }
    makeCurrent();
    m_gpuViewport->syncLayersToGpu(m_doc);
    doneCurrent();
}

// ── Temporary contextual tool override (spring-loaded tools) ─────────────────
//
// Some tools temporarily borrow another tool while a modifier/key is held — the
// canonical example is Alt over a paint tool, which samples a colour like the
// Eyedropper. The rule for every such case lives HERE, in one table, so the
// behaviour is identical across tools and is never duplicated inside individual
// tool code. (Space → Hand is the same pattern, but it is a spring-loaded pan
// implemented via m_spacePanActive and is intentionally left untouched so its
// behaviour stays byte-for-byte identical; this resolver covers the
// modifier-activated overrides.)
std::optional<CanvasView::Tool> CanvasView::resolveTemporaryOverride(
    Qt::KeyboardModifiers mods) const
{
    // A gesture in progress owns the input until it ends — never swap mid-stroke.
    if (m_brushDrawing || m_panning || m_moving || m_distortActive
        || m_shapeDragging || m_gradientDragging || m_cropDragging
        || m_selectDragging || m_lassoDrawing || m_polyLassoDrawing)
        return std::nullopt;

    switch (m_currentTool) {
    case Tool::Brush:
    case Tool::Eraser:
        // Alt over a paint tool acts as the Eyedropper (routed through the real
        // tool, so it honours the eyedropper sample-mode/size and Ctrl/right →
        // background-colour rules instead of a one-off framebuffer grab).
        if (mods & Qt::AltModifier)
            return Tool::Eyedropper;
        break;
    default:
        break;
    }
    return std::nullopt;
}

void CanvasView::refreshTemporaryOverride()
{
    refreshTemporaryOverride(QApplication::keyboardModifiers());
}

void CanvasView::refreshTemporaryOverride(Qt::KeyboardModifiers mods)
{
    const std::optional<Tool> next = resolveTemporaryOverride(mods);
    const bool overrideChanged = (next != m_temporaryOverrideTool);

    // Some tools flip their cursor on a held modifier WITHOUT swapping the effective
    // tool (clone/healing Alt → source crosshair). Those still need a cursor refresh
    // on the modifier transition even though the override itself is unchanged. When
    // neither the override changed nor the active tool is modifier-cursor-sensitive,
    // there is nothing to do.
    if (!overrideChanged && !isCloneOrHealingTool())
        return;

    m_temporaryOverrideTool = next;
    // The eyedropper hover read-out is owned by the eyedropper hover path; if the
    // effective tool is no longer the eyedropper, drop it so it can't linger after
    // the modifier is released.
    if (effectiveTool() != Tool::Eyedropper) {
        m_eyedropperHovering = false;
        if (m_eyedropperOverlay)
            m_eyedropperOverlay->setVisible(false);
    }
    // Purely an interaction/cursor change: no toolChanged, no toolbar/options/
    // history update, no persistent tool state touched. Pass the settled modifier
    // state so the clone crosshair flips on the Alt transition, not on a mouse move.
    updateToolCursor(mods);
    updateBrushPreviewOverlay();
    update();
}

void CanvasView::setTool(Tool t)
{
    // Picking a real tool clears any armed contextual override.
    m_temporaryOverrideTool.reset();

    const Tool prevTool = m_currentTool;

    // A group keeps whatever tool the user picks — paint/fill/gradient tools
    // safely no-op on a group (their active-layer guards return early); Move
    // transforms the group as a unit. No auto-switch to Move.

    // Leaving the Skew tool cancels any in-progress distort session (no auto-
    // commit): the layer is restored to its pre-session state. Commit is explicit
    // (Enter / double-click).
    if (m_distortActive && t != Tool::Skew) {
        cancelDistort();
    }

    if (m_freeTransformActive && t != Tool::Move) {
        commitFreeTransform();
    }

    if (m_textToolState == TextToolState::Editing && t != Tool::Text) {
        commitTextEdit();
    }

    if (m_moving) {
        m_transformState.mode = InteractionMode::Idle;
        m_moving = false;
        m_boxSelecting = false;
        m_snapMoveStartDocumentBoundsValid = false;
        m_snapMoveIndices.clear();
        m_snapMoveStartTransforms.clear();
        clearSnapBoundsCache();
        clearSnapFeedback();
    }
    m_boxSelecting = false;
    // Cancel pending crop when switching away — commit only on explicit user action
    if (m_currentTool == Tool::Crop && m_cropActive && t != Tool::Crop) {
        cancelCrop();
    }

    m_eyedropperHovering = false;
    if (m_eyedropperOverlay)
        m_eyedropperOverlay->setVisible(false);

    // Switching the main tool cancels a pending Hue/Saturation eyedropper so the
    // editor can drop its armed state (no new adjustment applied).
    if (m_hueSatPickMode != 0) {
        setHueSatPickMode(0);
        emit hueSatPickCancelled();
    }

    // Leaving the AI Object Selection tool cancels any in-flight inference and
    // tears down the box-drag feedback so a pending result is not applied later.
    if (prevTool == Tool::AiSelect && t != Tool::AiSelect) {
        if (m_aiSelectController)
            m_aiSelectController->cancelPending();
        if (m_aiBoxDragging && m_selectDragOverlay)
            m_selectDragOverlay->finish();
        m_aiBoxDragging = false;
        m_aiMoved = false;
    }

    if (prevTool == Tool::AiRemove && t != Tool::AiRemove) {
        cancelAiRemoveLasso();
        if (m_aiRemoveController)
            m_aiRemoveController->cancel();
    }

    m_currentTool = t;
    m_brushSettings.mode = (t == Tool::Eraser) ? BrushMode::Erase : BrushMode::Paint;
    updateBrushPreviewOverlay();

    // Switching tools must NOT leave mask editing on its own. Only tools that
    // edit layer pixels/geometry are incompatible with the active mask and force
    // the target back to pixels; navigation/selection tools keep the mask.
    // Brush, Eraser, Fill and Gradient are mask-aware (they route to the active
    // mask), so they keep the mask target. Only tools that edit pixels/geometry
    // with no mask path force the target back to pixels.
    const bool maskIncompatibleTool =
        t == Tool::Text || t == Tool::Shape || t == Tool::Crop
        || t == Tool::AiRemove || t == Tool::Skew || t == Tool::CloneStamp;
    if (m_editingMask && maskIncompatibleTool)
        setEditingMask(false);

    if (m_shapeDragging && t != Tool::Shape) {
        cancelShapeDrag();
    }

    if (m_gradientDragging && t != Tool::Gradient) {
        cancelGradientDrag();
    }

    if (m_polyLassoDrawing) {
        cancelPolyLasso();
    }

    if (t != Tool::Text) {
        m_textToolState = TextToolState::Idle;
    }
    if (t == Tool::Crop) {
        resetCrop();
    }
    updateToolCursor();

    // Skew is "distort edit mode": entering it immediately begins a session on
    // the active layer so the handles appear without an extra canvas click. The
    // mode persists while Skew stays active (across layer switches) but resets to
    // Distort whenever the tool is (re-)entered from another tool.
    if (t == Tool::Skew) {
        if (prevTool != Tool::Skew)
            m_distortMode = TransformMode::Distort;
        beginDistort(m_distortMode);
    }

    emit toolChanged(static_cast<int>(t));
    update();
}

void CanvasView::setEditingMask(bool editing)
{
    if (m_settingEditingMask) return;
    m_settingEditingMask = true;

    m_editingMask = editing;
    if (!editing)
        m_grayscaleMaskView = false;
    if (m_controller)
        m_controller->setEditingMask(editing);

    // When entering mask edit mode, make raster masks cover the current layer
    // pixel bounds. Shape masks are transformed with the vector layer, so entering
    // edit mode must not destructively resample them to the current raster cache.
    if (editing && m_doc && m_gpuViewport && m_controller) {
        auto* node = m_doc->activeNode();
        auto* layer = m_doc->activeLayer();
        if (layer && !layer->maskImage.isNull()) {
            const QSize oldMaskSize = layer->maskImage.size();
            QSize targetSize = oldMaskSize;
            QPoint newOrigin = layer->maskOrigin;
            if (layer->isShapeLayer()) {
                // keep targetSize as oldMaskSize
            } else if (layer->renderRasterStorage().isEnabled()) {
                const QRect targetBounds = layer->maskTargetBounds();
                targetSize = targetBounds.size();
                newOrigin  = targetBounds.topLeft();
            } else {
                targetSize = layer->cpuImage.size();
                // Flat layers: coordinate system always anchored at (0,0).
                newOrigin = QPoint(0, 0);
            }

            if ((targetSize != oldMaskSize || newOrigin != layer->maskOrigin) && !targetSize.isEmpty()) {
                // Sync GPU→CPU first so any painted-but-not-synced strokes are included.
                makeCurrent();
                if (m_controller)
                    m_controller->syncLayerMaskFromGpu(layer);
                // Offset of old mask content in new mask pixel space.
                const int offX = layer->maskOrigin.x() - newOrigin.x();
                const int offY = layer->maskOrigin.y() - newOrigin.y();
                QImage newMask(targetSize, QImage::Format_Grayscale8);
                newMask.fill(255);
                QPainter mp(&newMask);
                mp.drawImage(offX, offY, layer->maskImage);
                mp.end();
                layer->maskImage = newMask;
                layer->maskOrigin = newOrigin;
                m_controller->syncLayerMaskToGpu(layer);
                m_gpuViewport->setupMaskFBO(layer);
                doneCurrent();
            }
        }
    }

    m_settingEditingMask = false;
    updateToolCursor();
    update();
}

void CanvasView::setBrushSize(float s)
{
    m_brushSettings.size = clampedBrushSize(s);
    m_brushRenderer.updateStamp(m_brushSettings);
    updateBrushPreviewOverlay();
}

void CanvasView::setBrushHardness(float h)
{
    m_brushSettings.hardness = clampedBrushHardness(h);
    m_brushRenderer.updateStamp(m_brushSettings);
    updateBrushPreviewOverlay();
}

bool CanvasView::isBrushBasedTool() const
{
    return m_currentTool == Tool::Brush
        || m_currentTool == Tool::Eraser
        || isCloneOrHealingTool();
}

float CanvasView::clampedBrushSize(float size) const
{
    return std::clamp(size, 1.0f, 5000.0f);
}

float CanvasView::clampedBrushHardness(float hardness) const
{
    return std::clamp(hardness, 0.0f, 1.0f);
}

void CanvasView::applyBrushSizeFromShortcut(float size)
{
    const float clamped = clampedBrushSize(size);
    if (qFuzzyCompare(clamped, m_brushSettings.size))
        return;
    setBrushSize(clamped);
    emit brushSizeChanged(clamped);
    update();
}

void CanvasView::applyBrushHardnessFromShortcut(float hardness)
{
    const float clamped = clampedBrushHardness(hardness);
    if (qFuzzyCompare(clamped, m_brushSettings.hardness))
        return;
    setBrushHardness(clamped);
    emit brushHardnessChanged(clamped);
    update();
}

void CanvasView::adjustBrushSizeByStep(int direction)
{
    const float size = m_brushSettings.size;
    const float step = size < 50.0f ? 1.0f : (size < 200.0f ? 5.0f : 10.0f);
    applyBrushSizeFromShortcut(size + step * static_cast<float>(direction));
}

void CanvasView::adjustBrushHardnessByStep(int direction)
{
    applyBrushHardnessFromShortcut(m_brushSettings.hardness
                                   + 0.10f * static_cast<float>(direction));
}

bool CanvasView::handleBrushShortcutKey(QKeyEvent* event)
{
    // Quick Selection reuses the shared brush size (its dab radius), so the
    // size shortcuts apply there too — but it has no hardness, so those stay
    // exclusive to the paint brushes.
    const bool quickSelect = m_currentTool == Tool::Select
                          && m_selectType == SelectType::QuickSelect;
    if (!isBrushBasedTool() && !quickSelect)
        return false;

    auto* shortcuts = ShortcutManager::instance();
    const QKeySequence keySeq(event->modifiers() | event->key());
    auto matches = [&](const QString& id) {
        const QKeySequence seq = shortcuts->currentShortcut(id);
        return !seq.isEmpty() && seq == keySeq;
    };

    if (matches(QStringLiteral("brush.size_decrease"))) {
        adjustBrushSizeByStep(-1);
    } else if (matches(QStringLiteral("brush.size_increase"))) {
        adjustBrushSizeByStep(1);
    } else if (!quickSelect && matches(QStringLiteral("brush.hardness_decrease"))) {
        adjustBrushHardnessByStep(-1);
    } else if (!quickSelect && matches(QStringLiteral("brush.hardness_increase"))) {
        adjustBrushHardnessByStep(1);
    } else {
        return false;
    }

    event->accept();
    return true;
}

bool CanvasView::beginBrushAdjustDrag(QPointF screenPos)
{
    if (!isBrushBasedTool() || !m_doc || !m_brushPreviewOverlay)
        return false;
    if (m_brushDrawing)
        return false;

    m_brushAdjustDragging = true;
    m_brushAdjustStartScreen = screenPos;
    m_brushAdjustCurrentScreen = screenPos;
    m_brushAdjustAccumulatedDelta = QPointF();
    m_brushAdjustLockGlobalPos = mapToGlobal(screenPos.toPoint());
    m_brushAdjustStartSize = m_brushSettings.size;
    m_brushAdjustStartHardness = m_brushSettings.hardness;
    m_lastScreenPos = screenPos;
    setFocus();
    setCursor(Qt::CrossCursor);
    QCursor::setPos(m_brushAdjustLockGlobalPos);
    updateBrushAdjustDrag(QPointF(m_brushAdjustLockGlobalPos));
    return true;
}

void CanvasView::updateBrushAdjustDrag(QPointF globalPos)
{
    if (!m_brushAdjustDragging || !m_doc)
        return;

    const QPointF lockGlobal(m_brushAdjustLockGlobalPos);
    const QPointF frameDelta = globalPos - lockGlobal;
    m_brushAdjustAccumulatedDelta += frameDelta;
    if (QCursor::pos() != m_brushAdjustLockGlobalPos)
        QCursor::setPos(m_brushAdjustLockGlobalPos);

    m_brushAdjustCurrentScreen = m_brushAdjustStartScreen;
    m_lastScreenPos = m_brushAdjustStartScreen;
    const QPointF delta = m_brushAdjustAccumulatedDelta;
    const float newSize = clampedBrushSize(m_brushAdjustStartSize
                                           + static_cast<float>(delta.x()) * 0.5f);
    const float newHardness = clampedBrushHardness(m_brushAdjustStartHardness
                                                   - static_cast<float>(delta.y()) * 0.005f);
    applyBrushSizeFromShortcut(newSize);
    applyBrushHardnessFromShortcut(newHardness);

    const qreal radius = std::max<qreal>(3.0, m_brushSettings.size * m_doc->zoom * 0.5);
    const bool squareBrush = m_brushSettings.type == BrushType::Square;
    const QString text = tr("Brush Size: %1 px\nHardness: %2%")
        .arg(qRound(m_brushSettings.size))
        .arg(qRound(m_brushSettings.hardness * 100.0f));
    m_brushPreviewOverlay->setAdjustmentPreview(m_brushAdjustStartScreen, radius, squareBrush, text);
    update();
}

void CanvasView::finishBrushAdjustDrag(bool cancel)
{
    if (!m_brushAdjustDragging)
        return;

    m_brushAdjustDragging = false;
    if (cancel) {
        applyBrushSizeFromShortcut(m_brushAdjustStartSize);
        applyBrushHardnessFromShortcut(m_brushAdjustStartHardness);
    }
    updateBrushPreviewOverlay();
    updateToolCursor();
    update();
}

void CanvasView::setBrushOpacity(float o)
{
    m_brushSettings.opacity = o;
    updateBrushPreviewOverlay();
}

void CanvasView::setBrushFlow(float f)
{
    m_brushSettings.flow = f;
    updateBrushPreviewOverlay();
}

void CanvasView::setBrushType(BrushType t)
{
    m_brushSettings.type = t;
    m_brushRenderer.updateStamp(m_brushSettings);
    updateBrushPreviewOverlay();
}

void CanvasView::setBrushTipSource(BrushTipSource source)
{
    m_brushSettings.tipSource = source;
    m_brushRenderer.updateStamp(m_brushSettings);
    updateBrushPreviewOverlay();
}

void CanvasView::setBrushTipImage(const QImage& img)
{
    m_brushSettings.tipSource = BrushTipSource::Image;
    m_brushSettings.tipImage = img;
    m_brushRenderer.updateStamp(m_brushSettings);
    updateBrushPreviewOverlay();
}

void CanvasView::setBrushTexture(const QImage& tex)
{
    m_brushSettings.textureConfig.texture = tex;
    if (tex.isNull())
        m_brushRenderer.clearTexture();
    else
        m_brushRenderer.setTextureImage(tex);
}

void CanvasView::setBrushTextureConfig(const TextureConfig& config)
{
    m_brushSettings.textureConfig = config;
    if (config.texture.isNull())
        m_brushRenderer.clearTexture();
    else
        m_brushRenderer.setTextureImage(config.texture);
}

void CanvasView::setBrushBlendMode(BrushBlendMode m)
{
    m_brushSettings.blendMode = m;
    updateBrushPreviewOverlay();
}

void CanvasView::setGradientDefinition(const GradientDefinition& definition)
{
    m_gradientDefinition = definition;
    m_gradientDefinition.normalize();
    if (m_gradientDragging && m_gradientOverlay)
        m_gradientOverlay->setLine(m_gradientStartScreen, m_gradientCurrentScreen,
                                   m_gradientDefinition.kind);
}

void CanvasView::setCloneSampleMode(CloneSampleMode mode)
{
    if (m_cloneSampleMode == mode)
        return;
    m_cloneSampleMode = mode;
    invalidateClonePreviewCache();
    updateBrushPreviewOverlay();
    update();
}

void CanvasView::setCloneAligned(bool aligned)
{
    m_cloneAligned = aligned;
    if (!aligned)
        m_cloneOffsetValid = false;
    updateBrushPreviewOverlay();
    update();
}

void CanvasView::setCloneSource(QPointF documentPoint)
{
    m_cloneSourcePoint = documentPoint;
    m_cloneSourceDefined = true;
    m_cloneOffsetValid = false;
    m_cloneNeedsSourceFeedback = false;
    invalidateClonePreviewCache();
    updateToolCursor();
    updateBrushPreviewOverlay();
    update();
}

bool CanvasView::canPaintActiveRasterLayer() const
{
    if (!m_doc)
        return false;
    auto* node = m_doc->activeNode();
    auto* layer = m_doc->activeLayer();
    if (!node || !layer || node->type != LayerTreeNode::Type::Layer)
        return false;
    if (!node->isVisible())
        return false;
    if (node->isPixelEditingLocked())
        return false;
    if (layer->isTextLayer() || layer->isShapeLayer())
        return false;
    return true;
}

bool CanvasView::ensurePaintTargetLayer(const QString& undoName)
{
    if (!m_doc || !m_controller)
        return false;
    // No open document / closed canvas: nothing to paint on and nothing to create.
    // An empty size with no layers yet (brand new document) is fine: newLayer()
    // below picks a fallback size and stamps it onto the document.
    if (m_doc->size.isEmpty() && m_doc->flatCount() > 0)
        return false;

    // A mode currently owns the canvas → the action must not run (and we must not
    // silently spawn a layer behind a transform/crop/text-edit/distort session).
    if (m_cropActive)
        return false;
    if (m_textToolState == TextToolState::Editing)
        return false;
    if (m_transformState.mode != InteractionMode::Idle)
        return false;
    if (m_freeTransformActive || m_distortActive)
        return false;

    // A layer (pixel/text/shape) or adjustment is already selected: don't
    // auto-create. The tool's own gate (checkDestructiveOp / lock checks /
    // rasterization rules) decides whether the action proceeds or is blocked.
    if (m_doc->activeLayer())
        return true;

    // Nothing editable is selected (empty document, deselected, or a group is
    // active): create a fresh transparent, document-sized pixel layer above the
    // active node (or at the top), select it, and fold its creation into the same
    // undo step as the paint action that follows.
    m_controller->history().beginMacro(undoName);
    m_controller->newLayer();   // transparent, doc-sized, selected; pushes AddLayer
    return true;
}

QImage CanvasView::cloneSourceSnapshot() const
{
    if (!m_doc || m_doc->size.isEmpty())
        return QImage();

    RenderContext ctx;
    ctx.document = m_doc;
    ctx.outputSize = m_doc->size;
    ctx.documentRect = QRectF(QPointF(0, 0), m_doc->size);
    ctx.targetType = RenderTargetType::Canvas;
    ctx.highQuality = true;

    switch (m_cloneSampleMode) {
    case CloneSampleMode::CurrentLayer:
        return DocumentCompositor::compositeOnlyFlatIndex(m_doc, m_doc->activeFlatIndex, ctx);
    case CloneSampleMode::CurrentAndBelow:
        return DocumentCompositor::compositeFromFlatIndex(m_doc, m_doc->activeFlatIndex, ctx);
    case CloneSampleMode::AllLayers:
    default:
        return DocumentCompositor::composite(m_doc, ctx);
    }
}

void CanvasView::invalidateClonePreviewCache()
{
    m_clonePreviewSourceImage = QImage();
    m_clonePreviewSourceGeneration = std::numeric_limits<uint64_t>::max();
    m_clonePreviewSourceFlatIndex = -1;
}

QImage CanvasView::clonePreviewSourceSnapshot()
{
    if (!m_doc)
        return QImage();

    const bool cacheValid = !m_clonePreviewSourceImage.isNull()
        && m_clonePreviewSourceGeneration == m_doc->compositionGeneration
        && m_clonePreviewSourceFlatIndex == m_doc->activeFlatIndex
        && m_clonePreviewSourceMode == m_cloneSampleMode;
    if (cacheValid)
        return m_clonePreviewSourceImage;

    m_clonePreviewSourceImage = cloneSourceSnapshot().convertToFormat(QImage::Format_RGBA8888);
    m_clonePreviewSourceGeneration = m_doc->compositionGeneration;
    m_clonePreviewSourceFlatIndex = m_doc->activeFlatIndex;
    m_clonePreviewSourceMode = m_cloneSampleMode;
    return m_clonePreviewSourceImage;
}

QImage CanvasView::cloneStampPreviewImage(QPointF destinationScreen,
                                          QPointF destinationDoc,
                                          QPointF sourceDoc,
                                          const QImage& sourceImage)
{
    if (!m_doc || sourceImage.isNull())
        return QImage();

    // Match the painted dab footprint. size is a DIAMETER, so the painted radius is
    // size*0.5 in doc px → size*zoom*0.5 on screen — keeps the clone-stamp source
    // preview aligned with the cursor circle.
    const qreal radiusScreen = std::max<qreal>(3.0, m_brushSettings.size * m_doc->zoom * 0.5);
    const int side = std::clamp(static_cast<int>(std::ceil(radiusScreen * 2.0)), 8, 192);
    const qreal previewScale = side / std::max<qreal>(1.0, radiusScreen * 2.0);
    const qreal center = side * 0.5;
    const qreal previewRadius = side * 0.5;
    const float hardness = std::clamp(m_brushSettings.hardness, 0.0f, 1.0f);
    const float aa = std::min(0.25f, 1.5f / static_cast<float>(std::max<qreal>(1.0, previewRadius)));
    const float displayAlpha = std::clamp(0.35f + m_brushSettings.opacity * 0.5f, 0.35f, 0.85f);
    const bool squareBrush = m_brushSettings.type == BrushType::Square;
    const QImage source = sourceImage.format() == QImage::Format_RGBA8888
        ? sourceImage
        : sourceImage.convertToFormat(QImage::Format_RGBA8888);
    QImage tipAlpha;
    if (m_brushSettings.tipSource == BrushTipSource::Image && !m_brushSettings.tipImage.isNull()) {
        // Cached per tip/size: rescaling a large source tip on every pointer
        // event is far too expensive at tablet event rates.
        const qint64 key = m_brushSettings.tipImage.cacheKey();
        if (m_cloneTipPreviewKey != key || m_cloneTipPreviewSide != side
            || m_cloneTipPreviewScaled.isNull()) {
            m_cloneTipPreviewScaled = m_brushSettings.tipImage.scaled(side, side,
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGBA8888);
            m_cloneTipPreviewKey = key;
            m_cloneTipPreviewSide = side;
        }
        tipAlpha = m_cloneTipPreviewScaled;
    }

    QImage preview(side, side, QImage::Format_ARGB32);
    preview.fill(Qt::transparent);

    for (int y = 0; y < side; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(preview.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const qreal dxScreen = (static_cast<qreal>(x) + 0.5 - center) / previewScale;
            const qreal dyScreen = (static_cast<qreal>(y) + 0.5 - center) / previewScale;
            float maskAlpha = 0.0f;
            if (!tipAlpha.isNull()) {
                maskAlpha = qAlpha(reinterpret_cast<const QRgb*>(
                    tipAlpha.constScanLine(y))[x]) / 255.0f;
            } else {
                const qreal nx = (static_cast<qreal>(x) + 0.5 - center) / previewRadius;
                const qreal ny = (static_cast<qreal>(y) + 0.5 - center) / previewRadius;
                const float d = squareBrush
                    ? static_cast<float>(std::max(std::abs(nx), std::abs(ny)))
                    : static_cast<float>(std::sqrt(nx * nx + ny * ny));
                maskAlpha = brushFalloff(d, hardness, aa);
            }
            if (maskAlpha <= 0.0f)
                continue;

            const QPointF mappedDoc = screenToDocument(destinationScreen + QPointF(dxScreen, dyScreen));
            const QPointF sourcePixel = sourceDoc + (mappedDoc - destinationDoc);
            const QColor sampled = sampleCanvasBilinear(source,
                static_cast<float>(sourcePixel.x()),
                static_cast<float>(sourcePixel.y()));
            const int alpha = std::clamp(static_cast<int>(
                std::round(sampled.alpha() * maskAlpha * displayAlpha)), 0, 255);
            if (alpha <= 0)
                continue;
            row[x] = qRgba(sampled.red(), sampled.green(), sampled.blue(), alpha);
        }
    }

    return preview;
}

QImage CanvasView::brushTipPreviewImage(int side) const
{
    if (side <= 0)
        return QImage();

    QImage preview(side, side, QImage::Format_ARGB32);
    preview.fill(Qt::transparent);

    const bool erasing = m_brushSettings.mode == BrushMode::Erase;
    QColor tint = erasing ? QColor(255, 255, 255) : m_brushSettings.color;
    if (!tint.isValid())
        tint = QColor(255, 255, 255);

    const float displayAlpha = std::clamp(0.30f + m_brushSettings.opacity * 0.55f,
                                          0.30f, 0.85f);

    if (m_brushSettings.tipSource == BrushTipSource::Image && !m_brushSettings.tipImage.isNull()) {
        QImage scaled = m_brushSettings.tipImage.scaled(side, side,
            Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGBA8888);

        bool hasAlpha = false;
        for (int y = 0; y < scaled.height() && !hasAlpha; ++y) {
            const uchar* src = scaled.constScanLine(y);
            for (int x = 0; x < scaled.width() && !hasAlpha; ++x)
                hasAlpha = src[x * 4 + 3] < 255;
        }

        const int ox = (side - scaled.width()) / 2;
        const int oy = (side - scaled.height()) / 2;
        for (int y = 0; y < scaled.height(); ++y) {
            const uchar* src = scaled.constScanLine(y);
            QRgb* dst = reinterpret_cast<QRgb*>(preview.scanLine(y + oy));
            for (int x = 0; x < scaled.width(); ++x) {
                int mask = 0;
                if (hasAlpha) {
                    mask = src[x * 4 + 3];
                } else {
                    const int lum = (src[x * 4] * 77
                                   + src[x * 4 + 1] * 150
                                   + src[x * 4 + 2] * 29) >> 8;
                    mask = 255 - lum;
                }
                const int alpha = std::clamp(static_cast<int>(std::round(mask * displayAlpha)), 0, 255);
                if (alpha <= 0)
                    continue;
                dst[x + ox] = qRgba(tint.red(), tint.green(), tint.blue(), alpha);
            }
        }
        return preview;
    }

    const qreal center = side * 0.5;
    const qreal radius = std::max<qreal>(1.0, side * 0.5);
    const float hardness = std::clamp(m_brushSettings.hardness, 0.0f, 1.0f);
    const float aa = std::min(0.25f, 1.5f / static_cast<float>(radius));
    const bool squareBrush = m_brushSettings.type == BrushType::Square;

    for (int y = 0; y < side; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(preview.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const qreal nx = (static_cast<qreal>(x) + 0.5 - center) / radius;
            const qreal ny = (static_cast<qreal>(y) + 0.5 - center) / radius;
            const float d = squareBrush
                ? static_cast<float>(std::max(std::abs(nx), std::abs(ny)))
                : static_cast<float>(std::sqrt(nx * nx + ny * ny));
            const float mask = brushFalloff(d, hardness, aa);
            if (mask <= 0.0f)
                continue;
            const int alpha = std::clamp(static_cast<int>(
                std::round(mask * displayAlpha * 255.0f)), 0, 255);
            if (alpha <= 0)
                continue;
            row[x] = qRgba(tint.red(), tint.green(), tint.blue(), alpha);
        }
    }

    return preview;
}

// Normalized brush silhouette, centered at the origin with its larger axis spanning
// 1.0. Traced ONCE per tip (downsampled, like Krita's "simple outline") and cached;
// size/roundness/angle are applied per frame by brushOutlineScreenPath, so the heavy
// boundary tracing never runs on hover.
QPainterPath CanvasView::brushOutlineUnitPath() const
{
    const bool imageTip = m_brushSettings.tipSource == BrushTipSource::Image
                          && !m_brushSettings.tipImage.isNull();
    const qint64 key = imageTip
        ? (m_brushSettings.tipImage.cacheKey() ^ qint64(0x5151))
        : (m_brushSettings.type == BrushType::Square ? 2 : 1);
    if (key == m_brushOutlineKey && !m_brushOutlineUnit.isEmpty())
        return m_brushOutlineUnit;
    m_brushOutlineKey = key;

    QPainterPath unit;
    if (imageTip) {
        // Trace at a bounded resolution so the cost (and blockiness) stay small.
        constexpr int kTrace = 64;
        const QImage a = m_brushSettings.tipImage
            .scaled(kTrace, kTrace, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32);
        const int w = a.width();
        const int h = a.height();
        // Coverage convention matches the engine tip: alpha when present, else
        // inverted luminance (dark = opaque). Threshold to a 1-bit mask.
        bool varies = false;
        for (int y = 0; y < h && !varies; ++y) {
            const QRgb* r = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            for (int x = 0; x < w; ++x)
                if (qAlpha(r[x]) < 255) { varies = true; break; }
        }
        // Build the region from the COVERED pixels directly (run-length per row), so
        // the traced path is the silhouette boundary only — no polarity ambiguity and
        // no enclosing image-rectangle border (which QBitmap inversion produced).
        QRegion covered;
        for (int y = 0; y < h; ++y) {
            const QRgb* r = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            int runStart = -1;
            for (int x = 0; x < w; ++x) {
                const int cov = varies ? qAlpha(r[x]) : (255 - qGray(r[x]));
                const bool on = cov > 32;
                if (on && runStart < 0) {
                    runStart = x;
                } else if (!on && runStart >= 0) {
                    covered += QRect(runStart, y, x - runStart, 1);
                    runStart = -1;
                }
            }
            if (runStart >= 0)
                covered += QRect(runStart, y, w - runStart, 1);
        }
        QPainterPath traced;
        traced.addRegion(covered);
        const QRectF br = traced.boundingRect();
        if (br.isEmpty() || traced.isEmpty()) {
            unit.addEllipse(QRectF(-0.5, -0.5, 1.0, 1.0));
        } else {
            const qreal s = 1.0 / std::max(br.width(), br.height());
            const QPainterPath centered =
                QTransform::fromTranslate(-br.center().x(), -br.center().y()).map(traced);
            unit = QTransform::fromScale(s, s).map(centered).simplified();
        }
    } else if (m_brushSettings.type == BrushType::Square) {
        unit.addRect(QRectF(-0.5, -0.5, 1.0, 1.0));
    } else {
        unit.addEllipse(QRectF(-0.5, -0.5, 1.0, 1.0));
    }

    m_brushOutlineUnit = unit;
    return unit;
}

// Per-frame: position the cached unit silhouette at the cursor, scaled to the painted
// footprint (size is a DIAMETER → unit's 1.0 axis maps to size*zoom), squashed by
// roundness and turned by the brush angle. Only a QTransform + map, no tracing.
QPainterPath CanvasView::brushOutlineScreenPath(const QPointF& centerScreen) const
{
    const QPainterPath& unit = brushOutlineUnitPath();
    const qreal zoom = m_doc ? m_doc->zoom : 1.0;
    const qreal diameter = std::max<qreal>(6.0, m_brushSettings.size * zoom);
    const qreal roundness = std::clamp<qreal>(m_brushSettings.roundness, 0.05, 1.0);
    QTransform t;
    t.translate(centerScreen.x(), centerScreen.y());
    t.rotateRadians(m_brushSettings.angle);
    t.scale(diameter, diameter * roundness);
    return t.map(unit);
}

void CanvasView::beginCloneStroke(QPointF documentPoint)
{
    if (!m_doc)
        return;
    auto* layer = m_doc->activeLayer();
    if (!layer)
        return;
    const QPointF screenPos = documentToScreen(documentPoint);
    BrushInputState input;
    input.imagePos = screenToImage(screenPos, layer);
    beginCloneStampStroke(input, screenPos, documentPoint);
}

void CanvasView::updateCloneStroke(QPointF documentPoint)
{
    if (!m_doc || !m_brushDrawing)
        return;
    auto* layer = m_doc->activeLayer();
    if (!layer)
        return;
    const QPointF screenPos = documentToScreen(documentPoint);
    BrushInputState input;
    input.imagePos = screenToImage(screenPos, layer);
    continueCloneStampStroke(input, screenPos, documentPoint);
}

void CanvasView::endCloneStroke()
{
    if (m_brushDrawing)
        endBrushStroke();
}

void CanvasView::setStampMode(int mode)
{
    const bool healing = (mode == static_cast<int>(StampMode::Healing));
    if (m_stampHealing == healing)
        return;
    m_stampHealing = healing;
    updateBrushPreviewOverlay();
    update();
}

void CanvasView::setHealingDiffusion(float diffusion)
{
    m_healingDiffusion = std::clamp(diffusion, 0.0f, 1.0f);
}

void CanvasView::setHealingSource(QPointF documentPoint)
{
    setStampMode(static_cast<int>(StampMode::Healing));
    if (m_currentTool != Tool::CloneStamp)
        setTool(Tool::CloneStamp);
    setCloneSource(documentPoint);
}

void CanvasView::beginHealingStroke(QPointF documentPoint)
{
    setStampMode(static_cast<int>(StampMode::Healing));
    if (m_currentTool != Tool::CloneStamp)
        setTool(Tool::CloneStamp);
    beginCloneStroke(documentPoint);
}

void CanvasView::updateHealingStroke(QPointF documentPoint)
{
    updateCloneStroke(documentPoint);
}

void CanvasView::endHealingStroke()
{
    endCloneStroke();
}

void CanvasView::updateBrushPreviewOverlay()
{
    if (!m_brushPreviewOverlay)
        return;
    // Keyed on the effective tool so the circular brush preview steps aside the
    // moment a contextual override (e.g. Alt → Eyedropper) is armed.
    const Tool tool = effectiveTool();
    const bool quickSelect = tool == Tool::Select
                          && m_selectType == SelectType::QuickSelect;
    const bool paintTool = tool == Tool::Brush
                        || tool == Tool::Eraser
                        || isCloneOrHealingTool();
    if ((!paintTool && !quickSelect) || !m_doc) {
        m_brushPreviewOverlay->clear();
        return;
    }
    // Caps Lock on a brush-engine tool replaces the circular size preview with a
    // precise crosshair (drawn by the OS CrossCursor in updateToolCursor), so the
    // circular overlay must step aside.
    if (paintTool && m_capsLockActive) {
        m_brushPreviewOverlay->clear();
        return;
    }
    // underMouse() tracks the OS mouse cursor. A pen hovering above the tablet may
    // not drive that cursor (proximity events carry no synthetic mouse), so also
    // keep the preview alive while the tablet is the active device — otherwise the
    // brush outline would vanish exactly when the pen is hovering.
    if (!m_brushDrawing && !m_quickSelecting && !underMouse()
        && !m_inputState.isTabletActive()) {
        m_brushPreviewOverlay->clear();
        return;
    }

    const QPointF destinationDoc = screenToDocument(m_lastScreenPos);
    // The painted dab uses radius == effectiveSize == settings.size*0.5 in document
    // pixels (size is a DIAMETER; see DynamicsEvaluator and BrushDab::rasterize), so
    // its on-screen radius is size*zoom*0.5. Match it so the cursor preview is the
    // size of the dab.
    const qreal radius = std::max<qreal>(3.0, m_brushSettings.size * m_doc->zoom * 0.5);

    if (quickSelect) {
        // Quick Selection paints a selection mask (not pixels), so the brush is
        // a plain circular outline — no tip/colour preview image. Reuses the
        // same zoom-aware overlay as the paint brushes for a consistent cursor.
        m_brushPreviewOverlay->setPreview(m_lastScreenPos, radius, true,
                                          false /*circle*/, QImage());
        return;
    }

    const int previewSide = std::clamp(static_cast<int>(std::ceil(radius * 2.0)), 8, 192);
    const bool squareBrush = m_brushSettings.type == BrushType::Square;

    if (!isCloneOrHealingTool()) {
        // Krita-style: show the real brush silhouette outline, not a filled tip.
        m_brushPreviewOverlay->setOutlinePreview(brushOutlineScreenPath(m_lastScreenPos));
        return;
    }

    const bool showSource = m_cloneSourceDefined || m_cloneNeedsSourceFeedback;
    if (!showSource) {
        m_brushPreviewOverlay->setPreview(m_lastScreenPos, radius, true,
                                          squareBrush,
                                          brushTipPreviewImage(previewSide));
        return;
    }

    QPointF sourceDoc = m_cloneSourceDefined ? m_cloneSourcePoint : destinationDoc;
    if (m_brushDrawing) {
        sourceDoc = m_cloneCurrentSourcePoint;
    } else if (m_cloneAligned && m_cloneOffsetValid) {
        sourceDoc = destinationDoc - m_cloneSourceOffset;
    }

    const QPointF sourceScreen = documentToScreen(sourceDoc);
    QImage previewImage;
    if (m_cloneSourceDefined) {
        // Rendering the source preview is O(preview²) with a per-pixel document
        // mapping; during a stroke pointer events arrive at hundreds of Hz, so
        // refresh the image at most ~30 Hz and reuse the last one in between —
        // the overlay position still follows every event.
        const bool reuse = m_brushDrawing
            && !m_clonePreviewImage.isNull()
            && m_clonePreviewThrottle.isValid()
            && m_clonePreviewThrottle.elapsed() < 33;
        if (reuse) {
            previewImage = m_clonePreviewImage;
        } else {
            const QImage sourceImage = (m_brushDrawing && m_cloneStrokeContext.isValid())
                ? m_cloneStrokeContext.sourceImage
                : clonePreviewSourceSnapshot();
            previewImage = cloneStampPreviewImage(m_lastScreenPos, destinationDoc,
                                                  sourceDoc, sourceImage);
            m_clonePreviewImage = previewImage;
            m_clonePreviewThrottle.start();
        }
    } else {
        previewImage = brushTipPreviewImage(previewSide);
    }
    m_brushPreviewOverlay->setPreview(m_lastScreenPos, radius, true,
                                      squareBrush,
                                      previewImage,
                                      sourceScreen,
                                      true,
                                      m_cloneSourceDefined,
                                      m_brushDrawing);
}

void CanvasView::markSelectMaskDirty(const QRect& region)
{
    if (m_gpuViewport) m_gpuViewport->markSelectNeedsUpload(region);
    updateSelectionAnimation();
    update();
}

void CanvasView::setGrayscaleMaskView(bool enabled)
{
    m_grayscaleMaskView = enabled;
    update();
}

void CanvasView::resetCrop()
{
    m_cropRect = QRectF(-1.0f, -1.0f, 2.0f, 2.0f);
    m_cropActive = true;
    m_cropDragging = false;
    m_cropRotateAngle = 0.0f;
}

void CanvasView::commitCropAction()
{
    if (!m_cropActive || !m_cropRect.isValid()) return;
    if (!m_controller) return;
    if (m_cropDragging) {
        // Finish any pending drag first
        m_cropDragging = false;
    }

    // Convert NDC crop rect to document-pixel space
    double docW = static_cast<double>(m_doc->size.width());
    double docH = static_cast<double>(m_doc->size.height());

    double ndcLeft = std::min(m_cropRect.left(), m_cropRect.right());
    double ndcRight = std::max(m_cropRect.left(), m_cropRect.right());
    double ndcTop = std::max(m_cropRect.top(), m_cropRect.bottom());
    double ndcBottom = std::min(m_cropRect.top(), m_cropRect.bottom());

    // Canvas NDC: x -1..1 maps left→right, y +1..-1 maps top→bottom.
    int cropX = static_cast<int>(std::round((ndcLeft + 1.0) * 0.5 * docW));
    int cropY = static_cast<int>(std::round((1.0 - ndcTop) * 0.5 * docH));
    int cropW = static_cast<int>(std::round((ndcRight - ndcLeft) * 0.5 * docW));
    int cropH = static_cast<int>(std::round((ndcTop - ndcBottom) * 0.5 * docH));

    // Clamp to valid document bounds
    cropX = std::clamp(cropX, 0, static_cast<int>(docW) - 1);
    cropY = std::clamp(cropY, 0, static_cast<int>(docH) - 1);
    cropW = std::clamp(cropW, 1, static_cast<int>(docW) - cropX);
    cropH = std::clamp(cropH, 1, static_cast<int>(docH) - cropY);

    m_controller->cropDocument(QRect(cropX, cropY, cropW, cropH));

    // Exit crop mode
    m_cropActive = false;
    setTool(Tool::Move);
}

void CanvasView::cancelCrop()
{
    m_cropActive = false;
    m_cropDragging = false;
    m_cropRotating = false;
    update();
}

CanvasView::CropHandleId CanvasView::cropHandleAtScreen(QPointF screenPos) const
{
    if (!m_cropActive) return CropHandleId::None;

    float l = std::min(static_cast<float>(m_cropRect.left()), static_cast<float>(m_cropRect.right()));
    float r = std::max(static_cast<float>(m_cropRect.left()), static_cast<float>(m_cropRect.right()));
    float t = std::max(static_cast<float>(m_cropRect.top()), static_cast<float>(m_cropRect.bottom()));
    float b = std::min(static_cast<float>(m_cropRect.top()), static_cast<float>(m_cropRect.bottom()));

    auto toScreen = [&](float x, float y) {
        float sxNdc = static_cast<float>(m_doc->panOffset.x())
                    + m_doc->zoom * static_cast<float>(m_canvasHalfExtents.x()) * x;
        float syNdc = static_cast<float>(m_doc->panOffset.y())
                    + m_doc->zoom * static_cast<float>(m_canvasHalfExtents.y()) * y;
        return QPointF((sxNdc + 1.0f) * 0.5f * width(),
                       (1.0f - syNdc) * 0.5f * height());
    };

    constexpr float hitHalfPx = 18.0f;
    auto hitBox = [&](float x, float y) {
        QPointF p = toScreen(x, y);
        return std::abs(static_cast<float>(screenPos.x() - p.x())) <= hitHalfPx &&
               std::abs(static_cast<float>(screenPos.y() - p.y())) <= hitHalfPx;
    };

    // Check corner handles first (priority over edges)
    if (hitBox(l, t))
        return CropHandleId::TopLeft;
    if (hitBox(r, t))
        return CropHandleId::TopRight;
    if (hitBox(r, b))
        return CropHandleId::BottomRight;
    if (hitBox(l, b))
        return CropHandleId::BottomLeft;

    // Edge midpoints
    float mx = (l + r) * 0.5f;
    float my = (t + b) * 0.5f;
    if (hitBox(mx, t))
        return CropHandleId::Top;
    if (hitBox(r, my))
        return CropHandleId::Right;
    if (hitBox(mx, b))
        return CropHandleId::Bottom;
    if (hitBox(l, my))
        return CropHandleId::Left;

    // Inside the rect
    QPointF ndc = screenToCanvasNdc(screenPos);
    if (ndc.x() >= l && ndc.x() <= r && ndc.y() <= t && ndc.y() >= b)
        return CropHandleId::Inside;

    return CropHandleId::None;
}

QCursor CanvasView::cropCursorForHandle(CropHandleId handle) const
{
    switch (handle) {
    case CropHandleId::TopLeft:
    case CropHandleId::BottomRight:
        return Qt::SizeFDiagCursor;
    case CropHandleId::TopRight:
    case CropHandleId::BottomLeft:
        return Qt::SizeBDiagCursor;
    case CropHandleId::Top:
    case CropHandleId::Bottom:
        return Qt::SizeVerCursor;
    case CropHandleId::Left:
    case CropHandleId::Right:
        return Qt::SizeHorCursor;
    case CropHandleId::Inside:
        return Qt::SizeAllCursor;
    default:
        return Qt::ArrowCursor;
    }
}

void CanvasView::setCropAspectRatio(const QSizeF& ratio)
{
    m_cropLockedRatio = ratio;
    update();
}

void CanvasView::setCropGuideType(int type)
{
    m_cropGuideType = static_cast<CropGuideType>(type);
    update();
}

void CanvasView::setCropOverlayOpacity(float opacity)
{
    m_cropOverlayOpacity = opacity;
    update();
}

void CanvasView::setCropStraightenAngle(float angle)
{
    m_cropRotateAngle = angle;
    update();
}

void CanvasView::setCropCustomSize(int w, int h)
{
    if (!m_doc || m_doc->size.isEmpty()) return;
    m_cropLockedRatio = QSizeF(w, h);
    double docW = static_cast<double>(m_doc->size.width());
    double docH = static_cast<double>(m_doc->size.height());

    // Keep center fixed while updating size
    float cx = static_cast<float>(m_cropRect.center().x());
    float cy = static_cast<float>(m_cropRect.center().y());

    // Convert pixel W,H to canvas NDC width,height
    float ndcW = static_cast<float>(w) / static_cast<float>(docW) * 2.0f;
    float ndcH = static_cast<float>(h) / static_cast<float>(docH) * 2.0f;

    m_cropRect = QRectF(cx - ndcW * 0.5f, cy - ndcH * 0.5f, ndcW, ndcH);
    update();
}

void CanvasView::setViewportCursor(const QCursor& cursor)
{
    setCursor(cursor);
    if (m_tabletViewportCursorOverride)
        syncTabletViewportCursor();
}

void CanvasView::syncTabletViewportCursor()
{
    // The pen pointer does not honour the widget's setCursor() the way the mouse
    // does, so EVERY tool needs the application-wide override to actually display
    // its cursor under the tablet (the brush's BlankCursor included). The override
    // leaking onto the UI off-canvas is not handled here — the qApp event filter
    // releases it the moment the pen leaves this widget (see eventFilter).
    const QCursor cursorForTablet = cursor();
    if (m_tabletViewportCursorOverride) {
        if (QApplication::overrideCursor())
            QApplication::changeOverrideCursor(cursorForTablet);
        else
            QApplication::setOverrideCursor(cursorForTablet);
        return;
    }
    if (QApplication::overrideCursor())
        return;
    QApplication::setOverrideCursor(cursorForTablet);
    m_tabletViewportCursorOverride = true;
}

void CanvasView::releaseTabletViewportCursor()
{
    if (!m_tabletViewportCursorOverride)
        return;
    if (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();
    m_tabletViewportCursorOverride = false;
}

void CanvasView::updateToolCursor()
{
    updateToolCursor(QApplication::keyboardModifiers());
}

void CanvasView::updateToolCursor(Qt::KeyboardModifiers mods)
{
    if (m_panning) return;
    if (m_transformState.mode != InteractionMode::Idle) return;
    if (m_textToolState == TextToolState::Editing) {
        setViewportCursor(QCursor(Qt::IBeamCursor));
        return;
    }
    // Cursor follows the effective tool so a contextual override (e.g. Alt over a
    // paint tool → Eyedropper) shows the override's cursor immediately.
    const Tool tool = effectiveTool();
    if (tool == Tool::Select) {
        setViewportCursor(m_selectCursors[static_cast<int>(m_selectType)]);
        return;
    }
    if (tool == Tool::Crop) {
        setViewportCursor(m_cursors[static_cast<int>(Tool::Crop)]);
        return;
    }
    if (tool == Tool::Eyedropper) {
        setViewportCursor(m_cursors[static_cast<int>(Tool::Eyedropper)]);
        return;
    }
    if (tool == Tool::Brush || tool == Tool::Eraser || isCloneOrHealingTool()) {
        // Caps Lock swaps the circular size cursor for a precise crosshair
        // (standard Caps Lock behaviour); otherwise the preview overlay draws the circle, so
        // hide the OS cursor icon to avoid overlapping it. Alt over a clone/healing
        // tool arms source-picking, which also shows the crosshair — kept in sync with
        // the pointer-driven path (updateBrushCursorFromPointer) so the cursor flips on
        // the Alt press/release, not only on the next mouse move.
        const bool cloneAlt = isCloneOrHealingTool() && (mods & Qt::AltModifier);
        setViewportCursor(QCursor((cloneAlt || m_capsLockActive)
            ? Qt::CrossCursor : Qt::BlankCursor));
        return;
    }
    setViewportCursor(m_cursors[static_cast<int>(tool)]);
}

void CanvasView::commitTextEdit()
{
    if (m_textToolState != TextToolState::Editing || !m_textEditor.isEditing()) return;
    m_textEditor.endEdit();
    m_textToolState = TextToolState::Idle;

    if (m_controller && m_textLayerIndex >= 0) {
        TextLayerData before = m_textBeforeSnapshot;
        auto* node = m_doc ? m_doc->nodeAt(m_textLayerIndex) : nullptr;
        if (node && node->layer && node->layer->textData) {
            TextLayerData after = *node->layer->textData;
            auto& td = *node->layer->textData;
            td.dirty = true;

            TextRenderer renderer;
            renderer.render(td, node->layer->cpuImage);
            fitTextLayerTransformToImage(node);

            makeCurrent();
            m_controller->syncLayerToGpu(node->layer.get());
            doneCurrent();

            m_controller->commitTextEdit(m_textLayerIndex, before, m_textBeforeTransform);
        }
    }
    m_textLayerIndex = -1;
    m_textBeforeSnapshot = {};
    m_textBeforeTransform = QTransform();
    updateToolCursor();
    update();
}

TextLayerData* CanvasView::activeTextData()
{
    if (m_textEditor.isEditing() && m_textEditor.data())
        return m_textEditor.data();
    if (!m_doc || m_doc->activeFlatIndex < 0) return nullptr;
    auto* node = m_doc->activeNode();
    if (node && node->layer && node->layer->textData) {
        m_textLayerIndex = m_doc->activeFlatIndex;
        return node->layer->textData.get();
    }
    return nullptr;
}

// Whether the active layer's editable content (text spans/paragraphs, shape
// geometry/paint) may change — false when Lock Image Pixels / Lock All is set.
bool CanvasView::activeContentEditable() const
{
    auto* node = m_doc ? m_doc->activeNode() : nullptr;
    return node && node->canEditContent();
}

bool CanvasView::currentTextContext(TextSpan& outChar, ParagraphStyle& outPara)
{
    TextLayerData* td = activeTextData();
    if (!td) return false;

    if (m_textEditor.isEditing() && m_textEditor.data() == td) {
        outChar = m_textEditor.currentStyle();
        const int pos = m_textEditor.hasSelection()
            ? m_textEditor.selLow() : m_textEditor.cursorPos();
        outPara = paragraphStyleAt(*td, paragraphIndexForChar(td->text, pos));
        return true;
    }

    // Not editing: report the last run / first paragraph as a representative.
    if (!td->spans.empty()) outChar = td->spans.back();
    else { outChar = TextSpan{}; outChar.color = Qt::black; }
    outPara = paragraphStyleAt(*td, 0);
    return true;
}

void CanvasView::applyTextCharStyle(const std::function<void(TextSpan&)>& fn)
{
    TextLayerData* td = activeTextData();
    if (!td) return;
    if (!activeContentEditable()) {
        if (m_controller)
            emit m_controller->operationBlocked(
                tr("The content of this text layer is locked."));
        return;
    }

    // Editing: route through the editor so the change lands on the selection
    // (splitting runs) or — with no selection — the pending insertion style.
    if (m_textEditor.isEditing() && m_textEditor.data() == td) {
        m_textEditor.modifyCharStyle(fn);   // emits textChanged → rerenderTextLayer
        return;
    }

    // Not editing (layer merely selected): apply to the whole layer + undo step.
    int idx = (m_doc && m_doc->activeFlatIndex >= 0) ? m_doc->activeFlatIndex : m_textLayerIndex;
    auto* node = (m_doc && idx >= 0) ? m_doc->nodeAt(idx) : nullptr;
    const bool canCommit = node && node->layer
        && node->layer->textData.get() == td && m_controller;

    TextLayerData before = *td;
    QTransform beforeTransform = node ? node->transform() : QTransform();

    if (td->spans.empty())
        td->spans.push_back({0, static_cast<int>(td->text.size())});
    for (auto& span : td->spans)
        fn(span);
    td->dirty = true;
    rerenderTextLayer();

    if (canCommit)
        m_controller->commitTextEdit(idx, before, beforeTransform);
}

void CanvasView::applyTextParagraphStyle(const std::function<void(ParagraphStyle&)>& fn)
{
    TextLayerData* td = activeTextData();
    if (!td) return;
    if (!activeContentEditable()) {
        if (m_controller)
            emit m_controller->operationBlocked(
                tr("The content of this text layer is locked."));
        return;
    }
    normalizeParagraphs(*td);

    if (m_textEditor.isEditing() && m_textEditor.data() == td) {
        m_textEditor.applyToParagraphs(fn);   // emits textChanged → rerenderTextLayer
        return;
    }

    int idx = (m_doc && m_doc->activeFlatIndex >= 0) ? m_doc->activeFlatIndex : m_textLayerIndex;
    auto* node = (m_doc && idx >= 0) ? m_doc->nodeAt(idx) : nullptr;
    const bool canCommit = node && node->layer
        && node->layer->textData.get() == td && m_controller;

    TextLayerData before = *td;
    QTransform beforeTransform = node ? node->transform() : QTransform();

    for (auto& ps : td->paragraphs)
        fn(ps);
    td->dirty = true;
    rerenderTextLayer();

    if (canCommit)
        m_controller->commitTextEdit(idx, before, beforeTransform);
}

void CanvasView::setTextFont(const QFont& font)
{
    const QString family = font.family();
    applyTextCharStyle([family](TextSpan& s) { s.fontFamily = family; });
}

void CanvasView::setTextSize(int size)
{
    const float v = static_cast<float>(size);
    applyTextCharStyle([v](TextSpan& s) { s.fontSize = v; });
}

void CanvasView::setTextBold(bool bold)
{
    applyTextCharStyle([bold](TextSpan& s) { s.bold = bold; });
}

void CanvasView::setTextItalic(bool italic)
{
    applyTextCharStyle([italic](TextSpan& s) { s.italic = italic; });
}

void CanvasView::setTextUnderline(bool underline)
{
    applyTextCharStyle([underline](TextSpan& s) { s.underline = underline; });
}

void CanvasView::setTextStrikethrough(bool strikethrough)
{
    applyTextCharStyle([strikethrough](TextSpan& s) { s.strikethrough = strikethrough; });
}

void CanvasView::setTextColor(const QColor& color)
{
    applyTextCharStyle([color](TextSpan& s) { s.color = color; });
}

void CanvasView::setTextAlign(int align)
{
    const TextAlign a = static_cast<TextAlign>(align);
    if (TextLayerData* td = activeTextData())
        td->align = a;   // seed alignment for newly created paragraphs
    applyTextParagraphStyle([a](ParagraphStyle& ps) { ps.alignment = a; });
}

void CanvasView::setTextTracking(double tracking)
{
    const float v = static_cast<float>(tracking);
    applyTextCharStyle([v](TextSpan& s) { s.letterSpacing = v; });
}

void CanvasView::setTextLeading(double leading)
{
    const float v = static_cast<float>(leading);
    if (TextLayerData* td = activeTextData())
        td->lineSpacing = v;   // seed leading for newly created paragraphs
    applyTextParagraphStyle([v](ParagraphStyle& ps) { ps.lineHeight = v; });
}

static QPointF localNdcToPixel(QPointF ndc, const Layer* layer)
{
    if (!layer || layer->cpuImage.isNull())
        return ndc;
    float px = (static_cast<float>(ndc.x()) + 1.0f) * 0.5f * layer->cpuImage.width();
    float py = (1.0f - static_cast<float>(ndc.y())) * 0.5f * layer->cpuImage.height();
    return {px, py};
}

int CanvasView::textLayerCharAt(LayerTreeNode* node, QPointF screenPos)
{
    if (!node || !node->layer || !node->layer->textData) return 0;
    QPointF canvasPos = screenToCanvasNdc(screenPos);
    QTransform inv = node->accumulatedTransform().inverted();
    QPointF localNdc = inv.map(canvasPos);
    QPointF pixelPos = localNdcToPixel(localNdc, node->layer.get());

    TextLayoutEngine engine;
    engine.setData(node->layer->textData.get());
    return engine.hitTest(pixelPos, true);
}

void CanvasView::rerenderTextLayer()
{
    if (!m_controller) return;
    int idx = m_textLayerIndex;
    if (idx < 0 && m_doc)
        idx = m_doc->activeFlatIndex;
    if (idx < 0) return;
    auto* node = m_doc ? m_doc->nodeAt(idx) : nullptr;
    if (!node || !node->layer || !node->layer->textData) return;

    TextRenderer renderer;
    renderer.setEditing(true);
    renderer.setCursorPos(m_textEditor.cursorPos());
    if (m_textEditor.hasSelection()) {
        renderer.setSelection(m_textEditor.selStart(), m_textEditor.selEnd());
    }
    renderer.setCaretVisible(m_textEditor.caretVisible());
    renderer.render(*node->layer->textData, node->layer->cpuImage);
    fitTextLayerTransformToImage(node);

    node->layer->textureOutdated = true;
    node->invalidateEffects();
    // Bump compositionGeneration so the projection cache rebuilds both during
    // live typing (liveEdit=true uses LayerCompositor) and after commit
    // (liveEdit drops to false and the projection must reflect the new text).
    if (m_doc) ++m_doc->compositionGeneration;
    makeCurrent();
    syncLayersToGpu();
    doneCurrent();

    update();
    // Let the options bar follow the caret/selection style during editing.
    emit textStyleContextChanged();
}

void CanvasView::fitTextLayerTransformToImage(LayerTreeNode* node)
{
    if (!m_doc || !node || !node->layer || node->layer->cpuImage.isNull())
        return;

    // Work in WORLD (accumulated) space so this also works for nested text (inside
    // a group), then strip the parent chain to store the result as the node's LOCAL
    // transform. We rescale only the magnitude of the layer's basis vectors to map
    // the freshly rendered cpuImage 1:1 to document pixels, keeping their direction
    // intact so any ROTATION/SHEAR survives (a text layer must not snap back to
    // axis-aligned when re-rendered after a rotate). The basis lengths are
    // measured in VISUAL (document-pixel) space — NDC lengths are anisotropic
    // for non-square documents, so fitting them stretched rotated text by the
    // aspect ratio. The local top-left corner (layer NDC -1,+1) stays anchored
    // in world space, matching the old behaviour for the non-rotated case.
    const QTransform oldAccum = node->accumulatedTransform();

    // 2x2 linear part: x-basis = (m11,m12), y-basis = (m21,m22); center = (m31,m32).
    const double xbx = oldAccum.m11(), xby = oldAccum.m12();
    const double ybx = oldAccum.m21(), yby = oldAccum.m22();
    const double cx  = oldAccum.m31(), cy  = oldAccum.m32();

    const double docW = static_cast<double>(std::max(1, m_doc->size.width()));
    const double docH = static_cast<double>(std::max(1, m_doc->size.height()));

    // Visual length each basis currently spans (document pixels), and the
    // target = the rendered image's own pixel extent. For an upright layer
    // this reduces exactly to the old NDC math.
    const double oldLenX = std::hypot(xbx * docW, xby * docH);
    const double oldLenY = std::hypot(ybx * docW, yby * docH);
    if (oldLenX < 1e-9 || oldLenY < 1e-9)
        return;

    // Rescale each basis vector so its VISUAL length matches the image,
    // keeping its direction.
    const double sx = static_cast<double>(node->layer->cpuImage.width()) / oldLenX;
    const double sy = static_cast<double>(node->layer->cpuImage.height()) / oldLenY;
    const double nXbx = xbx * sx, nXby = xby * sx;
    const double nYbx = ybx * sy, nYby = yby * sy;

    // World position of the local top-left corner (-1,+1) — keep it fixed, then
    // solve for the new center: center = anchor + xbasis - ybasis.
    const double anchorX = -xbx + ybx + cx;
    const double anchorY = -xby + yby + cy;
    const double nCx = anchorX + nXbx - nYbx;
    const double nCy = anchorY + nXby - nYby;

    QTransform naturalWorld;
    naturalWorld.setMatrix(
        nXbx, nXby, 0.0,
        nYbx, nYby, 0.0,
        nCx,  nCy,  1.0);

    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    node->setBaseTransform(naturalWorld * parentAccum.inverted());
}

void CanvasView::resizeParagraphTextBoxToTransform(LayerTreeNode* node, bool editing)
{
    if (!m_doc || !node || !node->layer || !node->layer->textData)
        return;
    auto& td = *node->layer->textData;
    if (td.flowMode != TextFlowMode::Paragraph)
        return;

    // Use the basis-vector lengths (not raw m11/m22) so a rotated/sheared text box
    // keeps the size the user dragged instead of the foreshortened axis projection.
    // Lengths are measured in VISUAL (document-pixel) space — NDC lengths are
    // anisotropic for non-square documents and would resize the box of a
    // rotated paragraph by the aspect ratio.
    const QTransform xf = node->transform();
    const double docW = static_cast<double>(std::max(1, m_doc->size.width()));
    const double docH = static_cast<double>(std::max(1, m_doc->size.height()));
    const float imageW = static_cast<float>(
        std::hypot(xf.m11() * docW, xf.m12() * docH));
    const float imageH = static_cast<float>(
        std::hypot(xf.m21() * docW, xf.m22() * docH));
    td.box.width = std::max(16.0f, imageW - 12.0f);
    td.box.height = std::max(16.0f, imageH - 12.0f);
    td.dirty = true;

    TextRenderer renderer;
    renderer.setEditing(editing);
    if (editing) {
        renderer.setCursorPos(m_textEditor.cursorPos());
        if (m_textEditor.hasSelection())
            renderer.setSelection(m_textEditor.selStart(), m_textEditor.selEnd());
        renderer.setCaretVisible(m_textEditor.caretVisible());
    }
    renderer.render(td, node->layer->cpuImage);
    fitTextLayerTransformToImage(node);

    node->layer->textureOutdated = true;
    node->invalidateEffects();
    // Bump the composition generation so the cached CPU projection rebuilds with
    // the freshly re-rendered raster + fitted transform. Without this, when the
    // resize gesture ends (liveEdit drops to false) ProjectionCache::update sees
    // an unchanged generation, keeps its stale texture, and the rendered text
    // lands at the pre-resize position while the transform outline is already
    // correct — only a later move (which bumps the generation) fixes it. Mirrors
    // rerenderTextLayer() and bakeTextLayerResolution() (the Point-text sibling).
    if (m_doc) ++m_doc->compositionGeneration;
    makeCurrent();
    syncLayersToGpu();
    doneCurrent();
}

void CanvasView::initializeGL()
{
    m_gpuViewport = new GPUViewport();
    m_gpuViewport->initialize();
    // Repaint when an off-thread projection composite finishes (Fase C async):
    // the next paint uploads + displays the freshly-built projection. Queued so
    // it is delivered to the GUI thread even though the signal originates there.
    m_gpuViewport->setProjectionRepaintCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
    });
    m_brushRenderer.initGL();
    updateCanvasRect();
    m_antTimer.start();
}

void CanvasView::updateSelectionAnimation()
{
    if (!m_selectAnimTimer || !m_doc) return;

    const bool hasSelection = m_doc->selection.active() && !m_doc->selection.isEmpty();
    if (hasSelection) {
        if (!m_antTimer.isValid())
            m_antTimer.start();
        if (isVisible() && !m_selectAnimTimer->isActive())
            m_selectAnimTimer->start();
    } else if (m_selectAnimTimer->isActive()) {
        m_selectAnimTimer->stop();
    }
}

void CanvasView::updateCanvasRect()
{
    if (!m_doc || m_doc->size.isNull() || m_doc->size.isEmpty() || width() == 0 || height() == 0) {
        m_canvasHalfExtents = QPointF(1.0f, 1.0f);
        return;
    }

    // 100% zoom must be document pixels at 1:1 logical screen pixels.
    // m_canvasHalfExtents controls the pre-zoom screen footprint, so using
    // docPx / viewportPx maps zoom=1.0 to real document size instead of fit-to-view.
    const float docW = static_cast<float>(std::max(1, m_doc->size.width()));
    const float docH = static_cast<float>(std::max(1, m_doc->size.height()));
    const float vpW = static_cast<float>(std::max(1, width()));
    const float vpH = static_cast<float>(std::max(1, height()));
    m_canvasHalfExtents = QPointF(docW / vpW, docH / vpH);
}

bool CanvasView::viewportCanvasMetrics(double& vw, double& vh,
                                       double& cw, double& ch) const
{
    if (!m_doc || m_doc->size.isEmpty() || width() <= 0 || height() <= 0)
        return false;
    vw = std::max(1, width());
    vh = std::max(1, height());
    cw = static_cast<double>(m_doc->zoom) * std::max(1, m_doc->size.width());
    ch = static_cast<double>(m_doc->zoom) * std::max(1, m_doc->size.height());
    return true;
}

void CanvasView::clampPanOffset()
{
    double vw, vh, cw, ch;
    if (!viewportCanvasMetrics(vw, vh, cw, ch))
        return;

    // panOffset is stored in viewport NDC: it is the clip-space position of the
    // canvas CENTRE (vertex 0,0 maps to clip = panOffset). Convert it to the
    // clampPan() convention (screen-pixel position of the canvas TOP-LEFT),
    // remembering screen Y grows downward while NDC Y grows upward.
    const double specX = (m_doc->panOffset.x() + 1.0) * 0.5 * vw - cw * 0.5;
    const double specY = (1.0 - m_doc->panOffset.y()) * 0.5 * vh - ch * 0.5;

    const QPointF clamped = core::ViewportCamera::clampPan(
        QPointF(specX, specY), QSizeF(vw, vh), QSizeF(cw, ch), m_overscrollEnabled);

    // Convert the clamped top-left back to centre-NDC.
    const double newPanX = (clamped.x() + cw * 0.5) * 2.0 / vw - 1.0;
    const double newPanY = 1.0 - (clamped.y() + ch * 0.5) * 2.0 / vh;

    m_doc->panOffset = QPointF(newPanX, newPanY);
}

void CanvasView::updateScrollBars()
{
    if (!m_hScrollBar || !m_vScrollBar)
        return;

    double vw, vh, cw, ch;
    if (!viewportCanvasMetrics(vw, vh, cw, ch)) {
        m_hScrollBar->hide();
        m_vScrollBar->hide();
        return;
    }

    const auto b = core::ViewportCamera::panBounds(
        QSizeF(vw, vh), QSizeF(cw, ch), m_overscrollEnabled);

    // Current canvas top-left (same convention as the bounds).
    const double specX = (m_doc->panOffset.x() + 1.0) * 0.5 * vw - cw * 0.5;
    const double specY = (1.0 - m_doc->panOffset.y()) * 0.5 * vh - ch * 0.5;

    // Scrollbar value grows as the canvas slides left/up (revealing its right/
    // bottom side): value = max - spec. The navigable span (max - min) is the
    // whole range; pageStep = viewport, so the handle reflects the visible
    // fraction (canvas-only with overscroll off, canvas+margin with it on).
    const int rangeX = std::max(0, static_cast<int>(std::lround(b.maxX - b.minX)));
    const int rangeY = std::max(0, static_cast<int>(std::lround(b.maxY - b.minY)));
    const bool showH = rangeX > 0;
    const bool showV = rangeY > 0;

    m_updatingScrollBars = true;
    m_hScrollBar->setPageStep(std::max(1, static_cast<int>(std::lround(vw))));
    m_hScrollBar->setSingleStep(std::max(1, static_cast<int>(std::lround(vw / 20.0))));
    m_hScrollBar->setRange(0, rangeX);
    m_hScrollBar->setValue(static_cast<int>(std::lround(b.maxX - specX)));
    m_vScrollBar->setPageStep(std::max(1, static_cast<int>(std::lround(vh))));
    m_vScrollBar->setSingleStep(std::max(1, static_cast<int>(std::lround(vh / 20.0))));
    m_vScrollBar->setRange(0, rangeY);
    m_vScrollBar->setValue(static_cast<int>(std::lround(b.maxY - specY)));
    m_updatingScrollBars = false;

    // Overlay layout: bars hug the right/bottom edges; when both show they leave
    // a small corner so they don't overlap.
    const int thickness = 12;
    const int wReserve = showV ? thickness : 0;
    const int hReserve = showH ? thickness : 0;

    m_hScrollBar->setVisible(showH);
    m_vScrollBar->setVisible(showV);
    if (showH) {
        m_hScrollBar->setGeometry(0, height() - thickness,
                                  std::max(1, width() - wReserve), thickness);
        m_hScrollBar->raise();
    }
    if (showV) {
        m_vScrollBar->setGeometry(width() - thickness, 0,
                                  thickness, std::max(1, height() - hReserve));
        m_vScrollBar->raise();
    }
}

void CanvasView::onScrollBarMoved()
{
    if (m_updatingScrollBars)
        return;

    double vw, vh, cw, ch;
    if (!viewportCanvasMetrics(vw, vh, cw, ch))
        return;

    const auto b = core::ViewportCamera::panBounds(
        QSizeF(vw, vh), QSizeF(cw, ch), m_overscrollEnabled);

    // Inverse of updateScrollBars(): spec = max - value.
    const double specX = b.maxX - m_hScrollBar->value();
    const double specY = b.maxY - m_vScrollBar->value();

    const double newPanX = (specX + cw * 0.5) * 2.0 / vw - 1.0;
    const double newPanY = 1.0 - (specY + ch * 0.5) * 2.0 / vh;
    m_doc->panOffset = QPointF(newPanX, newPanY);
    clampPanOffset();
    update();
}

void CanvasView::setOverscrollEnabled(bool enabled)
{
    if (m_overscrollEnabled == enabled)
        return;
    m_overscrollEnabled = enabled;
    clampPanOffset();
    updateScrollBars();
    update();
}



void CanvasView::paintGL()
{
    updateCanvasRect();
    // Single enforcement net: keeps panOffset valid no matter which path moved
    // it (interactive pan/zoom, viewport resize, document switch, controller
    // reset_view/zoom, project load) before anything reads it this frame.
    clampPanOffset();

    if (!m_gpuViewport || !m_doc) return;
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][canvas-paintGL]"
            << "canvasSize" << size()
            << "docSize" << m_doc->size
            << "zoom" << m_doc->zoom
            << "pan" << m_doc->panOffset
            << "halfExtents" << m_canvasHalfExtents
            << "compositionGeneration" << m_doc->compositionGeneration
            << "activeFlatIndex" << m_doc->activeFlatIndex;
    }

    RenderParams p;
    p.doc = m_doc;
    p.canvasHalfExtents = m_canvasHalfExtents;

    p.currentTool            = static_cast<int>(m_currentTool);
    p.editingMask            = m_editingMask;
    p.grayscaleMaskView      = m_grayscaleMaskView;
    p.quickMaskMode          = m_quickMaskMode;

    // On-demand rubylith overlay (per-document view flag). Only force the
    // per-layer path when there is actually an active-layer mask to overlay.
    {
        auto* overlayLayer = m_doc->activeLayer();
        p.showMaskOverlay = m_doc->maskOverlayVisible
                         && overlayLayer && !overlayLayer->maskImage.isNull();
        p.maskOverlayOpacity = m_doc->maskOverlayOpacity;
    }
    // p.showTransformControls  = m_showTransformControls && m_freeTransformActive;
    // While distorting, the DistortPreviewOverlay draws its own quad + handles,
    // so suppress the GPU affine bounding box (which would sit at the hidden
    // layer's old position).
    // Adjustment layers are non-spatial: no bounding box / transform handles.
    {
        auto* activeForXf = m_doc->activeNode();
        const bool adjustmentActive = activeForXf
            && activeForXf->type == LayerTreeNode::Type::Adjustment;
        p.showTransformControls  = !m_distortActive && !adjustmentActive
                                 && (m_showTransformControls || m_freeTransformActive);
    }

    // A single active group drives the same GPU box+handles via the multi-bbox
    // slots. Its frame is recomputed FRESH every frame from the live group
    // transform + children (no cache), so it rotates/scales during a gesture and
    // follows external moves (e.g. the align tool) without staleness.
    if (activeIsSingleGroup()) {
        bool gok = false;
        const QTransform gf = groupFrameTransform(m_doc->activeNode(), &gok);
        if (gok) {
            float gHw, gHh, gRot; QPointF gC;
            TransformController::decomposeVisual(gf, m_canvasHalfExtents, size(),
                                                 gHw, gHh, gC, gRot);
            const QPointF metric(std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                                 std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
            m_multiResizeGroupBboxCenter = QPointF(gC.x() / metric.x(), gC.y() / metric.y());
            m_multiResizeGroupBboxHw = gHw / metric.x();
            m_multiResizeGroupBboxHh = gHh / metric.y();
            m_multiResizeGroupBboxRotation = gRot;
            m_multiResizeGroupBboxValid = true;
        } else {
            m_multiResizeGroupBboxValid = false;  // empty group: no box
        }
    } else if (m_doc->selectedFlatIndices.size() > 1) {
        // Multi-selection: same recompute path mousePress uses, so the drawn
        // box and the grabbed box always agree (groups included). The cache is
        // kept while the selection signature matches — gestures update it live.
        if (!multiOutlineMatchesSelection())
            computeMultiSelectionBbox();
    } else {
        m_multiResizeGroupBboxValid = false;
    }

    p.activeMultiTransform = m_multiResizeGroupBboxValid
        || (m_moving && m_multiResizeIndices.size() > 1
            && (m_transformState.mode == InteractionMode::Resizing
                || m_transformState.mode == InteractionMode::Rotating));
    p.multiBboxCenterX = static_cast<float>(m_multiResizeGroupBboxCenter.x());
    p.multiBboxCenterY = static_cast<float>(m_multiResizeGroupBboxCenter.y());
    p.multiBboxHw = m_multiResizeGroupBboxHw;
    p.multiBboxHh = m_multiResizeGroupBboxHh;
    p.multiBboxRotation = m_multiResizeGroupBboxRotation;

    p.hasPreview      = m_hasPreview;
    p.previewTexture  = m_previewTexture;

    p.selectType          = static_cast<int>(m_selectType);
    p.selectStart         = m_selectStart;
    p.selectCurrent       = m_selectCurrent;
    p.selectDragging      = m_selectDragging;
    p.lassoDrawing        = m_lassoDrawing;
    p.lassoCanClose       = m_lassoCanClose;
    p.lassoPoints         = &m_lassoPoints;
    p.antTimer            = &m_antTimer;

    p.cropRect            = m_cropRect;
    p.cropActive          = m_cropActive;
    p.cropGuideType       = static_cast<int>(m_cropGuideType);
    p.cropOverlayOpacity  = m_cropOverlayOpacity;

    p.transformingSelection = m_transformingSelection;

    p.boxSelecting    = m_boxSelecting;
    p.boxSelectStart  = m_boxSelectStart;
    p.boxSelectCurrent = m_boxSelectCurrent;

    p.textToolState  = static_cast<int>(m_textToolState);
    p.textLayerIndex = m_textLayerIndex;
    p.textEditor     = &m_textEditor;

    p.brushRadius          = m_brushSettings.size;
    p.brushIndicatorCenter = m_lastScreenPos;

    // Live composite mutations not yet reflected in compositionGeneration: the
    // GPU keeps its per-layer compositor for these frames. Everything else
    // displays the cached CPU projection (overlays still draw on top).
    p.liveEdit = m_brushDrawing
              || m_moving
              || m_movingSelection
              || m_freeTransformActive
              || m_shapeDragging
              || m_adjustmentLiveEdit // Adjustment slider/curve drag in Properties panel
              || (m_distortActive && m_distortDragCorner >= 0) // distort/perspective corner drag
              || (static_cast<int>(m_textToolState) == 2); // text editing

    p.viewportW = width();
    p.viewportH = height();

    m_gpuViewport->render(p);

    updateRulerGuideOverlay();
    updateScrollBars();

    // The distort outline is drawn in screen px; refresh it each frame so it
    // tracks zoom/pan (which repaint the canvas without firing mouse events).
    if (m_distortActive)
        updateDistortOverlay();

    updateBrushPreviewOverlay();
}

void CanvasView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    updateCanvasRect();
    if (m_selectDragOverlay)
        m_selectDragOverlay->setGeometry(rect());
    if (m_shapePreviewOverlay)
        m_shapePreviewOverlay->setGeometry(rect());
    if (m_distortOverlay) {
        m_distortOverlay->setGeometry(rect());
        if (m_distortActive)
            updateDistortOverlay();
    }
    if (m_brushPreviewOverlay) {
        m_brushPreviewOverlay->setGeometry(rect());
        updateBrushPreviewOverlay();
    }
    if (m_gradientOverlay)
        m_gradientOverlay->setGeometry(rect());
    updateRulerGuideOverlay();
    clampPanOffset();      // viewport size changed → re-clamp pan…
    updateScrollBars();    // …and re-layout/re-range the bars
    if (m_pendingFit)
        fitToView();
}

void CanvasView::fitToView()
{
    if (!m_doc || m_doc->size.isEmpty())
        return;

    const int vpW = width();
    const int vpH = height();
    if (vpW <= 0 || vpH <= 0) {
        // Widget not laid out yet — apply once resizeGL gives us a real size.
        m_pendingFit = true;
        return;
    }

    const float docW = static_cast<float>(std::max(1, m_doc->size.width()));
    const float docH = static_cast<float>(std::max(1, m_doc->size.height()));
    const float fit = std::min(static_cast<float>(vpW) / docW,
                               static_cast<float>(vpH) / docH);

    // Fit large documents inside the viewport (with a small margin so the
    // canvas border/shadow stay visible). Never upscale small images past 100%.
    float zoom = std::min(1.0f, fit * 0.95f);
    zoom = std::clamp(zoom, 0.01f, 100.0f);

    m_doc->zoom = zoom;
    m_doc->panOffset = QPointF(0.0f, 0.0f);
    m_pendingFit = false;

    updateCanvasRect();
    clampPanOffset();
    emit zoomChanged(m_doc->zoom);
    update();
}

void CanvasView::zoomToOriginal()
{
    if (!m_doc) return;
    m_doc->zoom = std::clamp(1.0f, 0.01f, 100.0f);
    m_doc->panOffset = QPointF(0.0f, 0.0f);
    updateCanvasRect();
    clampPanOffset();
    emit zoomChanged(m_doc->zoom);
    update();
}

void CanvasView::setZoom(float zoom)
{
    if (!m_doc) return;

    const float clampedZoom = std::clamp(zoom, 0.01f, 100.0f);
    if (std::abs(m_doc->zoom - clampedZoom) < 0.0001f)
        return;

    m_doc->zoom = clampedZoom;
    updateCanvasRect();
    // Re-clamp against the new on-screen canvas size so the pan stays valid (and
    // re-centres when overscroll-off zoom makes the canvas smaller than the view).
    clampPanOffset();
    emit zoomChanged(m_doc->zoom);
    update();
}

void CanvasView::zoomIn()
{
    if (!m_doc) return;
    setZoom(m_doc->zoom * 1.25f);
}

void CanvasView::zoomOut()
{
    if (!m_doc) return;
    setZoom(m_doc->zoom / 1.25f);
}

void CanvasView::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);
    if (m_selectDragOverlay)
        m_selectDragOverlay->setGeometry(rect());
    if (m_shapePreviewOverlay)
        m_shapePreviewOverlay->setGeometry(rect());
    if (m_distortOverlay) {
        m_distortOverlay->setGeometry(rect());
        if (m_distortActive)
            updateDistortOverlay();
    }
    if (m_gradientOverlay)
        m_gradientOverlay->setGeometry(rect());
    updateRulerGuideOverlay();
}

void CanvasView::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);
    updateSelectionAnimation();
}

void CanvasView::dragEnterEvent(QDragEnterEvent* e)
{
    const QStringList paths = extractValidDropImagePaths(e->mimeData());
    if (!paths.isEmpty()) {
        m_externalDragActive = true;
        setCursor(Qt::DragCopyCursor);
        update();
        e->acceptProposedAction();
        return;
    }
    e->ignore();
}

void CanvasView::dragMoveEvent(QDragMoveEvent* e)
{
    const QStringList paths = extractValidDropImagePaths(e->mimeData());
    if (!paths.isEmpty()) {
        if (!m_externalDragActive) {
            m_externalDragActive = true;
            setCursor(Qt::DragCopyCursor);
            update();
        }
        e->acceptProposedAction();
        return;
    }
    e->ignore();
}

void CanvasView::dragLeaveEvent(QDragLeaveEvent* e)
{
    Q_UNUSED(e)
    m_externalDragActive = false;
    updateToolCursor();
    update();
}

void CanvasView::dropEvent(QDropEvent* e)
{
    const QStringList paths = extractValidDropImagePaths(e->mimeData());
    m_externalDragActive = false;
    updateToolCursor();
    update();
    if (paths.isEmpty()) {
        e->ignore();
        return;
    }
    emit externalImagesDropped(paths, e->position());
    e->acceptProposedAction();
}

void CanvasView::wheelEvent(QWheelEvent* e)
{
    if (!m_doc) return;

    float factor = 1.1f;
    if (e->angleDelta().y() > 0)
        setZoom(m_doc->zoom * factor);
    else
        setZoom(m_doc->zoom / factor);

    // The selection drag overlays store screen points; a mid-gesture zoom
    // would otherwise leave the preview displaced from the real (doc-space)
    // path until the next mouse move.
    reprojectSelectionDragOverlay();
}

void CanvasView::leaveEvent(QEvent* e)
{
    QOpenGLWidget::leaveEvent(e);
    releaseTabletViewportCursor();
    // Cursor genuinely left the widget: drop tablet-active so the brush preview
    // gate (which keeps the cursor alive while the pen hovers) stops holding it on.
    m_inputState.noteTabletLeave();
    // Drop any armed contextual override; re-entering with the modifier still held
    // re-arms it via the mouse-move refresh.
    m_temporaryOverrideTool.reset();
    m_eyedropperHovering = false;
    if (m_eyedropperOverlay)
        m_eyedropperOverlay->setVisible(false);
    if (m_transformOverlay)
        m_transformOverlay->setVisible(false);
    if (m_brushPreviewOverlay)
        m_brushPreviewOverlay->clear();
    if (m_gradientOverlay && !m_gradientDragging)
        m_gradientOverlay->finish();
    if (m_rulerGuideOverlay) {
        m_rulerGuideOverlay->clearMouseDocumentPos();
        m_rulerGuideOverlay->setSelectedGuideIndex(-1);
    }
    clearSnapFeedback();
}

void CanvasView::focusOutEvent(QFocusEvent* e)
{
    QOpenGLWidget::focusOutEvent(e);
    disarmHeldModifierState();
}

// Every spring-loaded / modifier-held interaction (the contextual secondary-tool
// override from resolveTemporaryOverride(), and the Space → Hand pan) stays armed
// only until its key-release arrives. Losing keyboard focus — Alt+Tab to another
// app, a popup stealing focus — eats that release, so the armed state (and its
// cursor) would otherwise linger until the next mouse move re-ran the resolvers.
// This is the single place that force-disarms all of them: it can't consult
// QApplication::keyboardModifiers() (Alt may still read as held at focus-out time),
// so it resets unconditionally and snaps the cursor back to the selected tool.
// Re-entering with a modifier still held re-arms via the key-press / mouse-move
// refresh, so this stays general across tools rather than patching one of them.
void CanvasView::disarmHeldModifierState()
{
    bool changed = false;

    // Contextual override (table-driven: Alt over a paint tool → Eyedropper, etc.).
    if (m_temporaryOverrideTool) {
        m_temporaryOverrideTool.reset();
        m_eyedropperHovering = false;
        if (m_eyedropperOverlay)
            m_eyedropperOverlay->setVisible(false);
        changed = true;
    }

    // Space spring-pan. A live pan drag owns the input until the mouse release
    // finishes it, so only disarm the idle (armed-but-not-dragging) case.
    if (m_spacePanActive && !m_panning) {
        m_spacePanActive = false;
        changed = true;
    }

    // A modifier-sensitive cursor (clone/healing Alt → crosshair) must also revert:
    // the release that would clear it was eaten with the focus, so force the neutral
    // (no-modifier) state rather than trusting the possibly-still-held live state.
    if (changed || isCloneOrHealingTool()) {
        updateToolCursor(Qt::NoModifier);
        updateBrushPreviewOverlay();
        update();
    }
}

bool CanvasView::eventFilter(QObject* watched, QEvent* event)
{
    // Contextual override (Alt over a paint tool → Eyedropper, …) must follow the
    // modifier in real time — on the key press AND the key release — never waiting
    // for a mouse move. Alt frequently has keyboard focus elsewhere (a panel, or the
    // menu bar grabbing Alt for mnemonics), so the canvas's own key handlers don't
    // fire; this qApp-wide filter sees every key event regardless of focus. The
    // transitioning modifier is reported inconsistently in modifiers() during its own
    // press/release, so we set/clear its bit explicitly to get the settled state.
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (!ke->isAutoRepeat()) {
            Qt::KeyboardModifier bit = Qt::NoModifier;
            switch (ke->key()) {
            case Qt::Key_Alt:
            case Qt::Key_AltGr:   bit = Qt::AltModifier;     break;
            case Qt::Key_Shift:   bit = Qt::ShiftModifier;   break;
            case Qt::Key_Control: bit = Qt::ControlModifier; break;
            case Qt::Key_Meta:    bit = Qt::MetaModifier;    break;
            default: break;
            }
            if (bit != Qt::NoModifier) {
                Qt::KeyboardModifiers mods = ke->modifiers();
                if (event->type() == QEvent::KeyPress)
                    mods |= bit;
                else
                    mods &= ~bit;
                refreshTemporaryOverride(mods);
            }
        }
    }

    // Tablet blank-cursor override safety net (see installEventFilter in the ctor).
    // The override is application-wide, so as soon as the pointer reaches any widget
    // that isn't this canvas, restore the real cursor — otherwise the UI shows a
    // hidden cursor off-canvas. Re-entry re-arms it in tabletEvent. We only observe
    // here (never consume), so normal event delivery is untouched.
    if (m_tabletViewportCursorOverride && watched != this) {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter:
            // The pointer crossed INTO a different widget — a real boundary cross,
            // never emitted while moving within the canvas — so it is genuinely
            // off-canvas. Release unconditionally (covers scrollbars / panels that
            // overlap the canvas rect too).
            releaseTabletViewportCursor();
            break;
        case QEvent::MouseMove:
        case QEvent::TabletMove:
        case QEvent::HoverMove:
            // Move events also reach ancestor widgets (window / containers) while
            // the pen is still over the canvas. Releasing on those popped the
            // override every frame, flickering the cursor to the last one set.
            // Only release once the pointer is actually outside this widget.
            if (!rect().contains(mapFromGlobal(QCursor::pos())))
                releaseTabletViewportCursor();
            break;
        default:
            break;
        }
    }

    if (m_rulerGuideOverlay && watched == m_rulerGuideOverlay->parentWidget()
        && watched != this) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton
                && beginGuideInteraction(mouse->position()))
                return true;
            break;
        }
        case QEvent::MouseMove: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (m_guideDragging) {
                updateGuideInteraction(mouse->position());
                return true;
            }
            if (m_rulerGuideOverlay->hitHorizontalRuler(mouse->position())) {
                m_rulerGuideOverlay->parentWidget()->setCursor(Qt::SplitVCursor);
                return false;
            }
            if (m_rulerGuideOverlay->hitVerticalRuler(mouse->position())) {
                m_rulerGuideOverlay->parentWidget()->setCursor(Qt::SplitHCursor);
                return false;
            }
            m_rulerGuideOverlay->parentWidget()->unsetCursor();
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && m_guideDragging) {
                finishGuideInteraction(mouse->position());
                return true;
            }
            break;
        }
        case QEvent::Leave:
            if (!m_guideDragging)
                m_rulerGuideOverlay->parentWidget()->unsetCursor();
            break;
        default:
            break;
        }
    }

    return QOpenGLWidget::eventFilter(watched, event);
}

void CanvasView::toggleQuickMask()
{
    // Quick Mask only makes sense with an active, non-empty selection.
    if (!m_doc || !m_doc->selection.active() || m_doc->selection.isEmpty())
        return;
    m_quickMaskMode = !m_quickMaskMode;
    if (m_quickMaskMode) {
        m_quickMaskBefore = m_doc->selection.image().copy();
        if (m_currentTool != Tool::Brush && m_currentTool != Tool::Eraser)
            setTool(Tool::Brush);
    } else if (m_controller) {
        QImage after = m_doc->selection.image().copy();
        bool afterActive = m_doc->selection.active();
        m_controller->history().push(std::make_unique<SelectionCommand>(
            m_doc, m_quickMaskBefore, after,
            true, afterActive, "quick_mask_edit"));
    }
    markSelectMaskDirty();
    update();
}

void CanvasView::toggleMaskRubylith()
{
    if (!m_doc || !m_controller)
        return;
    auto* layer = m_doc->activeLayer();
    if (layer && !layer->maskImage.isNull())
        m_controller->toggleMaskOverlay();
}

void CanvasView::keyReleaseEvent(QKeyEvent* e)
{
    // Releasing a modifier disarms its contextual override immediately, snapping
    // the cursor and dispatch back to the selected tool.
    if (e->key() == Qt::Key_Alt || e->key() == Qt::Key_AltGr)
        refreshTemporaryOverride();

    // Release of the spring-loaded Space Hand. Ignore auto-repeat releases (a
    // held key emits release/press pairs). If a pan drag is mid-flight, let the
    // mouse release finish it; otherwise restore the active tool's cursor now.
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_spacePanActive) {
        m_spacePanActive = false;
        if (!m_panning)
            updateToolCursor();
        e->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

void CanvasView::keyPressEvent(QKeyEvent* e)
{
    // A modifier press may arm a contextual override (e.g. Alt over a paint tool
    // → Eyedropper). Refresh, then fall through so normal handling still runs.
    if (e->key() == Qt::Key_Alt || e->key() == Qt::Key_AltGr)
        refreshTemporaryOverride();

    // Caps Lock toggles the brush cursor between the circular size preview and a
    // precise crosshair. The OS sends a Key_CapsLock press each time the lock state
    // flips, so just toggle and refresh the cursor/preview.
    if (e->key() == Qt::Key_CapsLock && !e->isAutoRepeat()) {
        m_capsLockActive = !m_capsLockActive;
        if (isBrushBasedTool()) {
            updateToolCursor();
            updateBrushPreviewOverlay();
            update();
        }
        // Don't accept: let the platform keep its own Caps Lock handling.
    }

    // Esc cancels an in-flight AI selection (and any box-drag feedback).
    if (e->key() == Qt::Key_Escape && m_currentTool == Tool::AiSelect) {
        if (m_aiBoxDragging && m_selectDragOverlay)
            m_selectDragOverlay->finish();
        m_aiBoxDragging = false;
        m_aiMoved = false;
        if (m_aiSelectController)
            m_aiSelectController->cancelPending();
        e->accept();
        return;
    }

    if (e->key() == Qt::Key_Escape && m_currentTool == Tool::AiRemove) {
        cancelAiRemoveLasso();
        if (m_aiRemoveController)
            m_aiRemoveController->cancel();
        e->accept();
        return;
    }

    // Esc cancels an armed Hue/Saturation eyedropper without applying anything.
    if (e->key() == Qt::Key_Escape && m_hueSatPickMode != 0) {
        if (m_hueSatDragging) {
            m_hueSatDragging = false;
            emit hueSatPickDragEnded();
        }
        setHueSatPickMode(0);
        emit hueSatPickCancelled();
        e->accept();
        return;
    }

    if (m_textToolState == TextToolState::Editing && m_textEditor.isEditing()) {
        if (e->key() == Qt::Key_Escape) {
            commitTextEdit();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (!(e->modifiers() & Qt::ShiftModifier)) {
                commitTextEdit();
                return;
            }
            m_textEditor.newLine();
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Backspace) {
            m_textEditor.backspace();
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Delete) {
            m_textEditor.deleteChar();
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Left) {
            m_textEditor.moveLeft(e->modifiers() & Qt::ShiftModifier,
                                  e->modifiers() & Qt::ControlModifier);
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Right) {
            m_textEditor.moveRight(e->modifiers() & Qt::ShiftModifier,
                                   e->modifiers() & Qt::ControlModifier);
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Up) {
            m_textEditor.moveUp(e->modifiers() & Qt::ShiftModifier);
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Down) {
            m_textEditor.moveDown(e->modifiers() & Qt::ShiftModifier);
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_Home) {
            m_textEditor.moveHome(e->modifiers() & Qt::ShiftModifier);
            rerenderTextLayer();
            return;
        }
        if (e->key() == Qt::Key_End) {
            m_textEditor.moveEnd(e->modifiers() & Qt::ShiftModifier);
            rerenderTextLayer();
            return;
        }
        if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_A) {
            m_textEditor.selectAll();
            rerenderTextLayer();
            return;
        }
        if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_C) {
            m_textEditor.copy();
            return;
        }
        if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_X) {
            m_textEditor.cut();
            rerenderTextLayer();
            return;
        }
        if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_V) {
            m_textEditor.paste();
            rerenderTextLayer();
            return;
        }

        if (!e->text().isEmpty() && e->text()[0].isPrint()) {
            m_textEditor.insertText(e->text());
            rerenderTextLayer();
            return;
        }

        QOpenGLWidget::keyPressEvent(e);
        return;
    }

    // Space-bar = spring-loaded temporary Hand. Arm on the first press only
    // (auto-repeat presses are swallowed, not re-armed); the pan starts on mouse
    // drag and is released in keyReleaseEvent. The text-editing block above
    // already consumed a printable Space, so this never steals typing.
    if (e->key() == Qt::Key_Space) {
        if (!e->isAutoRepeat() && !m_spacePanActive) {
            m_spacePanActive = true;
            if (!m_panning)
                setCursor(Qt::OpenHandCursor);
        }
        e->accept();
        return;
    }

    if (m_currentTool == Tool::Gradient && m_gradientDragging) {
        if (e->key() == Qt::Key_Escape) {
            cancelGradientDrag();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            endGradientDrag(m_gradientCurrentScreen, e->modifiers());
            return;
        }
    }

    if (m_brushAdjustDragging) {
        if (e->key() == Qt::Key_Escape) {
            finishBrushAdjustDrag(true);
            return;
        }
        e->accept();
        return;
    }

    // Polygonal lasso editing keys (only while a polygon is being drawn).
    if (m_currentTool == Tool::Select && m_selectType == SelectType::PolygonalLasso
        && m_polyLassoDrawing) {
        switch (e->key()) {
        case Qt::Key_Escape:
            cancelPolyLasso();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_polyLassoPoints.size() >= 3)
                finalizePolyLasso();
            return;
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
            if (!m_polyLassoPoints.empty())
                m_polyLassoPoints.pop_back();
            if (m_polyLassoPoints.empty())
                cancelPolyLasso();
            else
                updatePolyLassoOverlay();
            return;
        default:
            break;
        }
    }

    if (e->modifiers() & Qt::ControlModifier) {
        if (e->key() == Qt::Key_T) {
            if (m_doc && m_doc->activeLayer()) {
                if (m_freeTransformActive)
                    commitFreeTransform();
                else
                    beginFreeTransform();
                return;
            }

            if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
                if (m_transformingSelection) {
                    // Confirm transform
                    m_transformingSelection = false;
                    m_selectTransformState.mode = InteractionMode::Idle;
                    update();
                } else {
                    // Enter transform selection mode
                    m_transformingSelection = true;
                    m_selectTransformBefore = m_doc->selection.image().copy();
                    m_selectTransformState.mode = InteractionMode::Idle;
                    m_selectTransformState.flatIndex = -1;
                    QRectF selBounds = m_doc->selection.bounds();
                    float dw = static_cast<float>(m_doc->size.width());
                    float dh = static_cast<float>(m_doc->size.height());
                    float cx = (2.0f * selBounds.center().x() / dw) - 1.0f;
                    float cy = 1.0f - (2.0f * selBounds.center().y() / dh);
                    m_selectTransformState.center = QPointF(cx, cy);
                    m_selectTransformState.startHw = selBounds.width() / dw;
                    m_selectTransformState.startHh = selBounds.height() / dh;
                    m_selectTransformState.startTransform = QTransform();
                    update();
                }
                return;
            }
        }
        if (e->key() == Qt::Key_C) {
            if (m_controller) m_controller->copy();
            return;
        }
        if (e->key() == Qt::Key_V) {
            if (m_controller) m_controller->paste();
            return;
        }
        if (e->key() == Qt::Key_Z)
            return;
    }

    // Transform selection keyboard controls
    if (m_transformingSelection && m_doc->selection.active()) {
        if (e->key() == Qt::Key_Escape) {
            m_doc->selection.image() = m_selectTransformBefore;
            m_doc->selection.setActive(!m_doc->selection.isEmpty());
            m_transformingSelection = false;
            m_selectTransformState.mode = InteractionMode::Idle;
            markSelectMaskDirty();
            update();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (m_controller) {
                QImage after = m_doc->selection.image().copy();
                bool afterActive = m_doc->selection.active();
                m_controller->history().push(std::make_unique<SelectionCommand>(
                    m_doc, m_selectTransformBefore, after,
                    true, afterActive, "transform_selection"));
            }
            m_transformingSelection = false;
            m_selectTransformState.mode = InteractionMode::Idle;
            markSelectMaskDirty();
            update();
            return;
        }
        // Keyboard nudging / scaling / rotating
        int dx = 0, dy = 0;
        float dScale = 0.0f;
        float dAngle = 0.0f;
        int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;

        if (e->key() == Qt::Key_Left) dx = -step;
        else if (e->key() == Qt::Key_Right) dx = step;
        else if (e->key() == Qt::Key_Up) dy = -step;
        else if (e->key() == Qt::Key_Down) dy = step;
        else if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) dScale = 0.05f;
        else if (e->key() == Qt::Key_Minus) dScale = -0.05f;
        else if (e->key() == Qt::Key_BracketLeft) dAngle = -1.0f;
        else if (e->key() == Qt::Key_BracketRight) dAngle = 1.0f;

        if (dx != 0 || dy != 0 || dScale != 0.0f || dAngle != 0.0f) {
            m_doc->selection.image() = m_selectTransformBefore.copy();
            QRectF selBounds = m_doc->selection.bounds();
            float centerX = selBounds.center().x();
            float centerY = selBounds.center().y();
            float scaleX = 1.0f + dScale;
            float scaleY = 1.0f + dScale;
            // Apply translate via mask shift, or re-apply transform
            if (dScale != 0.0f || dAngle != 0.0f)
                m_doc->selection.applyTransform(scaleX, scaleY, dAngle,
                    static_cast<int>(centerX), static_cast<int>(centerY));
            if (dx != 0 || dy != 0)
                m_doc->selection.translate(dx, dy);
            m_doc->selection.setActive(!m_doc->selection.isEmpty());
            markSelectMaskDirty();
            update();
            return;
        }
    }

    // Escape cancels selection, box selection, or crop
    if (e->key() == Qt::Key_Escape) {
        if (m_distortActive) {
            cancelDistort();
            return;
        }
        if (m_freeTransformActive) {
            cancelFreeTransform();
            return;
        }
        if (m_boxSelecting) {
            m_boxSelecting = false;
            update();
            return;
        }
        if (m_currentTool == Tool::Shape && m_shapeDragging) {
            cancelShapeDrag();
            return;
        }
        if (m_currentTool == Tool::Crop && m_cropActive) {
            resetCrop();
            update();
            return;
        }
        if (m_doc->selection.active() && m_currentTool == Tool::Select && !m_quickMaskMode && !m_transformingSelection) {
            QImage before = m_doc->selection.image().copy();
            m_doc->selection.clear();
            m_doc->selection.setActive(false);
            markSelectMaskDirty();
            if (m_controller) {
                m_controller->history().push(std::make_unique<SelectionCommand>(
                    m_doc, before, m_doc->selection.image().copy(),
                    true, false, "deselect"));
                emit m_controller->selectionChanged();
            }
            update();
            return;
        }
    }

    // Enter commits crop
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_distortActive) {
            commitDistort();
            return;
        }
        if (m_freeTransformActive) {
            commitFreeTransform();
            return;
        }
        if (m_currentTool == Tool::Crop && m_cropActive) {
            commitCropAction();
            return;
        }
    }

    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (m_currentTool == Tool::Text) {
            return;
        }
        // With selection: delete pixels; without: delete layer(s)
        if (m_controller && m_doc->selection.active() && !m_doc->selection.isEmpty()) {
            m_controller->executeTool("delete_selected", {});
        } else if (m_controller) {
            m_controller->removeSelectedNodes();
        }
        return;
    }

    // Move Tool: nudge active/selected layer(s) with arrow keys (1px, or Shift=10px)
    if (m_currentTool == Tool::Move && !m_freeTransformActive && m_doc && m_controller) {
        const bool isArrow = e->key() == Qt::Key_Left || e->key() == Qt::Key_Right
                          || e->key() == Qt::Key_Up   || e->key() == Qt::Key_Down;
        if (isArrow) {
            const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
            int dx = 0, dy = 0;
            if      (e->key() == Qt::Key_Left)  dx = -step;
            else if (e->key() == Qt::Key_Right) dx =  step;
            else if (e->key() == Qt::Key_Up)    dy = -step;
            else if (e->key() == Qt::Key_Down)  dy =  step;

            const auto nudgeIndices = collectTransformableSelectedIndices(false);
            std::vector<int> validIndices;
            std::vector<QTransform> beforeXfs, afterXfs;
            for (int idx : nudgeIndices) {
                auto* node = m_doc->nodeAt(idx);
                if (!node) continue;
                validIndices.push_back(idx);
                beforeXfs.push_back(node->transform());
                translateNodeByDocumentDelta(node, QPointF(dx, dy));
                afterXfs.push_back(node->transform());
            }
            if (!validIndices.empty())
                m_controller->setNodeTransforms(validIndices, afterXfs, beforeXfs);
            e->accept();
            return;
        }
    }

    if (handleBrushShortcutKey(e))
        return;

    // Q (Quick Mask) and "\" (rubylith overlay) are now owned by the
    // ShortcutManager dispatcher (canvas.quick_mask / canvas.edit_mask), which
    // the qApp event filter resolves before this handler runs.
    switch (e->key()) {
    default: {
        // Check ShortcutManager for tool keys
        auto* scMgr = ShortcutManager::instance();
        struct ToolShortcut {
            const char* id;
            Tool tool;
            int stampMode = -1;
            int selectType = -1;
        };
        static constexpr ToolShortcut toolShortcuts[] = {
            {"tool.move",          Tool::Move},
            {"tool.brush",         Tool::Brush},
            {"tool.eraser",        Tool::Eraser},
            {"tool.select",        Tool::Select},
            {"tool.selection.marquee_rect",    Tool::Select, -1, static_cast<int>(SelectType::Rectangular)},
            {"tool.selection.marquee_ellipse", Tool::Select, -1, static_cast<int>(SelectType::Elliptical)},
            {"tool.selection.lasso",           Tool::Select, -1, static_cast<int>(SelectType::Lasso)},
            {"tool.selection.polygonal_lasso", Tool::Select, -1, static_cast<int>(SelectType::PolygonalLasso)},
            {"tool.selection.magnetic_lasso",  Tool::Select, -1, static_cast<int>(SelectType::MagneticLasso)},
            {"tool.selection.quick_selection", Tool::Select, -1, static_cast<int>(SelectType::QuickSelect)},
            {"tool.selection.magic_wand",      Tool::Select, -1, static_cast<int>(SelectType::MagicWand)},
            {"tool.zoom",          Tool::Zoom},
            {"tool.hand",          Tool::Hand},
            {"tool.text",          Tool::Text},
            {"tool.crop",          Tool::Crop},
            {"tool.fill_bucket",   Tool::FillBucket},
            {"tool.eyedropper",    Tool::Eyedropper},
            {"tool.shape",         Tool::Shape},
            {"tool.clone_stamp",   Tool::CloneStamp, static_cast<int>(StampMode::Clone)},
            {"tool.gradient",      Tool::Gradient},
            {"tool.skew",          Tool::Skew},
            {"tool.ai_select",     Tool::AiSelect},
            // Temporarily hidden: AI Remove Tool will be released later.
            // Do not remove the implementation. Only the user-facing UI is disabled.
            // {"tool.ai_remove",     Tool::AiRemove},
            {"tool.healing_brush", Tool::CloneStamp, static_cast<int>(StampMode::Healing)}
        };
        QKeySequence keySeq(e->modifiers() | e->key());
        bool handled = false;
        for (const auto& toolShortcut : toolShortcuts) {
            QKeySequence seq = scMgr->currentShortcut(QString::fromLatin1(toolShortcut.id));
            if (!seq.isEmpty() && seq == keySeq) {
                if (toolShortcut.stampMode >= 0)
                    setStampMode(toolShortcut.stampMode);
                setTool(toolShortcut.tool);
                if (toolShortcut.selectType >= 0)
                    setSelectType(static_cast<SelectType>(toolShortcut.selectType));
                handled = true;
                break;
            }
        }
        if (!handled)
            QOpenGLWidget::keyPressEvent(e);
        break;
    }
    }
}

void CanvasView::setSelectType(SelectType t)
{
    // Discard any in-progress polygon when switching to another selection type.
    if (m_polyLassoDrawing && t != SelectType::PolygonalLasso)
        cancelPolyLasso();
    const bool changed = m_selectType != t;
    m_selectType = t;
    // Show/hide the circular brush cursor immediately when entering/leaving
    // Quick Selection (instead of waiting for the next mouse move).
    updateBrushPreviewOverlay();
    updateToolCursor();
    if (changed)
        emit selectTypeChanged(static_cast<int>(m_selectType));
}

void CanvasView::polyLassoMousePress(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;

    const QPointF docPos = screenToDocument(e->position());

    if (!m_polyLassoDrawing) {
        // Capture the combine mode once, at the first click; Shift afterwards is
        // reserved for the 45-degree constraint while drawing.
        const Qt::KeyboardModifiers mods = e->modifiers();
        if ((mods & Qt::ShiftModifier) && (mods & Qt::AltModifier))
            m_polyLassoMode = SelectMode::Intersect;
        else if (mods & Qt::ShiftModifier)
            m_polyLassoMode = SelectMode::Add;
        else if (mods & Qt::AltModifier)
            m_polyLassoMode = SelectMode::Subtract;
        else
            m_polyLassoMode = SelectMode::Replace;

        m_polyLassoDrawing = true;
        m_polyLassoCanClose = false;
        m_polyLassoPoints.clear();
        m_polyLassoPoints.push_back(docPos);
        m_polyLassoPreviewDoc = docPos;
        setFocus();
        updatePolyLassoOverlay();
        return;
    }

    // Already drawing: a click near the first vertex (with 3+ points) closes the
    // polygon; otherwise it adds a new vertex.
    if (m_polyLassoPoints.size() >= 3) {
        QPointF firstScreen = documentToScreen(m_polyLassoPoints.front());
        QPointF d = firstScreen - e->position();
        if (std::sqrt(d.x() * d.x() + d.y() * d.y()) <= 8.0) {
            finalizePolyLasso();
            return;
        }
    }

    QPointF pt = docPos;
    if ((e->modifiers() & Qt::ShiftModifier) && !m_polyLassoPoints.empty())
        pt = constrainAngle45(m_polyLassoPoints.back(), pt);

    // Reject duplicate consecutive vertices.
    if (m_polyLassoPoints.empty() || pt != m_polyLassoPoints.back())
        m_polyLassoPoints.push_back(pt);
    m_polyLassoPreviewDoc = pt;
    updatePolyLassoOverlay();
}

void CanvasView::finalizePolyLasso()
{
    m_polyLassoDrawing = false;
    m_polyLassoCanClose = false;
    if (m_polyLassoPoints.size() >= 3) {
        m_doc->selection.setPolygon(m_polyLassoPoints, m_polyLassoMode,
                                    m_selectAntiAlias);
        m_doc->selection.setActive(true);
        markSelectMaskDirty();
        pushSelectionGestureUndo("polygonal_lasso");
    }
    m_polyLassoPoints.clear();
    if (m_selectDragOverlay)
        m_selectDragOverlay->finish();
    update();
}

void CanvasView::cancelPolyLasso()
{
    m_polyLassoDrawing = false;
    m_polyLassoCanClose = false;
    m_polyLassoPoints.clear();
    if (m_selectDragOverlay)
        m_selectDragOverlay->finish();
    update();
}

void CanvasView::updatePolyLassoOverlay()
{
    if (!m_selectDragOverlay) {
        update();
        return;
    }
    QVector<QPointF> screenPts;
    screenPts.reserve(static_cast<int>(m_polyLassoPoints.size()));
    for (const QPointF& pt : m_polyLassoPoints)
        screenPts.push_back(documentToScreen(pt));
    m_selectDragOverlay->setPolyLasso(screenPts,
                                      documentToScreen(m_polyLassoPreviewDoc),
                                      m_polyLassoCanClose);
}

// True if any visible Normal-Mode (stack) adjustment uses a non-Normal blend
// mode. The GPU per-layer preview applies every adjustment *parameter* exactly
// like the CPU apply() (curves/colour-balance/hue-sat LUTs, opacity, mask,
// preserve-luminosity, colorize), but it draws a non-Normal blend on the
// adjustment node itself as Normal (a documented LayerCompositor limit). Only
// stack adjustments hit that path — Single-Layer-Mode (clipped) adjustments are
// baked into the parent's effected texture by the CPU, so the walk descends into
// Groups but never into Layer children, mirroring treeHasStackAdjustment().
static bool hasBlendedStackAdjustment(
    const std::vector<std::unique_ptr<LayerTreeNode>>& nodes)
{
    for (const auto& n : nodes) {
        if (!n->isVisible()) continue;
        if (n->isAdjustmentLayer() && n->blendMode() != BlendMode::Normal)
            return true;
        if (n->type == LayerTreeNode::Type::Group
            && hasBlendedStackAdjustment(n->children))
            return true;
    }
    return false;
}

void CanvasView::setAdjustmentLiveEdit(bool on)
{
    // Keep the CPU projection (visual source of truth) for the rare case the GPU
    // preview can't reproduce — a non-Normal blend on a stack adjustment — so the
    // drag preview never diverges from the committed result.
    if (on && m_doc && hasBlendedStackAdjustment(m_doc->roots))
        on = false;
    if (m_adjustmentLiveEdit == on)
        return;
    m_adjustmentLiveEdit = on;
    // Flip the render path for the next frame: while live (true) the GPU
    // per-layer compositor previews the adjustment cheaply; on commit (false)
    // the canvas falls back to the cached CPU projection, which has just
    // recomposited the final-quality result.
    update();
}

void CanvasView::setCurvesPickMode(int mode)
{
    if (m_curvesPickMode == mode)
        return;
    m_curvesPickMode = mode;
    if (mode != 4)
        m_curvesTargetDragging = false;
    if (mode != 0)
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();
}

void CanvasView::setHueSatPickMode(int mode)
{
    if (m_hueSatPickMode == mode)
        return;
    m_hueSatPickMode = mode;
    if (mode == 0)
        m_hueSatDragging = false;
    if (mode != 0)
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();
}

void CanvasView::mousePressEvent(QMouseEvent* e)
{
    // Drop the OS's synthetic pen-mouse twin: tabletEvent already drove this input.
    if (!m_synthesizingMouseFromTablet && isPenSyntheticMouse(e)) {
        e->accept();
        return;
    }
    if (!m_synthesizingMouseFromTablet) {
        PointerInputEvent me = CanvasInputMapper::fromMouseEvent(e, PointerPhase::Press);
        m_inputState.noteMouseEvent(me);
    }
    // Space-bar temporary Hand: while Space is held, a left-drag pans the view
    // regardless of the active tool — without switching tools, so a crop /
    // transform / text session in progress is preserved. The generic m_panning
    // path (mouseMove / mouseRelease) finishes the gesture.
    if (m_spacePanActive && e->button() == Qt::LeftButton) {
        m_panning = true;
        m_rightButtonDown = false;
        m_lastMousePos = e->position();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    // Curves editor eyedropper/target: report the document-space click position.
    // The editor samples its own input composite (with the curves layer bypassed)
    // so picks read the curve's INPUT and never accumulate the curve's own effect.
    // Black/gray/white (1/2/3) are one-shot; target (4) starts a drag session and
    // stays armed. Fires regardless of the active tool.
    if (m_curvesPickMode != 0 && e->button() == Qt::LeftButton && m_doc) {
        const QPointF docPos = screenToDocument(e->position());
        if (m_curvesPickMode == 4) {
            m_curvesTargetDragging = true;
            m_curvesTargetStartY = e->position().y();
            emit curvesTargetBegan(docPos);
        } else {
            const int mode = m_curvesPickMode;
            setCurvesPickMode(0);
            emit curvesPickRequested(docPos, mode);
        }
        e->accept();
        return;
    }

    // Hue/Saturation editor eyedropper: report the document-space click. The
    // editor samples its bypassed INPUT composite there. Main (1) is one-shot
    // and stays armed; add/subtract (2/3) open a press-drag-release gesture so
    // a drag can sample many hues but commits a single undo step.
    if (m_hueSatPickMode != 0 && e->button() == Qt::LeftButton && m_doc) {
        const QPointF docPos = screenToDocument(e->position());
        if (m_hueSatPickMode == 1) {
            emit hueSatPickClicked(docPos, 1);
        } else {
            m_hueSatDragging = true;
            emit hueSatPickDragBegan(docPos, m_hueSatPickMode);
        }
        e->accept();
        return;
    }

    // AI Object Selection: a left press starts a click/box gesture (the heavy SAM
    // inference runs async on release). Right/middle still pan via the block below.
    // Canvas input is dispatched to the EFFECTIVE tool: when a contextual override
    // is armed (e.g. Alt over a paint tool → Eyedropper) the press is processed by
    // the override, while the selected tool and toolbar stay put.
    const Tool tool = effectiveTool();

    if (tool == Tool::AiSelect && e->button() == Qt::LeftButton && m_doc) {
        aiSelectMousePress(e);
        e->accept();
        return;
    }

    if (tool == Tool::AiRemove && e->button() == Qt::LeftButton && m_doc) {
        aiRemoveMousePress(e);
        e->accept();
        return;
    }

    // Alt+Right over a paint tool is the brush size/hardness HUD — it must keep
    // reading the SELECTED tool so it still fires while Alt arms the Eyedropper
    // override (Alt-left picks a colour, Alt-right drags the HUD).
    if (isBrushBasedTool()
        && e->button() == Qt::RightButton
        && (e->modifiers() & Qt::AltModifier)) {
        if (beginBrushAdjustDrag(e->position())) {
            e->accept();
            return;
        }
    }

    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (e->button() == Qt::RightButton && tool != Tool::Eyedropper) {
        m_rightButtonDown = true;
        m_rightPressPos = e->position();
        return;
    }

    if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton) return;
    if (e->button() != Qt::LeftButton && tool != Tool::Eyedropper) return;

    if (e->button() == Qt::LeftButton
        && beginGuideInteraction(canvasToRulerOverlayPoint(e->position())))
        return;

    if (tool == Tool::Zoom) {
        float factor = 1.5f;
        float newZoom = (e->modifiers() & Qt::AltModifier)
            ? m_doc->zoom / factor
            : m_doc->zoom * factor;
        setZoom(newZoom);
        return;
    }

    if (tool == Tool::Hand) {
        m_panning = true;
        m_lastMousePos = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (tool == Tool::Crop) {
        m_cropDragging = true;
        m_cropHandle = cropHandleAtScreen(e->position());
        if (m_cropHandle == CropHandleId::None)
            setCursor(m_cursors[static_cast<int>(Tool::Crop)]);
        else
            setCursor(cropCursorForHandle(m_cropHandle));
        m_cropDragStart = e->position();
        m_cropDragOrigRect = m_cropRect;
        m_cropRotating = false;

        if (m_cropHandle == CropHandleId::None) {
            // Click outside arms a new crop rect, but does not replace the current
            // rect until the pointer actually moves.
            QPointF ndcStart = screenToCanvasNdc(e->position());
            ndcStart.setX(std::clamp(static_cast<float>(ndcStart.x()), -1.0f, 1.0f));
            ndcStart.setY(std::clamp(static_cast<float>(ndcStart.y()), -1.0f, 1.0f));
        }
        update();
        return;
    }

    if (tool == Tool::Text) {
        if (m_textToolState == TextToolState::Editing) {
            int hitIndex = pickLayerAtScreenPos(e->position());
            auto* node = hitIndex >= 0 ? m_doc->nodeAt(hitIndex) : nullptr;
            if (node && node->layer && node->layer->textData) {
                m_textLayerIndex = hitIndex;
                m_controller->setActiveNode(hitIndex);
                int charIndex = textLayerCharAt(node, e->position());
                if (e->modifiers() & Qt::ShiftModifier) {
                    // Shift+Click extends the selection from the existing anchor.
                    int anchor = m_textEditor.hasSelection()
                        ? m_textEditor.selStart() : m_textEditor.cursorPos();
                    m_textDragStart = anchor;
                    m_textEditor.setSelection(anchor, charIndex);
                    m_textEditor.setCursorPos(charIndex);
                } else {
                    m_textDragStart = charIndex;
                    m_textEditor.clearSelection();
                    m_textEditor.setCursorPos(charIndex);
                }
                rerenderTextLayer();
                updateToolCursor();
            } else {
                commitTextEdit();
                m_controller->setActiveNode(-1);
                update();
                return;
            }
        } else {
            int hitIndex = pickLayerAtScreenPos(e->position());
            auto* hitNode = hitIndex >= 0 ? m_doc->nodeAt(hitIndex) : nullptr;
            if (hitNode && hitNode->layer && hitNode->layer->textData
                && !hitNode->canEditContent()) {
                // Locked content: allow selecting the layer, but not editing it.
                m_controller->setActiveNode(hitIndex);
                emit m_controller->operationBlocked(
                    tr("The content of this text layer is locked."));
                update();
                return;
            }
            if (hitNode && hitNode->layer && hitNode->layer->textData) {
                m_textLayerIndex = hitIndex;
                m_textBeforeSnapshot = *hitNode->layer->textData;
                m_textBeforeTransform = hitNode->transform();
                m_controller->setActiveNode(hitIndex);
                m_textToolState = TextToolState::Editing;
                m_textEditor.beginEdit(hitNode->layer->textData.get());
                m_textEditor.setCursorPos(textLayerCharAt(hitNode, e->position()));
                rerenderTextLayer();
                updateToolCursor();
            } else {
                m_textToolState = TextToolState::Creating;
                m_textCreateStart = e->position();
                m_textCreatingBox = false;
                if (m_selectDragOverlay)
                    m_selectDragOverlay->beginRect(e->position(), false);
            }
        }
        return;
    }

    if (tool == Tool::Shape) {
        QPointF ndcStart = screenToCanvasNdc(e->position());
        beginShapeDrag(ndcStart);
        return;
    }

    if (m_doc->flatCount() == 0) {
        if (tool == Tool::Move && m_controller) {
            m_controller->setActiveNode(-1);
            update();
        }
        // Brush, Fill Bucket and Gradient still auto-create a layer on an empty
        // document (handled further down via ensurePaintTargetLayer) — every
        // other tool requires an existing layer and bails out here. The Eraser
        // is excluded too: there is nothing to erase on a fresh empty layer.
        if (tool != Tool::Brush && tool != Tool::FillBucket && tool != Tool::Gradient)
            return;
    }

    // Distort/Perspective (Skew tool) owns all mouse handling while active: a
    // press either grabs a corner handle or is ignored (commit is explicit).
    if (tool == Tool::Skew && m_distortActive) {
        if (e->button() == Qt::LeftButton)
            distortMousePress(e);
        return;
    }

    if (tool == Tool::Move) {
        // Always reset multi-move cache at the beginning of a new move gesture.
        m_multiMoveIndices.clear();
        m_multiMoveStartTransforms.clear();
        clearSnapBoundsCache();

        auto* activeNode = m_doc->activeNode();
        bool activeIsGroup = activeNode
            && activeNode->type == LayerTreeNode::Type::Group;
        bool onHandle = false;
        bool insideBody = false;
        bool onRotateZone = false;
        const bool transformControlsVisible = m_showTransformControls || m_freeTransformActive;

        if (transformControlsVisible && activeNode
            && (activeNode->layer || activeIsGroup)) {
            QPolygonF corners;
            if (m_doc->selectedFlatIndices.size() > 1) {
                // Shared multi-selection frame (layers AND groups). When the
                // cached outline doesn't match the selection, recompute it from
                // every transformable participant — same code path paintGL uses,
                // so the drawn box and the grabbed box always agree.
                if (!multiOutlineMatchesSelection())
                    computeMultiSelectionBbox();
                if (m_multiResizeGroupBboxValid) {
                    QPointF metric(std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                                   std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
                    QTransform groupXf = TransformController::composeVisual(
                        m_multiResizeGroupBboxHw * metric.x(),
                        m_multiResizeGroupBboxHh * metric.y(),
                        QPointF(m_multiResizeGroupBboxCenter.x() * metric.x(),
                                m_multiResizeGroupBboxCenter.y() * metric.y()),
                        m_multiResizeGroupBboxRotation,
                        m_canvasHalfExtents,
                        size());
                    corners = TransformController::cornersFromTransform(groupXf);

                    m_multiResizeGroupStartCenterVis = QPointF(
                        m_multiResizeGroupBboxCenter.x() * metric.x(),
                        m_multiResizeGroupBboxCenter.y() * metric.y());
                    m_multiResizeGroupStartHwVis = m_multiResizeGroupBboxHw * metric.x();
                    m_multiResizeGroupStartHhVis = m_multiResizeGroupBboxHh * metric.y();
                }
                // No transformable participant → corners stays empty, so the
                // handle/rotate hit-tests below fail and the press falls
                // through to move/auto-select.
            } else if (activeIsGroup) {
                // Single group: the transform is applied to the group node itself
                // (children follow via accumulatedTransform). The start frame is
                // the group's fresh visual frame (local-AABB × accumulated), which
                // carries the group's current rotation/scale — same model as a
                // single layer's frame, so resize-after-rotate behaves identically.
                m_groupTransformStart = activeNode->transform();
                m_groupParentAccum = QTransform();
                for (auto* p = activeNode->parent; p; p = p->parent)
                    m_groupParentAccum = m_groupParentAccum * p->transform();

                // The group transforms as a single node — the per-leaf multi-resize
                // loops must never fire for it.
                m_multiResizeIndices.clear();
                m_multiResizeStartTransforms.clear();

                bool gok = false;
                const QTransform gf = groupFrameTransform(activeNode, &gok);
                if (gok) {
                    corners = TransformController::cornersFromTransform(gf);
                    float gHw, gHh, gRot; QPointF gC;
                    TransformController::decomposeVisual(gf, m_canvasHalfExtents,
                                                         size(), gHw, gHh, gC, gRot);
                    QPointF metric(std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                                   std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
                    m_multiResizeGroupBboxCenter = QPointF(gC.x() / metric.x(),
                                                           gC.y() / metric.y());
                    m_multiResizeGroupBboxHw = gHw / metric.x();
                    m_multiResizeGroupBboxHh = gHh / metric.y();
                    m_multiResizeGroupBboxRotation = gRot;
                    m_multiResizeGroupBboxValid = true;
                    m_multiResizeGroupStartCenterVis = gC;
                    m_multiResizeGroupStartHwVis = gHw;
                    m_multiResizeGroupStartHhVis = gHh;
                } else {
                }
            } else {
                corners = TransformController::cornersFromNode(activeNode);
            }
            QPointF screenPos = e->position();

            float ndcX = 2.0f * static_cast<float>(screenPos.x()) / width() - 1.0f;
            float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / height();

            // Transform corners from canvas-NDC to screen-NDC
            QMatrix4x4 mvp;
            mvp.translate(m_doc->panOffset.x(), m_doc->panOffset.y());
            mvp.scale(m_doc->zoom);
            mvp.scale(m_canvasHalfExtents.x(), m_canvasHalfExtents.y());

            QPolygonF screenCorners;
            for (auto& p : corners) {
                QVector4D v = mvp * QVector4D(static_cast<float>(p.x()),
                                               static_cast<float>(p.y()), 0, 1);
                screenCorners << QPointF(v.x(), v.y());
            }

            onRotateZone = isInRotateZone(screenCorners, screenPos);
            if (onRotateZone && activeNode->canTransform()) {
                m_transformState.mode = InteractionMode::Rotating;
                m_transformState.activeHandle = HandlePosition::None;
                m_transformState.flatIndex = m_doc->activeFlatIndex;
                m_transformState.startMouseScreen = screenPos;
                m_transformState.startTransform = activeNode->transform();
                if (!m_freeTransformActive
                    && activeNode->layer && activeNode->layer->textData) {
                    // The release commit compares against this snapshot for
                    // text layers — rotate gestures need it too, not just
                    // resize/move. During a free-transform session the
                    // begin-of-session snapshot must survive across gestures.
                    m_textLayerIndex = m_doc->activeFlatIndex;
                    m_textBeforeSnapshot = *activeNode->layer->textData;
                    m_textBeforeTransform = activeNode->transform();
                }
                // Start frame is the node's WORLD (accumulated) frame so resize/
                // rotate work in world space for nested layers too (root: identical
                // to local). Application removes the parent chain to get local.
                // visualFrameForNode() narrows it to the rendered box (content
                // bounds for raster) so the rotate pivot is the visible box
                // center, not the full base center — otherwise a small dab on a
                // full-canvas layer orbits the canvas center.
                TransformController::decomposeVisual(TransformController::visualFrameForNode(activeNode),
                    m_canvasHalfExtents, size(),
                    m_transformState.startHw, m_transformState.startHh,
                    m_transformState.center, m_transformState.rotation);
                if (m_doc->selectedFlatIndices.size() > 1 || activeIsGroup) {
                    m_transformState.startHw = m_multiResizeGroupStartHwVis;
                    m_transformState.startHh = m_multiResizeGroupStartHhVis;
                    m_transformState.center = m_multiResizeGroupStartCenterVis;
                    m_transformState.rotation = m_multiResizeGroupBboxRotation;
                }
                m_moving = true;
                setCursor(Qt::CrossCursor);
                update();
                if (m_doc->selectedFlatIndices.size() > 1)
                    captureMultiResizeStartTransforms();
                return;
            }

            HandlePosition handle = TransformController::handleAt(
                screenPos, screenCorners, size());

            if (handle != HandlePosition::None && handle != HandlePosition::Center) {
                onHandle = true;
                if (activeNode->canTransform()) {
                    m_transformState.mode = InteractionMode::Resizing;
                    m_transformState.activeHandle = handle;
                    m_transformState.flatIndex = m_doc->activeFlatIndex;
                    m_transformState.startMouseScreen = screenPos;
                    m_transformState.startTransform = activeNode->transform();
                    // Per-gesture snapshot — but never overwrite the session
                    // snapshot while a free-transform session is open.
                    if (!m_freeTransformActive
                        && activeNode->layer && activeNode->layer->textData) {
                        m_textLayerIndex = m_doc->activeFlatIndex;
                        m_textBeforeSnapshot = *activeNode->layer->textData;
                        m_textBeforeTransform = activeNode->transform();
                    }

                    // World (accumulated) start frame, narrowed to the rendered
                    // box via visualFrameForNode() — see rotate-grab note above.
                    // This puts the scale anchor on the visible box corner; with
                    // the raw accumulated frame a sparsely-painted layer anchors
                    // at the full-base corner and jumps/shrinks on the first drag.
                    TransformController::decomposeVisual(TransformController::visualFrameForNode(activeNode),
                        m_canvasHalfExtents, size(),
                        m_transformState.startHw, m_transformState.startHh,
                        m_transformState.center, m_transformState.rotation);

                    if (m_doc->selectedFlatIndices.size() > 1 || activeIsGroup) {
                        m_transformState.startHw = m_multiResizeGroupStartHwVis;
                        m_transformState.startHh = m_multiResizeGroupStartHhVis;
                        m_transformState.center = m_multiResizeGroupStartCenterVis;
                        m_transformState.rotation = m_multiResizeGroupBboxRotation;
                    }
                    QPointF anchorCanvas;
                    float hx = 0, hy = 0;
                    switch (handle) {
                    case HandlePosition::TopLeft:      hx = -1; hy = -1; break;
                    case HandlePosition::Top:          hx =  0; hy = -1; break;
                    case HandlePosition::TopRight:     hx =  1; hy = -1; break;
                    case HandlePosition::Right:        hx =  1; hy =  0; break;
                    case HandlePosition::BottomRight:  hx =  1; hy =  1; break;
                    case HandlePosition::Bottom:       hx =  0; hy =  1; break;
                    case HandlePosition::BottomLeft:   hx = -1; hy =  1; break;
                    case HandlePosition::Left:         hx = -1; hy =  0; break;
                    default: break;
                    }

                    float a = m_transformState.rotation;
                    float c = std::cos(a);
                    float s = std::sin(a);
                    m_transformState.anchorCanvas = m_transformState.center
                        - QPointF(c * m_transformState.startHw * hx
                                   - s * m_transformState.startHh * hy,
                                   s * m_transformState.startHw * hx
                                   + c * m_transformState.startHh * hy);

                    // Snapshot the bounds so resize snap can tell which edges
                    // move (vs the fixed anchor) regardless of handle/axis
                    // orientation.
                    m_resizeStartDocBounds = currentTransformDocumentBounds();

                    m_moving = true;
                    setCursor(TransformController::cursorForHandle(handle));
                    update();
                    if (m_doc->selectedFlatIndices.size() > 1)
                        captureMultiResizeStartTransforms();
                    return;
                }
            }

            if (!onHandle) {
                insideBody = (handle == HandlePosition::Center);
            }
        }

        // For a group: move it only when the click lands on its own content
        // (any descendant layer). Clicking elsewhere must fall through to
        // auto-select so other layers stay selectable on the canvas.
        // (Handles / rotate-zone already returned above.)
        if (activeIsGroup && activeNode && !onHandle && !onRotateZone) {
            // With auto-select OFF, clicking anywhere moves the active group (like
            // a single layer). With auto-select ON, only a click on the group's
            // own content moves it; otherwise fall through to auto-select.
            bool clickedOnGroupContent = !m_autoSelect;
            int hitIndex = -1;
            if (m_autoSelect) {
                hitIndex = pickLayerAtScreenPos(e->position(), true);
                if (hitIndex >= 0) {
                    for (auto* walk = m_doc->nodeAt(hitIndex); walk; walk = walk->parent) {
                        if (walk == activeNode) { clickedOnGroupContent = true; break; }
                    }
                }
            }
            if (clickedOnGroupContent) {
                if (activeNode->isPositionLocked()) {
                    update();
                    return;
                }
                m_transformState.mode = InteractionMode::Moving;
                m_transformState.flatIndex = m_doc->activeFlatIndex;
                m_transformState.startMouseScreen = e->position();
                m_transformState.startTransform = activeNode->transform();
                TransformController::decomposeVisual(activeNode->transform(),
                    m_canvasHalfExtents, size(),
                    m_transformState.startHw, m_transformState.startHh,
                    m_transformState.center, m_transformState.rotation);
                m_moving = true;
                prepareSnapMoveBounds();
                setCursor(Qt::SizeAllCursor);
                update();
                return;
            }
            // Not on the group's content → ensure auto-select runs below.
            insideBody = false;
        }

        if (!onHandle && !insideBody && !onRotateZone) {
            if (!m_autoSelect) {
                if (m_doc->selectedFlatIndices.empty()
                    || !m_doc->activeNode()
                    || (!activeIsGroup && !m_doc->activeNode()->layer)
                    || (m_doc->activeNode()->isPositionLocked())) {
                    m_boxSelecting = true;
                    m_boxSelectStart = screenToCanvasNdc(e->position());
                    m_boxSelectCurrent = m_boxSelectStart;
                    update();
                    return;
                }
            } else {
                int hitIndex = pickLayerAtScreenPos(e->position(), true);
                if (hitIndex >= 0) {
                    if (m_autoSelectGroup && m_doc->flatCount() > 0) {
                        auto* hitNode = m_doc->nodeAt(hitIndex);
                        auto flat = m_doc->flatten();
                        LayerTreeNode* walk = hitNode;
                        while (walk && walk->parent
                               && walk->parent->type == LayerTreeNode::Type::Group) {
                            walk = walk->parent;
                        }
                        if (walk && walk != hitNode && walk->type == LayerTreeNode::Type::Group) {
                            for (int gi = 0; gi < static_cast<int>(flat.size()); ++gi) {
                                if (flat[gi] == walk) {
                                    hitIndex = gi;
                                    break;
                                }
                            }
                        }
                    }

                    bool alreadySelected = m_doc->isSelected(hitIndex);
                    bool ctrlHeld = (e->modifiers() & Qt::ControlModifier) != 0;

                    if (alreadySelected && !ctrlHeld) {
                        // Click on already-selected layer — move all selected
                    } else if (ctrlHeld) {
                        m_controller->setMultiSelectNode(hitIndex, true);
                    } else {
                        m_controller->setMultiSelectNode(hitIndex, false);
                    }
                    activeNode = m_doc->activeNode();
                } else {
                    m_boxSelecting = true;
                    m_boxSelectStart = screenToCanvasNdc(e->position());
                    m_boxSelectCurrent = m_boxSelectStart;
                    update();
                    return;
                }
            }
        }

        if (insideBody && !onHandle && !onRotateZone && (e->modifiers() & Qt::ControlModifier)) {
            int hitIndex = pickLayerAtScreenPos(e->position(), true);
            if (hitIndex >= 0) {
                m_controller->setMultiSelectNode(hitIndex, true);
                activeNode = m_doc->activeNode();
            }
        }

        // Auto-select above may have changed the active node — refresh so the
        // move setup below targets the right node (group vs layer).
        activeNode = m_doc->activeNode();
        activeIsGroup = activeNode
            && activeNode->type == LayerTreeNode::Type::Group;

        // Gather all selected (non-locked) layers to move
        if (m_doc && !activeIsGroup) {
            m_multiMoveIndices.clear();
            m_multiMoveStartTransforms.clear();
            auto moveIndices = collectTransformableSelectedIndices(false);
            for (int idx : moveIndices) {
                auto* node = m_doc->nodeAt(idx);
                if (!node) continue;
                m_multiMoveIndices.push_back(idx);
                m_multiMoveStartTransforms.push_back(node->transform());
            }
        }

        if (m_doc->activeNode()
            && (m_doc->activeNode()->canTransform())
            && (activeIsGroup || !m_multiMoveIndices.empty())
            && (activeIsGroup || m_doc->activeNode()->layer)) {
            auto* active = m_doc->activeNode();
            m_transformState.mode = onRotateZone ? InteractionMode::Rotating
                                                 : InteractionMode::Moving;
            m_transformState.flatIndex = m_doc->activeFlatIndex;
            m_transformState.startMouseScreen = e->position();
            m_transformState.startTransform = active->transform();
            if (m_doc->selectedFlatIndices.size() > 1 && multiOutlineMatchesSelection()) {
                QPointF metric(std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                               std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
                m_multiResizeGroupStartCenterVis = QPointF(
                    m_multiResizeGroupBboxCenter.x() * metric.x(),
                    m_multiResizeGroupBboxCenter.y() * metric.y());
            }
            if (!m_freeTransformActive
                && active->layer && active->layer->textData) {
                m_textLayerIndex = m_doc->activeFlatIndex;
                m_textBeforeSnapshot = *active->layer->textData;
                m_textBeforeTransform = active->transform();
            }
            TransformController::decomposeVisual(active->transform(),
                m_canvasHalfExtents, size(),
                m_transformState.startHw, m_transformState.startHh,
                m_transformState.center, m_transformState.rotation);
            m_moving = true;
            if (onRotateZone)
                setCursor(Qt::CrossCursor);
            else {
                prepareSnapMoveBounds();
                setCursor(Qt::SizeAllCursor);
            }
        }
    }
    else if (tool == Tool::Brush || tool == Tool::Eraser
             || isCloneOrHealingTool()) {
        if (isCloneOrHealingTool()) {
            const QPointF docPos = screenToDocument(e->position());
            if (e->modifiers() & Qt::AltModifier) {
                setCloneSource(docPos);
                setCursor(Qt::CrossCursor);
                return;
            }
            if (!m_cloneSourceDefined) {
                m_cloneNeedsSourceFeedback = true;
                update();
                return;
            }
            if (!canPaintActiveRasterLayer())
                return;
            auto* cloneLayer = m_doc->activeLayer();
            if (!cloneLayer) { m_brushDrawing = false; return; }
            BrushInputState input;
            input.imagePos = screenToImage(e->position(), cloneLayer);
            beginCloneStampStroke(input, e->position(), docPos);
            return;
        }
        // Note: Alt over Brush/Eraser no longer samples a colour here. It arms the
        // Eyedropper override (see resolveTemporaryOverride()), so the press is
        // dispatched to the real Eyedropper branch below — which honours the
        // eyedropper sample-mode/size and Ctrl/right → background-colour rules.
        if (m_quickMaskMode) {
            // Paint on selection mask in Quick Mask mode
            QPointF docPos = screenToDocument(e->position());
            int cx = static_cast<int>(docPos.x());
            int cy = static_cast<int>(docPos.y());
            float radius = m_brushSettings.size * 0.5f;
            if (radius < 1.0f) radius = 1.0f;
            bool erasing = (tool == Tool::Eraser);

            int mw = m_doc->selection.width();
            int mh = m_doc->selection.height();
            int r = static_cast<int>(radius);

            for (int y = std::max(0, cy - r); y <= std::min(mh - 1, cy + r); ++y) {
                uchar* row = m_doc->selection.image().scanLine(y);
                for (int x = std::max(0, cx - r); x <= std::min(mw - 1, cx + r); ++x) {
                    float dist = std::sqrt(static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                    if (dist <= radius) {
                        row[x] = erasing ? 0 : 255;
                    }
                }
            }
            m_doc->selection.setActive(!m_doc->selection.isEmpty());
            markSelectMaskDirty();
            m_quickMaskDabbed = true;
            update();
        } else if (m_editingMask) {
            auto* layer = m_doc->activeLayer();
            if (!layer || layer->maskImage.isNull()) {
                m_brushDrawing = false;
                return;
            }
            BrushInputState input;
            input.imagePos = screenToImage(e->position(), layer);
            beginBrushStroke(input, e->position());
        } else {
            // Drop the synthetic QMouseEvent the OS pairs with a pen press so the
            // pen never starts a duplicate stroke. Real mouse presses (no recent
            // tablet activity) pass straight through.
            if (m_inputState.shouldIgnoreSyntheticMouse()) return;
        // Brush auto-creates a transparent pixel layer when none is selected, at
        // the start of the stroke, folded into the stroke's undo entry (closed in
        // endBrushStroke). A blocking mode / closed canvas aborts the stroke. The
        // Eraser is excluded: there is nothing to erase on a fresh empty layer.
        if (tool == Tool::Brush) {
            if (!ensurePaintTargetLayer(tr("Brush Stroke"))) { m_brushDrawing = false; return; }
        }
        auto* brushLayer = m_doc->activeLayer();
        if (!brushLayer) { m_brushDrawing = false; if (m_controller) m_controller->history().endMacro(); return; }
        BrushInputState input;
        input.imagePos = screenToImage(e->position(), brushLayer);
        beginBrushStroke(input, e->position());
        }
    }
    else if (tool == Tool::Gradient) {
        // Allow the drag to start even with no layer selected; the target pixel
        // layer is auto-created at apply time (endGradientDrag) so a bare click
        // that never drags leaves no empty layer behind.
        if (!m_doc || m_doc->size.isEmpty())
            return;
        beginGradientDrag(e->position());
    }
    else if (tool == Tool::FillBucket) {
        if (!m_doc || m_doc->size.isEmpty()) return;
        // Mask is the edit target: fill the mask (grayscale), not RGB pixels.
        // Requires an existing layer + mask; never auto-creates a pixel layer.
        // Bypasses checkDestructiveOp (a mask edit is not a pixel edit, and a
        // text/shape layer's mask is still fillable).
        {
            auto* node = m_doc->activeNode();
            auto* layer = m_doc->activeLayer();
            if (m_editingMask && node && layer
                && node->type == LayerTreeNode::Type::Layer && node->isVisible()
                && !layer->maskImage.isNull() && m_controller) {
                const QPointF imgPos = screenToImage(e->position(), layer);
                const QPoint layerPos(static_cast<int>(std::floor(imgPos.x())),
                                      static_cast<int>(std::floor(imgPos.y())));
                m_controller->applyFillBucketToMask(layerPos, m_fillBucketColor,
                                                    m_fillBucketTolerance / 255.0f);
                return;
            }
        }
        // No editable pixel layer selected → auto-create one for this click,
        // grouped with the fill as a single "Fill" undo entry.
        if (!ensurePaintTargetLayer(tr("Fill"))) return;
        auto* node = m_doc->activeNode();
        auto* layer = m_doc->activeLayer();
        // Central gate: blocks Text/Shape and pixel-locked (Lock Image / Lock All).
        if (node && layer && node->type == LayerTreeNode::Type::Layer && node->isVisible()
            && m_controller && m_controller->checkDestructiveOp(layer)) {
            fillBucket(e->position());
        }
        if (m_controller)
            m_controller->history().endMacro();   // no-op unless a layer was created
    }
    else if (tool == Tool::Eyedropper) {
        eyedropperSample(e->position(), e->button(), e->modifiers());
    }
    else if (tool == Tool::Select) {
        m_selectStart = screenToDocument(e->position());
        m_selectCurrent = m_selectStart;

        // While actively drawing a polygon, every left click is a vertex/close
        // action — never a rubber-band or move-selection gesture.
        if (m_selectType == SelectType::PolygonalLasso && m_polyLassoDrawing) {
            polyLassoMousePress(e);
            return;
        }

        if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::AltModifier))
            m_selectMode = SelectMode::Intersect;
        else if (e->modifiers() & Qt::ShiftModifier)
            m_selectMode = SelectMode::Add;
        else if (e->modifiers() & Qt::AltModifier)
            m_selectMode = SelectMode::Subtract;
        else
            m_selectMode = SelectMode::Replace;

        // Snapshot for the SelectionCommand pushed when this gesture commits
        // (move-selection and magic wand push their own commands).
        m_selectGestureBefore = m_doc->selection.image().copy();
        m_selectGestureBeforeActive = m_doc->selection.active();

        // Check if click is inside existing selection (move mode)
        bool insideSelection = false;
        if (m_selectMode == SelectMode::Replace && m_doc->selection.active() && !m_doc->selection.isEmpty()) {
            int sx = static_cast<int>(std::round(m_selectStart.x()));
            int sy = static_cast<int>(std::round(m_selectStart.y()));
            insideSelection = m_doc->selection.isSelected(sx, sy);
        }

        if (insideSelection) {
            m_movingSelection = true;
            m_selectMoveStart = m_selectStart;
            m_selectMoveBefore = m_doc->selection.image().copy();
            m_selectMoveCurrentDelta = QPoint(0, 0);
            m_selectMoveBoundsBefore = m_doc->selection.bounds();
            if (m_selectDragOverlay)
                m_selectDragOverlay->setScreenRect(documentRectToScreen(m_selectMoveBoundsBefore));
        } else if (m_selectType == SelectType::PolygonalLasso) {
            // Not inside an existing selection → begin a new polygon. The lasso
            // is click-driven and manages its own combine mode / overlay.
            polyLassoMousePress(e);
            return;
        } else if (m_selectType == SelectType::Lasso || m_selectType == SelectType::MagneticLasso) {
            m_lassoDrawing = true;
            m_lassoPoints.clear();
            QPointF startPoint = m_selectStart;
            if (m_selectType == SelectType::MagneticLasso) {
                updateMagneticEdgeMap();
                startPoint = snapToEdge(m_selectStart);
            }
            m_lassoPoints.push_back(startPoint);
            if (m_selectDragOverlay)
                m_selectDragOverlay->beginLasso(documentToScreen(startPoint));
        } else if (m_selectType == SelectType::QuickSelect) {
            m_quickSelecting = true;
            m_quickSelectSrcImage = quickToolSourceImage(m_doc->activeLayer());
            if (m_selectMode == SelectMode::Replace) {
                m_doc->selection.clear();
            } else if (m_selectMode == SelectMode::Intersect) {
                // Intersect: dabs reveal the original selection where the
                // color matches instead of adding — start from empty and copy
                // original values back in doQuickSelectDab.
                m_quickSelectOriginal = m_doc->selection.image().copy();
                m_doc->selection.image().fill(0);
            }
            QRect dab = doQuickSelectDab(m_selectStart);
            // Activate immediately so the marching-ants overlay (which is gated
            // on selection.active()) renders the evolving selection during the
            // drag instead of only after mouse release.
            m_doc->selection.setActive(true);
            markSelectMaskDirty(dab);
            update();

        } else if (m_selectType == SelectType::MagicWand) {
            if (m_controller) {
                // Tool params are document coords (same convention as
                // select_rect); the handler maps doc→layer itself for
                // transformed layers. lround (not truncation) so negative
                // coords don't shift by 1px.
                QPointF docPos = screenToDocument(e->position());
                m_controller->executeTool("select_magic_wand", {
                    {"x", static_cast<double>(std::lround(docPos.x()))},
                    {"y", static_cast<double>(std::lround(docPos.y()))},
                    {"tolerance", static_cast<double>(m_quickSelectTolerance)},
                    {"contiguous", 1.0},
                    {"mode", static_cast<double>(static_cast<int>(m_selectMode))}
                });
            }
        } else {
            m_selectDragging = true;
            if (m_selectDragOverlay) {
                bool ellipse = m_selectType == SelectType::Elliptical;
                m_selectDragOverlay->beginRect(e->position(), ellipse);
            }
        }

        // Don't clear for type-specific handlers that manage their own clear
        bool selfClearing = (m_selectType == SelectType::MagicWand
                          || m_selectType == SelectType::QuickSelect
                          || m_selectType == SelectType::Lasso
                          || m_selectType == SelectType::MagneticLasso);
        if (!insideSelection && m_selectMode == SelectMode::Replace && !selfClearing)
            m_doc->selection.clear();

        if (!m_selectDragging && !m_lassoDrawing)
            update();
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_curvesTargetDragging) {
        emit curvesTargetDragged(
            static_cast<int>(m_curvesTargetStartY - e->position().y()));
        e->accept();
        return;
    }
    if (m_hueSatDragging) {
        emit hueSatPickDragMoved(screenToDocument(e->position()));
        e->accept();
        return;
    }
    // Drop the OS's synthetic pen-mouse twin: tabletEvent already drove this input.
    if (!m_synthesizingMouseFromTablet && isPenSyntheticMouse(e)) {
        e->accept();
        return;
    }
    // Safety net for the contextual override: a modifier may have been pressed or
    // released while the canvas lacked key focus, so re-resolve it here. Cheap and
    // a no-op when the override is unchanged.
    refreshTemporaryOverride();
    if (!m_synthesizingMouseFromTablet) {
        // Record real mouse activity. Past the synthetic-mouse window this hands
        // the active device back to the mouse so painting resumes normally after
        // the pen is set down (acceptance criterion 14).
        PointerInputEvent me = CanvasInputMapper::fromMouseEvent(e, PointerPhase::Move);
        m_inputState.noteMouseEvent(me);
    }
    // Don't let a synthetic mouse-move (the OS twin of a pen move) drag the brush
    // cursor anchor away from where the pen put it.
    if (!m_inputState.shouldIgnoreSyntheticMouse())
        m_lastScreenPos = e->position();
    if (m_currentTool == Tool::AiSelect && m_doc) {
        aiSelectMouseMove(e);
        e->accept();
        return;
    }
    if (m_currentTool == Tool::AiRemove && m_doc) {
        aiRemoveMouseMove(e);
        e->accept();
        return;
    }
    if (m_rulerGuideOverlay && m_doc) {
        const QPointF overlayPos = canvasToRulerOverlayPoint(e->position());
        const QPointF docPos = m_rulerGuideOverlay->screenToDocument(overlayPos);
        const bool overCanvas = docPos.x() >= 0.0 && docPos.y() >= 0.0
            && docPos.x() <= m_doc->size.width()
            && docPos.y() <= m_doc->size.height();
        m_rulerGuideOverlay->setMouseDocumentPos(docPos, overCanvas);
    }

    if (m_brushAdjustDragging) {
        updateBrushAdjustDrag(e->globalPosition());
        e->accept();
        return;
    }

    if (m_guideDragging) {
        updateGuideInteraction(canvasToRulerOverlayPoint(e->position()));
        return;
    }

    if (m_panning) {
        QPointF delta = e->position() - m_lastMousePos;

        float ndcX = 2.0f * static_cast<float>(delta.x()) / width();
        float ndcY = -2.0f * static_cast<float>(delta.y()) / height();

        m_doc->panOffset.rx() += ndcX;
        m_doc->panOffset.ry() += ndcY;
        clampPanOffset();

        m_lastMousePos = e->position();
        update();
        return;
    }

    if (m_currentTool == Tool::Skew && m_distortActive) {
        distortMouseMove(e);
        return;
    }

    if (m_currentTool == Tool::Shape && m_shapeDragging) {
        QPointF ndcCurrent = screenToCanvasNdc(e->position());
        updateShapeDrag(ndcCurrent, e->modifiers());
        return;
    }

    if (m_currentTool == Tool::Text && m_textToolState == TextToolState::Creating) {
        m_textCreatingBox = true;
        if (m_selectDragOverlay)
            m_selectDragOverlay->updateRect(e->position());
        update();
        return;
    }

    if (m_currentTool == Tool::Text && m_textToolState == TextToolState::Editing) {
        if (e->buttons() & Qt::LeftButton) {
            auto* node = m_doc ? m_doc->nodeAt(m_textLayerIndex) : nullptr;
            if (node && node->layer && node->layer->textData) {
                int charIndex = textLayerCharAt(node, e->position());
                if (m_textDragStart >= 0)
                    m_textEditor.setSelection(m_textDragStart, charIndex);
                m_textEditor.setCursorPos(charIndex);
                rerenderTextLayer();
            }
        }
        return;
    }

    if (m_boxSelecting) {
        m_boxSelectCurrent = screenToCanvasNdc(e->position());
        update();
        return;
    }

    if (m_moving && m_transformState.mode != InteractionMode::Idle) {
        auto* movingNode = m_doc->activeNode();
        const bool movingGroup = movingNode
            && movingNode->type == LayerTreeNode::Type::Group;
        if (!movingNode || (!movingNode->layer && !movingGroup)) {
            m_transformState.mode = InteractionMode::Idle;
            m_moving = false;
            return;
        }

        if (m_transformState.mode == InteractionMode::Moving) {
            applySnapMoveFromStart(e->position(), e->modifiers());
            emit activeTransformChanged();
            if (m_transformOverlay)
                m_transformOverlay->setVisible(false);
            update();
            return;
        }

        QTransform newTransform;
        switch (m_transformState.mode) {
        case InteractionMode::Resizing: {
            auto modifiers = e->modifiers();
            auto* resizeNode = m_doc ? m_doc->activeNode() : nullptr;
            if (resizeNode && resizeNode->layer && resizeNode->layer->textData
                && resizeNode->layer->textData->flowMode == TextFlowMode::Point)
                modifiers &= ~Qt::ShiftModifier; // Force proportional for Point text
            newTransform = TransformController::updateResize(
                m_transformState, e->position(), m_doc->zoom,
                m_doc->panOffset, m_canvasHalfExtents, size(),
                modifiers);
            break;
        }
        case InteractionMode::Rotating:
            newTransform = TransformController::updateRotate(
                m_transformState, e->position(), m_doc->zoom,
                m_doc->panOffset, m_canvasHalfExtents, size(),
                e->modifiers());
            break;
        default:
            break;
        }

        // Apply the new frame to the gesture's nodes as a WORLD-SPACE DELTA:
        //   M = startFrame⁻¹ · newFrame    (canvas-NDC map: anchor-fixed scale,
        //                                   or rotation about the frame center)
        //   local' = startLocal · P · M · P⁻¹      (P = parent chain)
        // One model for every case — single layer (root or nested), single
        // group, and multi-selection (layers and groups alike). Applying the
        // delta on top of the gesture-start transform — instead of replacing
        // the node's world matrix with a recomposed frame — preserves shear
        // and flips already present in the transform (e.g. children of
        // rotated, non-uniformly scaled groups), so grabbing a handle never
        // snaps the layer to a reconstructed unsheared frame.
        const QTransform startFrame = TransformController::composeVisual(
            m_transformState.startHw, m_transformState.startHh,
            m_transformState.center, m_transformState.rotation,
            m_canvasHalfExtents, size());
        bool frameInvertible = false;
        const QTransform deltaM =
            startFrame.inverted(&frameInvertible) * newTransform;
        if (!frameInvertible) {
            update();
            return;
        }

        if (!m_multiResizeIndices.empty()) {
            // Multi-selection resize/rotate: every transformable participant
            // (layers and groups) takes the same world delta around the shared
            // frame; children of selected groups follow via accumulation.
            for (size_t i = 0; i < m_multiResizeIndices.size(); ++i) {
                auto* node = m_doc->nodeAt(m_multiResizeIndices[i]);
                if (!node || node->isPositionLocked()) continue;
                const QTransform& parentAccum = m_multiResizeStartParentAccums[i];
                m_controller->previewNodeTransform(node,
                    m_multiResizeStartTransforms[i] * parentAccum
                    * deltaM * parentAccum.inverted());
            }

            // Keep the displayed multi-bbox in sync with the dragged frame.
            float nHw, nHh, nRot;
            QPointF nC;
            TransformController::decomposeVisual(newTransform, m_canvasHalfExtents,
                                                 size(), nHw, nHh, nC, nRot);
            QPointF metric(std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                           std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
            m_multiResizeGroupBboxCenter = QPointF(nC.x() / metric.x(),
                                                   nC.y() / metric.y());
            m_multiResizeGroupBboxHw = std::abs(nHw) / metric.x();
            m_multiResizeGroupBboxHh = std::abs(nHh) / metric.y();
            m_multiResizeGroupBboxRotation = nRot;
            m_multiResizeGroupBboxValid = true;
        } else if (movingGroup) {
            // Single group: children follow via accumulatedTransform(); the
            // display bbox is recomputed fresh per frame in paintGL.
            m_controller->previewNodeTransform(movingNode,
                m_groupTransformStart * m_groupParentAccum
                * deltaM * m_groupParentAccum.inverted());
        } else {
            QTransform parentAccum;
            for (auto* p = movingNode->parent; p; p = p->parent)
                parentAccum = parentAccum * p->transform();
            m_controller->previewNodeTransform(movingNode,
                m_transformState.startTransform * parentAccum
                * deltaM * parentAccum.inverted());
        }
        if (m_freeTransformActive)
            m_freeTransformDirty = true;

        applySnapForCurrentTransform(e->modifiers());
        emit activeTransformChanged();

        if (m_transformState.mode == InteractionMode::Resizing
            && movingNode->layer && movingNode->layer->textData
            && movingNode->layer->textData->flowMode == TextFlowMode::Paragraph) {
            resizeParagraphTextBoxToTransform(movingNode, m_textToolState == TextToolState::Editing);
        }

        if (m_freeTransformActive && m_transformState.mode == InteractionMode::Rotating && m_transformOverlay) {
            float hw = 1.0f, hh = 1.0f, rot = 0.0f;
            QPointF center;
            TransformController::decomposeVisual(movingNode->transform(),
                m_canvasHalfExtents, size(), hw, hh, center, rot);
            float deg = static_cast<float>(rot * 180.0 / M_PI);
            while (deg > 180.0f) deg -= 360.0f;
            while (deg < -180.0f) deg += 360.0f;
            m_transformOverlay->setText(QStringLiteral("Angle: %1°").arg(deg, 0, 'f', 1));
            m_transformOverlay->adjustSize();
            QPoint pos = e->position().toPoint() + QPoint(14, 14);
            if (pos.x() + m_transformOverlay->width() > width())
                pos.setX(e->position().toPoint().x() - m_transformOverlay->width() - 12);
            if (pos.y() + m_transformOverlay->height() > height())
                pos.setY(e->position().toPoint().y() - m_transformOverlay->height() - 12);
            m_transformOverlay->move(pos);
            m_transformOverlay->setVisible(true);
        } else if (m_transformOverlay) {
            m_transformOverlay->setVisible(false);
        }
        update();
        return;
    }

    if (m_currentTool == Tool::Gradient && m_gradientDragging) {
        updateGradientDrag(e->position(), e->modifiers());
        return;
    }

    if (m_transformOverlay && m_transformOverlay->isVisible())
        m_transformOverlay->setVisible(false);

    if (updateGuideHover(canvasToRulerOverlayPoint(e->position())))
        return;

    // Hover feedback (cursor + previews) follows the effective tool so an armed
    // contextual override paints the override's cursor/overlay, not the selected
    // tool's.
    const Tool tool = effectiveTool();

    const bool quickSelectActive = tool == Tool::Select
                                && m_selectType == SelectType::QuickSelect;
    if ((tool == Tool::Brush
         || tool == Tool::Eraser
         || isCloneOrHealingTool()
         || quickSelectActive) && !m_brushDrawing) {
        if (isCloneOrHealingTool()) {
            // Alt picks the clone source: show a precise crosshair for that. Caps
            // Lock also forces the crosshair. In normal mode the preview overlay is
            // the cursor, so keep the OS cursor hidden (no icon overlapping it).
            setCursor(((e->modifiers() & Qt::AltModifier) || m_capsLockActive)
                ? Qt::CrossCursor
                : Qt::BlankCursor);
        }
        // Hover only moves the cursor preview, which lives in the
        // BrushPreviewOverlay QWidget on top of the GL canvas — the GL frame
        // itself is unchanged (renderQuickSelectCircle is no longer wired in),
        // so a full canvas repaint per mouse move here only burned a complete
        // render pass and made the cursor lag on dab-heavy documents.
        updateBrushPreviewOverlay();
    }

    if (tool == Tool::Move && m_showTransformControls && !m_moving) {
        auto* node = freeTransformNode();
        if (!node)
            node = m_doc ? m_doc->activeNode() : nullptr;
        const bool nodeIsGroup = node && node->type == LayerTreeNode::Type::Group;
        QPolygonF corners;
        if (node && node->layer)
            corners = TransformController::cornersFromNode(node);
        else if (nodeIsGroup)
            corners = groupUnionCorners(node);
        if (corners.size() == 4) {
            QMatrix4x4 mvp;
            mvp.translate(m_doc->panOffset.x(), m_doc->panOffset.y());
            mvp.scale(m_doc->zoom);
            mvp.scale(m_canvasHalfExtents.x(), m_canvasHalfExtents.y());
            QPolygonF screenCorners;
            for (auto& p : corners) {
                QVector4D v = mvp * QVector4D(static_cast<float>(p.x()),
                                              static_cast<float>(p.y()), 0, 1);
                screenCorners << QPointF(v.x(), v.y());
            }

            HandlePosition handle = TransformController::handleAt(e->position(), screenCorners, size());
            if (handle != HandlePosition::None && handle != HandlePosition::Center) {
                setCursor(TransformController::cursorForHandle(handle));
            } else if (isInRotateZone(screenCorners, e->position())) {
                setCursor(Qt::CrossCursor);
            } else if (handle == HandlePosition::Center) {
                setCursor(Qt::SizeAllCursor);
            } else {
                updateToolCursor();
            }
        }
    }

    if (tool == Tool::Crop && !m_cropDragging) {
        CropHandleId hoverHandle = cropHandleAtScreen(e->position());
        if (hoverHandle == CropHandleId::None)
            setCursor(m_cursors[static_cast<int>(Tool::Crop)]);
        else
            setCursor(cropCursorForHandle(hoverHandle));
    }

    if (tool == Tool::Eyedropper) {
        m_eyedropperHovering = true;
        m_eyedropperScreenPos = e->position();
        m_eyedropperHoverColor = eyedropperSampleColor(e->position());
        setCursor(m_cursors[static_cast<int>(Tool::Eyedropper)]);
        if (m_eyedropperHoverColor.isValid()) {
            QColor c = m_eyedropperHoverColor;
            QString hex = c.name(QColor::HexRgb).toUpper();
            if (c.alpha() < 255)
                hex = c.name(QColor::HexArgb).toUpper();
            QString text = QStringLiteral("%1\nRGB(%2, %3, %4)\nA: %5")
                .arg(hex)
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue())
                .arg(c.alpha());
            m_eyedropperOverlay->setText(text);
            m_eyedropperOverlay->adjustSize();
            QPoint pos = e->position().toPoint() + QPoint(16, 16);
            if (pos.x() + m_eyedropperOverlay->width() > width())
                pos.setX(e->position().toPoint().x() - m_eyedropperOverlay->width() - 8);
            if (pos.y() + m_eyedropperOverlay->height() > height())
                pos.setY(e->position().toPoint().y() - m_eyedropperOverlay->height() - 8);
            m_eyedropperOverlay->move(pos);
            m_eyedropperOverlay->setVisible(true);
        } else {
            m_eyedropperOverlay->setVisible(false);
        }
        update();
    }

    if (m_quickMaskMode && (e->buttons() & Qt::LeftButton)
        && (tool == Tool::Brush || tool == Tool::Eraser)) {
        bool erasing = (tool == Tool::Eraser);
        QPointF docPos = screenToDocument(e->position());
        int cx = static_cast<int>(docPos.x());
        int cy = static_cast<int>(docPos.y());
        float radius = m_brushSettings.size * 0.5f;
        if (radius < 1.0f) radius = 1.0f;
        int r = static_cast<int>(radius);
        int mw = m_doc->selection.width();
        int mh = m_doc->selection.height();
        for (int y = std::max(0, cy - r); y <= std::min(mh - 1, cy + r); ++y) {
            uchar* row = m_doc->selection.image().scanLine(y);
            for (int x = std::max(0, cx - r); x <= std::min(mw - 1, cx + r); ++x) {
                float dist = std::sqrt(static_cast<float>((x - cx)*(x - cx) + (y - cy)*(y - cy)));
                if (dist <= radius)
                    row[x] = erasing ? 0 : 255;
            }
        }
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        markSelectMaskDirty();
        m_quickMaskDabbed = true;
        update();
        return;
    }

    if (m_brushDrawing) {
        // Ignore the synthetic mouse-move twin of a pen move (the pen already
        // advanced the stroke via tabletEvent); otherwise the dab would be applied
        // twice and the cursor would lag a frame.
        if (m_inputState.shouldIgnoreSyntheticMouse()) return;

        QPointF screenPos = e->position();
        float dt = m_brushStrokeTimer.nsecsElapsed() / 1e9f;
        m_brushStrokeTimer.start();

        auto* brushLayer = m_doc->activeLayer();
        if (!brushLayer) { m_brushDrawing = false; return; }
        BrushInputState input;
        input.imagePos = screenToImage(screenPos, brushLayer);

        if (dt > 0.0f) {
            QPointF delta = screenPos - m_brushScreenLastPos;
            float dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
            input.velocity = dist / dt;
            input.direction = std::atan2(delta.y(), delta.x());
        }

        m_brushScreenLastPos = screenPos;
        if (isCloneOrHealingTool())
            continueCloneStampStroke(input, screenPos, screenToDocument(screenPos));
        else
            continueBrushStroke(input, screenPos);
        updateBrushPreviewOverlay();
    }

    if (m_currentTool == Tool::Select && m_selectDragging) {
        m_selectCurrent = screenToDocument(e->position());
        if (m_selectDragOverlay)
            m_selectDragOverlay->updateRect(e->position());
        return;
    }

    if (m_currentTool == Tool::Select && m_lassoDrawing) {
        m_selectCurrent = screenToDocument(e->position());
        QPointF nextPoint = m_selectCurrent;
        if (m_selectType == SelectType::MagneticLasso && m_magneticEdgeReady)
            nextPoint = snapToEdge(m_selectCurrent);

        // Auto-close affordance in screen pixels so it is zoom-independent,
        // matching the polygonal lasso's 8px rule (a doc-px threshold becomes
        // an 80px dead zone at 8× zoom and unreachable at 0.1×).
        m_lassoCanClose = false;
        if (m_lassoPoints.size() >= 3) {
            QPointF d = documentToScreen(m_lassoPoints[0]) - e->position();
            m_lassoCanClose = std::sqrt(d.x() * d.x() + d.y() * d.y()) <= 8.0;
        }

        if (!m_lassoCanClose)
            m_lassoPoints.push_back(nextPoint);
        if (m_selectDragOverlay && !m_lassoCanClose)
            m_selectDragOverlay->addLassoPoint(documentToScreen(nextPoint));
        // If can close, don't add more points (release will close)

        if (!m_selectDragOverlay)
            update();
        return;
    }

    if (m_currentTool == Tool::Select && m_polyLassoDrawing) {
        QPointF docPos = screenToDocument(e->position());
        if ((e->modifiers() & Qt::ShiftModifier) && !m_polyLassoPoints.empty())
            docPos = constrainAngle45(m_polyLassoPoints.back(), docPos);
        m_polyLassoPreviewDoc = docPos;

        // Close-affordance uses screen distance so it is zoom-independent.
        m_polyLassoCanClose = false;
        if (m_polyLassoPoints.size() >= 3) {
            QPointF firstScreen = documentToScreen(m_polyLassoPoints.front());
            QPointF d = firstScreen - e->position();
            m_polyLassoCanClose =
                std::sqrt(d.x() * d.x() + d.y() * d.y()) <= 8.0;
        }

        updatePolyLassoOverlay();
        return;
    }

    if (m_currentTool == Tool::Select && m_quickSelecting) {
        QPointF current = screenToDocument(e->position());
        QRect dab = doQuickSelectDab(current);
        // Keep the selection active throughout the drag so the overlay and
        // marching ants track every dab in real time (Add/Subtract included).
        m_doc->selection.setActive(true);
        markSelectMaskDirty(dab);
        update();
    }

    if (m_currentTool == Tool::Select && m_movingSelection) {
        QPointF current = screenToDocument(e->position());
        int dx = static_cast<int>(std::round(current.x() - m_selectMoveStart.x()));
        int dy = static_cast<int>(std::round(current.y() - m_selectMoveStart.y()));
        m_selectMoveCurrentDelta = QPoint(dx, dy);
        if (m_selectDragOverlay) {
            QRectF movedBounds = m_selectMoveBoundsBefore.translated(dx, dy);
            m_selectDragOverlay->setScreenRect(documentRectToScreen(movedBounds));
        }
        return;
    }

    if (m_currentTool == Tool::Crop && m_cropDragging) {
        QPointF ndcNow = screenToCanvasNdc(e->position());
        ndcNow.setX(std::clamp(static_cast<float>(ndcNow.x()), -1.0f, 1.0f));
        ndcNow.setY(std::clamp(static_cast<float>(ndcNow.y()), -1.0f, 1.0f));

        QPointF ndcStart = screenToCanvasNdc(m_cropDragStart);
        ndcStart.setX(std::clamp(static_cast<float>(ndcStart.x()), -1.0f, 1.0f));
        ndcStart.setY(std::clamp(static_cast<float>(ndcStart.y()), -1.0f, 1.0f));
        QPointF delta = ndcNow - ndcStart;

        auto boundsOf = [](const QRectF& rect, float& l, float& r, float& t, float& b) {
            l = std::min(static_cast<float>(rect.left()), static_cast<float>(rect.right()));
            r = std::max(static_cast<float>(rect.left()), static_cast<float>(rect.right()));
            t = std::max(static_cast<float>(rect.top()), static_cast<float>(rect.bottom()));
            b = std::min(static_cast<float>(rect.top()), static_cast<float>(rect.bottom()));
        };
        auto makeCropRect = [](float l, float r, float t, float b) {
            float left = std::clamp(std::min(l, r), -1.0f, 1.0f);
            float right = std::clamp(std::max(l, r), -1.0f, 1.0f);
            float top = std::clamp(std::max(t, b), -1.0f, 1.0f);
            float bottom = std::clamp(std::min(t, b), -1.0f, 1.0f);
            return QRectF(QPointF(left, bottom), QSizeF(right - left, top - bottom));
        };

        float ol, or_, ot, ob;
        boundsOf(m_cropDragOrigRect, ol, or_, ot, ob);
        float l = ol, r = or_, t = ot, b = ob;

        switch (m_cropHandle) {
        case CropHandleId::None: {
            if ((e->position() - m_cropDragStart).manhattanLength() < 4.0)
                return;
            l = static_cast<float>(ndcStart.x());
            r = static_cast<float>(ndcNow.x());
            t = static_cast<float>(ndcStart.y());
            b = static_cast<float>(ndcNow.y());
            break;
        }
        case CropHandleId::Inside: {
            // Moving keeps the box size locked. Translate by the raw delta, then
            // clamp the *position* (not the size) so the box stays fully inside
            // the canvas: when it reaches a border it stops there at full size
            // instead of shrinking.
            QPointF rawDelta = screenToCanvasNdc(e->position())
                             - screenToCanvasNdc(m_cropDragStart);
            float bl = static_cast<float>(std::min(m_cropDragOrigRect.left(), m_cropDragOrigRect.right()));
            float br = static_cast<float>(std::max(m_cropDragOrigRect.left(), m_cropDragOrigRect.right()));
            float bb = static_cast<float>(std::min(m_cropDragOrigRect.top(), m_cropDragOrigRect.bottom()));
            float bt = static_cast<float>(std::max(m_cropDragOrigRect.top(), m_cropDragOrigRect.bottom()));
            float dx = std::clamp(static_cast<float>(rawDelta.x()), -1.0f - bl, 1.0f - br);
            float dy = std::clamp(static_cast<float>(rawDelta.y()), -1.0f - bb, 1.0f - bt);
            m_cropRect = m_cropDragOrigRect.translated(QPointF(dx, dy));
            update();
            return;
        }
        case CropHandleId::TopLeft:
            l = static_cast<float>(ndcNow.x());
            t = static_cast<float>(ndcNow.y());
            break;
        case CropHandleId::Top:
            t = static_cast<float>(ndcNow.y());
            break;
        case CropHandleId::TopRight:
            r = static_cast<float>(ndcNow.x());
            t = static_cast<float>(ndcNow.y());
            break;
        case CropHandleId::Right:
            r = static_cast<float>(ndcNow.x());
            break;
        case CropHandleId::BottomRight:
            r = static_cast<float>(ndcNow.x());
            b = static_cast<float>(ndcNow.y());
            break;
        case CropHandleId::Bottom:
            b = static_cast<float>(ndcNow.y());
            break;
        case CropHandleId::BottomLeft:
            l = static_cast<float>(ndcNow.x());
            b = static_cast<float>(ndcNow.y());
            break;
        case CropHandleId::Left:
            l = static_cast<float>(ndcNow.x());
            break;
        }

        bool hasRatio = (m_cropLockedRatio.width() > 0 && m_cropLockedRatio.height() > 0)
                        || m_cropLockedRatio.width() == -1;
        if (hasRatio) {
            double docW = m_doc ? m_doc->size.width() : 1.0;
            double docH = m_doc ? m_doc->size.height() : 1.0;
            float ndcTargetRatio;
            if (m_cropLockedRatio.width() == -1) {
                ndcTargetRatio = 1.0f;
            } else {
                float pixelRatio = m_cropLockedRatio.width() / m_cropLockedRatio.height();
                ndcTargetRatio = pixelRatio * static_cast<float>(docH)
                                 / static_cast<float>(docW);
            }

            auto applyCornerRatio = [&](float ax, float ay) {
                float dx = static_cast<float>(ndcNow.x()) - ax;
                float dy = static_cast<float>(ndcNow.y()) - ay;
                float w = std::abs(dx);
                float h = std::abs(dy);
                if (w / std::max(h, 0.001f) > ndcTargetRatio)
                    h = w / ndcTargetRatio;
                else
                    w = h * ndcTargetRatio;
                float mx = ax + (dx < 0.0f ? -w : w);
                float my = ay + (dy < 0.0f ? -h : h);
                l = ax;
                r = mx;
                t = ay;
                b = my;
            };

            switch (m_cropHandle) {
            case CropHandleId::None: {
                float ax = static_cast<float>(ndcStart.x());
                float ay = static_cast<float>(ndcStart.y());
                applyCornerRatio(ax, ay);
                break;
            }
            case CropHandleId::Top: {
                float h = std::abs(static_cast<float>(ndcNow.y()) - ob);
                float w = h * ndcTargetRatio;
                float cx = (ol + or_) * 0.5f;
                l = cx - w * 0.5f;
                r = cx + w * 0.5f;
                t = static_cast<float>(ndcNow.y());
                b = ob;
                break;
            }
            case CropHandleId::Bottom: {
                float h = std::abs(ot - static_cast<float>(ndcNow.y()));
                float w = h * ndcTargetRatio;
                float cx = (ol + or_) * 0.5f;
                l = cx - w * 0.5f;
                r = cx + w * 0.5f;
                t = ot;
                b = static_cast<float>(ndcNow.y());
                break;
            }
            case CropHandleId::Left: {
                float w = std::abs(or_ - static_cast<float>(ndcNow.x()));
                float h = w / ndcTargetRatio;
                float cy = (ot + ob) * 0.5f;
                l = static_cast<float>(ndcNow.x());
                r = or_;
                t = cy + h * 0.5f;
                b = cy - h * 0.5f;
                break;
            }
            case CropHandleId::Right: {
                float w = std::abs(static_cast<float>(ndcNow.x()) - ol);
                float h = w / ndcTargetRatio;
                float cy = (ot + ob) * 0.5f;
                l = ol;
                r = static_cast<float>(ndcNow.x());
                t = cy + h * 0.5f;
                b = cy - h * 0.5f;
                break;
            }
            case CropHandleId::TopLeft:
                applyCornerRatio(or_, ob);
                break;
            case CropHandleId::TopRight:
                applyCornerRatio(ol, ob);
                break;
            case CropHandleId::BottomRight:
                applyCornerRatio(ol, ot);
                break;
            case CropHandleId::BottomLeft:
                applyCornerRatio(or_, ot);
                break;
            default:
                break;
            }
        }

        m_cropRect = makeCropRect(l, r, t, b);
        if (m_cropRect.width() < 0.01f || m_cropRect.height() < 0.01f)
            return;

        update();
    }

    if (m_currentTool == Tool::Move && !m_moving) {
        auto* node = m_doc->activeNode();
        if (node && node->layer) {
            QPolygonF corners = TransformController::cornersFromNode(node);
            QMatrix4x4 mvp;
            mvp.translate(m_doc->panOffset.x(), m_doc->panOffset.y());
            mvp.scale(m_doc->zoom);
            mvp.scale(m_canvasHalfExtents.x(), m_canvasHalfExtents.y());

            QPolygonF screenCorners;
            for (auto& p : corners) {
                QVector4D v = mvp * QVector4D(static_cast<float>(p.x()),
                                               static_cast<float>(p.y()), 0, 1);
                screenCorners << QPointF(v.x(), v.y());
            }

            HandlePosition handle = TransformController::handleAt(
                e->position(), screenCorners, size());
            setCursor(TransformController::cursorForHandle(handle));
        } else {
            updateToolCursor();
        }
    }

    auto* layer = m_doc->activeLayer();
    if (layer) {
        QPointF imgPos = screenToImage(e->position(), layer);
        emit mouseImageCoordChanged(imgPos);
    }
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e)
{
    // Drop the OS's synthetic pen-mouse twin: tabletEvent already drove this input.
    if (!m_synthesizingMouseFromTablet && isPenSyntheticMouse(e)) {
        e->accept();
        return;
    }
    if (!m_synthesizingMouseFromTablet) {
        PointerInputEvent me = CanvasInputMapper::fromMouseEvent(e, PointerPhase::Release);
        m_inputState.noteMouseEvent(me);
    }
    if (m_curvesTargetDragging) {
        m_curvesTargetDragging = false;
        emit curvesTargetEnded();
        e->accept();
        return;
    }

    if (m_hueSatDragging) {
        m_hueSatDragging = false;
        emit hueSatPickDragEnded();
        e->accept();
        return;
    }

    if (m_brushAdjustDragging) {
        finishBrushAdjustDrag(false);
        e->accept();
        return;
    }

    if (m_currentTool == Tool::AiSelect && e->button() == Qt::LeftButton && m_doc) {
        aiSelectMouseRelease(e);
        e->accept();
        return;
    }

    if (m_currentTool == Tool::AiRemove && e->button() == Qt::LeftButton && m_doc) {
        aiRemoveMouseRelease(e);
        e->accept();
        return;
    }

    if (m_panning) {
        m_panning = false;
        updateToolCursor();
        return;
    }

    if (m_rightButtonDown && e->button() == Qt::RightButton) {
        m_rightButtonDown = false;
        if (!m_brushDrawing && !m_freeTransformActive && !m_distortActive
            && !m_shapeDragging && !m_gradientDragging
            && (e->position() - m_rightPressPos).manhattanLength() < 4.0) {
            emit contextMenuRequested(e->globalPosition().toPoint());
        }
        return;
    }

    if (e->button() != Qt::LeftButton) return;

    if (m_guideDragging) {
        finishGuideInteraction(canvasToRulerOverlayPoint(e->position()));
        return;
    }

    if (m_currentTool == Tool::Skew && m_distortActive) {
        distortMouseRelease(e);
        return;
    }

    if (m_currentTool == Tool::Shape && m_shapeDragging) {
        endShapeDrag();
        return;
    }

    if (m_currentTool == Tool::Text && m_textToolState == TextToolState::Creating) {
        m_textToolState = TextToolState::Idle;
        QPointF delta = e->position() - m_textCreateStart;
        bool isDrag = delta.manhattanLength() > 8.0f;
        if (m_selectDragOverlay)
            m_selectDragOverlay->finish();

        if (m_controller && m_doc) {
            QPointF canvasPos = screenToCanvasNdc(m_textCreateStart);
            float boxWidth = 0.0f;
            float boxHeight = 0.0f;

            if (isDrag && m_textCreatingBox) {
                QPointF endCanvas = screenToCanvasNdc(e->position());
                float sx = (canvasPos.x() + 1.0f) * 0.5f * m_doc->size.width();
                float sy = (1.0f - canvasPos.y()) * 0.5f * m_doc->size.height();
                float ex = (endCanvas.x() + 1.0f) * 0.5f * m_doc->size.width();
                float ey = (1.0f - endCanvas.y()) * 0.5f * m_doc->size.height();
                boxWidth = std::abs(ex - sx);
                boxHeight = std::abs(ey - sy);
                canvasPos = QPointF(
                    std::min(canvasPos.x(), endCanvas.x()),
                    std::max(canvasPos.y(), endCanvas.y()));
            }

            TextBox textBox;
            textBox.width = boxWidth;
            textBox.height = boxHeight;

            m_controller->createTextLayer(QString(), textBox, canvasPos,
                                          32.0f, m_brushSettings.color);

            int newIdx = m_doc->flatCount() > 0 ? 0 : -1;
            if (newIdx >= 0) {
                auto* newNode = m_doc->nodeAt(newIdx);
                if (newNode && newNode->layer && newNode->layer->textData) {
                    m_textLayerIndex = newIdx;
                    m_textBeforeSnapshot = *newNode->layer->textData;
                    m_textBeforeTransform = newNode->transform();
                    m_textToolState = TextToolState::Editing;
                    m_textEditor.beginEdit(newNode->layer->textData.get());
                    m_textEditor.setCursorPos(0);
                    rerenderTextLayer();
                }
            }
        }
        m_textCreatingBox = false;
        updateToolCursor();
        update();
        return;
    }

    if (m_textToolState == TextToolState::Editing)
        m_textDragStart = -1;

    if (m_currentTool == Tool::Gradient && m_gradientDragging) {
        endGradientDrag(e->position(), e->modifiers());
        return;
    }

    if (m_boxSelecting) {
        float x1 = std::min(m_boxSelectStart.x(), m_boxSelectCurrent.x());
        float x2 = std::max(m_boxSelectStart.x(), m_boxSelectCurrent.x());
        float y1 = std::min(m_boxSelectStart.y(), m_boxSelectCurrent.y());
        float y2 = std::max(m_boxSelectStart.y(), m_boxSelectCurrent.y());
        QRectF selectionRect(x1, y1, x2 - x1, y2 - y1);

        float minDim = 4.0f / std::max(1.0f, static_cast<float>(std::min(width(), height()))) * 2.0f;
        bool isTiny = (selectionRect.width() < minDim && selectionRect.height() < minDim);

        if (isTiny) {
            bool ctrlHeld = (e->modifiers() & Qt::ControlModifier) != 0;
            if (!ctrlHeld) {
                m_controller->setSelectedIndices({});
            }
        } else {
            bool ctrlHeld = (e->modifiers() & Qt::ControlModifier) != 0;
            std::set<int> candidates = BoxSelection::findLayersInRect(m_doc, selectionRect);

            if (ctrlHeld) {
                std::set<int> newSelection = m_doc->selectedFlatIndices;
                for (int idx : candidates) {
                    if (m_doc->isSelected(idx))
                        newSelection.erase(idx);
                    else
                        newSelection.insert(idx);
                }
                m_controller->setSelectedIndices(newSelection);
            } else {
                m_controller->setSelectedIndices(candidates);
            }
        }

        m_boxSelecting = false;
        update();
        return;
    }

    if (m_moving && m_transformState.mode != InteractionMode::Idle) {
        InteractionMode prevMode = m_transformState.mode;
        m_transformState.mode = InteractionMode::Idle;
        m_moving = false;

        if (m_freeTransformActive) {
            // Free-transform session: gestures accumulate without touching the
            // history — commitFreeTransform/cancelFreeTransform consolidate the
            // whole session into one entry (or none). Record each node's
            // FIRST-seen start transform so the session commit/cancel spans it.
            auto record = [this](int idx, const QTransform& before) {
                for (int existing : m_freeTransformSessionIndices)
                    if (existing == idx) return;
                m_freeTransformSessionIndices.push_back(idx);
                m_freeTransformSessionStartTransforms.push_back(before);
            };
            if (prevMode == InteractionMode::Moving
                && !m_multiMoveIndices.empty()) {
                for (size_t i = 0; i < m_multiMoveIndices.size(); ++i)
                    record(m_multiMoveIndices[i], m_multiMoveStartTransforms[i]);
            } else if ((prevMode == InteractionMode::Resizing
                        || prevMode == InteractionMode::Rotating)
                       && !m_multiResizeIndices.empty()) {
                for (size_t i = 0; i < m_multiResizeIndices.size(); ++i)
                    record(m_multiResizeIndices[i], m_multiResizeStartTransforms[i]);
            } else if (m_doc->activeFlatIndex >= 0) {
                record(m_doc->activeFlatIndex, m_transformState.startTransform);
            }
            emit activeTransformChanged();
        } else if (prevMode == InteractionMode::Moving
            && m_multiMoveIndices.size() > 1
            && m_controller) {
            // Multi-move: commit all selected layers via NodeTransformCommand
            std::vector<int> indices;
            std::vector<QTransform> beforeXfs, afterXfs;
            for (size_t i = 0; i < m_multiMoveIndices.size(); ++i) {
                int idx = m_multiMoveIndices[i];
                auto* mn = m_doc->nodeAt(idx);
                if (!mn) continue;
                indices.push_back(idx);
                beforeXfs.push_back(m_multiMoveStartTransforms[i]);
                afterXfs.push_back(mn->transform());
            }
            if (!indices.empty()) {
                m_controller->setNodeTransforms(indices, afterXfs, beforeXfs);
            }
        } else if ((prevMode == InteractionMode::Resizing
                    || prevMode == InteractionMode::Rotating)
                   && !m_multiResizeIndices.empty()
                   && m_controller) {
            // Multi-selection resize/rotate. The list may hold a single entry
            // (e.g. the other participants are locked) — it still must commit
            // through here: the gesture transformed the LISTED nodes, not
            // necessarily the active one.
            const bool isResizeOrRotate =
                (prevMode == InteractionMode::Resizing
                 || prevMode == InteractionMode::Rotating);
            auto composite = std::make_unique<CompositeCommand>(
                tr("Transform Multi-Selection"));
            bool anyBaked = false;
            // Indices baked through their own command (TextEditCommand /
            // ModifyShapeCommand). They must NOT also go into the shared
            // NodeTransformCommand: the bake already mutated node->transform() and
            // its dedicated command restores it, so duplicating it would
            // double-apply on undo/redo.
            std::vector<bool> bakedListed(m_multiResizeIndices.size(), false);
            if (isResizeOrRotate) {
                for (size_t i = 0; i < m_multiResizeIndices.size(); ++i) {
                    int idx = m_multiResizeIndices[i];
                    auto* mn = m_doc->nodeAt(idx);
                    if (!mn) continue;
                    if (mn->type == LayerTreeNode::Type::Group) {
                        // A group: its node transform stays in the shared command
                        // below; only its vector descendants need baking.
                        anyBaked |= bakeGroupVectorChildrenToComposite(
                            mn, tr("Transform Multi-Selection"), *composite);
                    } else if (bakeVectorNodeToComposite(
                                   mn, idx, m_multiResizeStartTransforms[i],
                                   tr("Transform Multi-Selection"), *composite)) {
                        // A directly-selected Point-text / Shape layer: re-baked
                        // (font/geometry recalculated) and committed via its own
                        // command, so exclude it from the shared transform batch.
                        bakedListed[i] = true;
                        anyBaked = true;
                    }
                }
            }
            {
                std::vector<int> ntfIndices;
                std::vector<QTransform> ntfBefore, ntfAfter;
                for (size_t i = 0; i < m_multiResizeIndices.size(); ++i) {
                    if (bakedListed[i]) continue;
                    int idx = m_multiResizeIndices[i];
                    auto* mn = m_doc->nodeAt(idx);
                    if (!mn) continue;
                    ntfIndices.push_back(idx);
                    ntfBefore.push_back(m_multiResizeStartTransforms[i]);
                    ntfAfter.push_back(mn->transform());
                }
                if (!ntfIndices.empty())
                    composite->add(std::make_unique<NodeTransformCommand>(
                        m_doc, ntfIndices, ntfBefore, ntfAfter,
                        tr("Transform Multi-Selection")));
            }
            if (anyBaked) {
                m_controller->pushCommand(std::move(composite));
            } else {
                std::vector<int> indices;
                std::vector<QTransform> beforeXfs, afterXfs;
                for (size_t i = 0; i < m_multiResizeIndices.size(); ++i) {
                    int idx = m_multiResizeIndices[i];
                    auto* mn = m_doc->nodeAt(idx);
                    if (!mn) continue;
                    indices.push_back(idx);
                    beforeXfs.push_back(m_multiResizeStartTransforms[i]);
                    afterXfs.push_back(mn->transform());
                }
                if (!indices.empty()) {
                    m_controller->setNodeTransforms(indices, afterXfs, beforeXfs);
                }
            }
        } else if (m_controller && m_doc->activeFlatIndex >= 0) {
            auto* node = m_doc->activeNode();
            if (node) {
                auto* textData = node->layer ? node->layer->textData.get() : nullptr;
                if (prevMode == InteractionMode::Resizing && textData
                    && textData->flowMode == TextFlowMode::Paragraph) {
                    resizeParagraphTextBoxToTransform(node, false);
                    m_controller->commitTextEdit(m_doc->activeFlatIndex,
                        m_textBeforeSnapshot, m_textBeforeTransform);
                } else if (node->layer && node->layer->shapeData) {
                    // Re-render (bake) the shape at its new world scale/rotation so it
                    // stays crisp — works whether the shape is top-level or a child of
                    // a (possibly rotated/scaled) group, since bakeShapeTransform now
                    // uses the accumulated WORLD transform. Pure moves keep the raster.
                    if (prevMode == InteractionMode::Resizing
                        || prevMode == InteractionMode::Rotating) {
                        m_controller->bakeShapeTransform(m_doc->activeFlatIndex,
                            m_transformState.startTransform);
                    } else {
                        m_controller->setLayerTransform(m_doc->activeFlatIndex,
                            node->transform(), &m_transformState.startTransform);
                    }
                } else if (node->type == LayerTreeNode::Type::Group) {
                    const QTransform groupBefore = m_transformState.startTransform;
                    const QTransform groupAfter = node->transform();

                    // TODO - review
                    auto composite = std::make_unique<CompositeCommand>(tr("Transform Group"));
                    composite->add(std::make_unique<NodeTransformCommand>(
                        m_doc, std::vector<int>{m_doc->activeFlatIndex},
                        std::vector<QTransform>{groupBefore},
                        std::vector<QTransform>{groupAfter}, tr("Transform Group")));
                    if (prevMode == InteractionMode::Resizing
                        || prevMode == InteractionMode::Rotating) {
                        if (bakeGroupVectorChildrenToComposite(
                                node, tr("Transform Group"), *composite)) {
                            m_controller->pushCommand(std::move(composite));
                        } else {
                            std::vector<int> indices{m_doc->activeFlatIndex};
                            std::vector<QTransform> afterXfs{groupAfter};
                            std::vector<QTransform> beforeXfs{groupBefore};
                            m_controller->setNodeTransforms(indices, afterXfs, beforeXfs);
                        }
                    } else {
                        // Pure move — no vector children need baking.
                        std::vector<int> indices{m_doc->activeFlatIndex};
                        std::vector<QTransform> afterXfs{groupAfter};
                        std::vector<QTransform> beforeXfs{groupBefore};
                        m_controller->setNodeTransforms(indices, afterXfs, beforeXfs);
                    }
                } else if (node->layer && node->layer->textData) {
                    // Point text: the bake re-renders at the new world scale,
                    // mutating fontSize + cpuImage + transform together — a
                    // TextEditCommand restores all three consistently. (The old
                    // FilterCommand path captured the post-bake image as its
                    // "before" and never stored textData, so undo left a baked
                    // font under the pre-resize transform.) When the bake is a
                    // no-op (pure move/rotate) commitTextEdit pushes nothing,
                    // so fall back to a transform-only commit.
                    bakeTextLayerResolution(node);
                    if (!m_controller->commitTextEdit(m_doc->activeFlatIndex,
                            m_textBeforeSnapshot, m_textBeforeTransform)) {
                        m_controller->setLayerTransform(m_doc->activeFlatIndex,
                            node->transform(), &m_transformState.startTransform);
                    }
                } else {
                    m_controller->setLayerTransform(m_doc->activeFlatIndex,
                        node->transform(), &m_transformState.startTransform);
                }
            }
        }

        m_multiMoveIndices.clear();
        m_multiMoveStartTransforms.clear();
        m_multiResizeIndices.clear();
        m_multiResizeStartTransforms.clear();
        m_multiResizeStartParentAccums.clear();
        m_snapMoveStartDocumentBoundsValid = false;
        m_snapMoveIndices.clear();
        m_snapMoveStartTransforms.clear();

        if (m_transformOverlay)
            m_transformOverlay->setVisible(false);
        clearSnapFeedback();
        clearSnapBoundsCache();

        updateToolCursor();
        update();
    }

    if (m_currentTool == Tool::Crop && m_cropDragging) {
        m_cropDragging = false;
        m_cropRotating = false;

        // If the rect is too small after drawing new, reset to full canvas
        if (m_cropRect.width() < 0.02f || m_cropRect.height() < 0.02f) {
            resetCrop();
        }
        m_cropActive = true;
        update();
    }

    if (m_brushDrawing) {
        // Synthetic-mouse-release twin of a pen release: the pen already ran
        // endBrushStroke() (which clears m_brushDrawing), so this only fires for a
        // real mouse release — but guard anyway to be safe.
        if (m_inputState.shouldIgnoreSyntheticMouse()) return;
        endBrushStroke();
    }

    if (m_currentTool == Tool::Select && m_quickSelecting) {
        m_quickSelecting = false;
        m_quickSelectSrcImage = QImage();
        m_quickSelectOriginal = QImage();
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        markSelectMaskDirty();
        pushSelectionGestureUndo("quick_select");
        update();
    }

    if (m_currentTool == Tool::Select && m_movingSelection) {
        m_movingSelection = false;
        m_doc->selection.image() = m_selectMoveBefore.copy();
        m_doc->selection.translate(m_selectMoveCurrentDelta.x(), m_selectMoveCurrentDelta.y());
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        markSelectMaskDirty();
        if (m_controller) {
            QImage after = m_doc->selection.image().copy();
            bool afterActive = m_doc->selection.active();
            m_controller->history().push(std::make_unique<SelectionCommand>(
                m_doc, m_selectMoveBefore, after,
                true, afterActive, "move_selection"));
            emit m_controller->selectionChanged();
        }
        update();
    }

    if (m_currentTool == Tool::Select) {
        // While drawing a polygon, a button release must not tear down the
        // preview overlay or commit anything (the lasso is click-driven). When
        // not drawing, fall through so a move-selection gesture finishes
        // normally (overlay cleanup + state reset).
        if (m_selectType == SelectType::PolygonalLasso && m_polyLassoDrawing)
            return;

        if (m_selectDragOverlay)
            m_selectDragOverlay->finish();
        m_selectDragging = false;

        if ((m_selectType == SelectType::Lasso || m_selectType == SelectType::MagneticLasso) && m_lassoDrawing) {
            m_lassoDrawing = false;
            m_lassoCanClose = false;
            if (m_lassoPoints.size() >= 3) {
                m_doc->selection.setPolygon(m_lassoPoints, m_selectMode, m_selectAntiAlias);
                m_doc->selection.setActive(true);
                markSelectMaskDirty();
            }
            m_lassoPoints.clear();
            pushSelectionGestureUndo(m_selectType == SelectType::MagneticLasso
                                         ? "magnetic_lasso" : "lasso_select");
        } else if (m_selectType == SelectType::Rectangular || m_selectType == SelectType::Elliptical) {
            QRectF rubber = QRectF(m_selectStart, m_selectCurrent).normalized();
            if (rubber.width() >= 1.0 && rubber.height() >= 1.0) {
                if (m_selectType == SelectType::Rectangular)
                    m_doc->selection.setRect(rubber, m_selectMode, m_selectAntiAlias);
                else
                    m_doc->selection.setEllipse(rubber, m_selectMode, m_selectAntiAlias);
                m_doc->selection.setActive(true);
                markSelectMaskDirty();
            }
            // Also covers the click-without-drag Replace case, which cleared
            // the selection at mouse press (helper no-ops if nothing changed).
            pushSelectionGestureUndo(m_selectType == SelectType::Rectangular
                                         ? "select_rect" : "select_ellipse");
        }

        update();
    }
}


void CanvasView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;

    if (m_currentTool == Tool::Select && m_selectType == SelectType::PolygonalLasso
        && m_polyLassoDrawing) {
        QPointF docPos = screenToDocument(e->position());
        if ((e->modifiers() & Qt::ShiftModifier) && !m_polyLassoPoints.empty())
            docPos = constrainAngle45(m_polyLassoPoints.back(), docPos);
        if (m_polyLassoPoints.empty() || docPos != m_polyLassoPoints.back())
            m_polyLassoPoints.push_back(docPos);
        if (m_polyLassoPoints.size() >= 3)
            finalizePolyLasso();
        return;
    }

    // Double-click commits an in-progress distort/perspective transform.
    if (m_currentTool == Tool::Skew && m_distortActive) {
        commitDistort();
        return;
    }

    // Double-click inside crop area commits it
    if (m_currentTool == Tool::Crop && m_cropActive) {
        commitCropAction();
        return;
    }

    if (m_currentTool == Tool::Text || m_currentTool == Tool::Move) {
        int hitIndex = pickLayerAtScreenPos(e->position());
        auto* node = hitIndex >= 0 ? m_doc->nodeAt(hitIndex) : nullptr;
        if (node && node->layer && node->layer->textData && !node->canEditContent()) {
            m_controller->setActiveNode(hitIndex);
            emit m_controller->operationBlocked(
                tr("The content of this text layer is locked."));
            update();
            return;
        }
        if (node && node->layer && node->layer->textData) {
            if (m_currentTool != Tool::Text)
                setTool(Tool::Text);
            m_textLayerIndex = hitIndex;
            m_textBeforeSnapshot = *node->layer->textData;
            m_textBeforeTransform = node->transform();
            m_controller->setActiveNode(hitIndex);
            m_textToolState = TextToolState::Editing;
            m_textEditor.beginEdit(node->layer->textData.get());
            int charIndex = textLayerCharAt(node, e->position());
            m_textEditor.setCursorPos(charIndex);

            TextLayoutEngine engine;
            engine.setData(node->layer->textData.get());
            int wordStart = engine.wordStart(charIndex);
            int wordEnd = engine.wordEnd(charIndex);
            m_textEditor.setSelection(wordStart, wordEnd);
            m_textEditor.setCursorPos(wordEnd);

            rerenderTextLayer();
            updateToolCursor();
            return;
        }
    }

    if (m_currentTool == Tool::Move && m_freeTransformActive) {
        auto* node = freeTransformNode();
        const bool ftGroup = node && node->type == LayerTreeNode::Type::Group;
        QPolygonF corners;
        if (node && node->layer)
            corners = TransformController::cornersFromNode(node);
        else if (ftGroup)
            corners = groupUnionCorners(node);
        if (corners.size() == 4) {
            QMatrix4x4 mvp;
            mvp.translate(m_doc->panOffset.x(), m_doc->panOffset.y());
            mvp.scale(m_doc->zoom);
            mvp.scale(m_canvasHalfExtents.x(), m_canvasHalfExtents.y());

            QPolygonF screenCorners;
            for (auto& p : corners) {
                QVector4D v = mvp * QVector4D(static_cast<float>(p.x()),
                                               static_cast<float>(p.y()), 0, 1);
                screenCorners << QPointF(v.x(), v.y());
            }

            QPointF mouseNdc(
                2.0f * static_cast<float>(e->position().x()) / width() - 1.0f,
                1.0f - 2.0f * static_cast<float>(e->position().y()) / height());
            if (screenCorners.containsPoint(mouseNdc, Qt::OddEvenFill)) {
                commitFreeTransform();
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Unified pointer pipeline
// ---------------------------------------------------------------------------

void CanvasView::fillPointerCanvasCoords(PointerInputEvent& ev, Layer* layer) const
{
    // Single coordinate-conversion point shared by mouse and tablet. Both go
    // through the same screenToDocument / screenToImage path so zoom, pan, HiDPI
    // and the active-layer transform are resolved identically — the cursor and the
    // dab read the exact same numbers.
    ev.canvasPos = const_cast<CanvasView*>(this)->screenToDocument(ev.widgetPos);
    if (layer)
        ev.imagePos = const_cast<CanvasView*>(this)->screenToImage(ev.widgetPos, layer);
}

void CanvasView::logPointerInput(const PointerInputEvent& ev) const
{
    if (!m_inputDebug)
        return;
    static const char* kSource[] = {"Mouse", "TabletPen", "TabletEraser", "Touch", "Unknown"};
    static const char* kPhase[] = {"Enter", "Leave", "Hover", "Press", "Move", "Release", "Cancel"};
}

void CanvasView::updateBrushCursorFromPointer(const PointerInputEvent& ev)
{
    // The brush preview overlay (and the in-canvas indicator) reads m_lastScreenPos.
    // Driving it from the normalised pointer — for BOTH mouse and pen, on hover and
    // while drawing — is what keeps the visible cursor glued to the stroke. This is
    // the fix for the pen-cursor divergence: previously only mouseMoveEvent updated
    // m_lastScreenPos, so the pen drew but the cursor stayed put.
    if (ev.phase == PointerPhase::Leave) {
        if (m_brushPreviewOverlay)
            m_brushPreviewOverlay->clear();
        update();
        return;
    }
    // The circular preview overlay *is* the cursor for brush-based tools, so keep
    // the OS cursor hidden. Enabling WA_TabletTracking makes Qt re-assert the
    // default window cursor on pen proximity, which would otherwise pop the arrow
    // back over the brush outline — re-assert BlankCursor here so the pen and the
    // synthetic mouse both stay hidden. Alt over a clone tool wants the crosshair.
    if (isBrushBasedTool()) {
        const bool cloneAlt = isCloneOrHealingTool()
            && (ev.modifiers & Qt::AltModifier);
        // Caps Lock forces the precise crosshair instead of the hidden cursor.
        const Qt::CursorShape want = (cloneAlt || m_capsLockActive)
            ? Qt::CrossCursor : Qt::BlankCursor;
        if (cursor().shape() != want)
            setCursor(want);
    }
    m_lastScreenPos = ev.widgetPos;
    updateBrushPreviewOverlay();
    update();
}

void CanvasView::dispatchBrushPointer(const PointerInputEvent& ev)
{
    // Single painting path for mouse + tablet brush/eraser/clone strokes. Cursor
    // and stroke are driven from the same PointerInputEvent so they can't diverge.
    logPointerInput(ev);

    auto* layer = m_doc ? m_doc->activeLayer() : nullptr;

    // A tablet eraser tip temporarily forces erase mode regardless of the active
    // tool, then restores it — without changing the user's selected tool. (Pen tip
    // keeps the tool's own mode.)
    const BrushMode toolMode = m_brushSettings.mode;
    const bool forceErase = ev.isEraser;
    if (forceErase)
        m_brushSettings.mode = BrushMode::Erase;
    struct ModeRestore {
        BrushSettings* s; BrushMode m; bool active;
        ~ModeRestore() { if (active) s->mode = m; }
    } restore{&m_brushSettings, toolMode, forceErase};

    BrushInputState input = ev.toBrushInput();

    switch (ev.phase) {
    case PointerPhase::Enter:
    case PointerPhase::Hover:
        // Pen hovering / mouse moving with no button: only the cursor moves.
        updateBrushCursorFromPointer(ev);
        break;
    case PointerPhase::Leave:
        updateBrushCursorFromPointer(ev);
        break;
    case PointerPhase::Press:
        m_brushScreenLastPos = ev.widgetPos;
        m_brushStrokeTimer.start();
        if (isCloneOrHealingTool()) {
            if (!m_cloneSourceDefined) {
                m_cloneNeedsSourceFeedback = true;
                update();
            } else if (canPaintActiveRasterLayer()) {
                beginCloneStampStroke(input, ev.widgetPos, ev.canvasPos);
            }
        } else {
            // Brush auto-creates a transparent pixel layer when none is selected
            // (grouped undo, closed in endBrushStroke). The Eraser — including a
            // pen's eraser tip — is excluded: there is nothing to erase on a fresh
            // empty layer, so it only strokes when a layer already exists.
            const bool isEraser = (effectiveTool() == Tool::Eraser) || forceErase;
            const bool ready = isEraser ? (m_doc->activeLayer() != nullptr)
                                        : ensurePaintTargetLayer(tr("Brush Stroke"));
            if (ready) {
                // Recompute the stroke's image-space start against whatever layer
                // is active now (the existing one, or one just auto-created).
                if (auto* target = m_doc->activeLayer()) {
                    input.imagePos = screenToImage(ev.widgetPos, target);
                    beginBrushStroke(input, ev.widgetPos);
                } else if (m_controller) {
                    m_controller->history().endMacro();
                }
            }
        }
        updateBrushCursorFromPointer(ev);
        break;
    case PointerPhase::Move: {
        if (m_brushDrawing) {
            const float dt = m_brushStrokeTimer.nsecsElapsed() / 1e9f;
            m_brushStrokeTimer.start();
            if (dt > 0.0f) {
                const QPointF delta = ev.widgetPos - m_brushScreenLastPos;
                const float dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
                input.velocity = dist / dt;
                input.direction = std::atan2(delta.y(), delta.x());
            }
            m_brushScreenLastPos = ev.widgetPos;
            if (isCloneOrHealingTool())
                continueCloneStampStroke(input, ev.widgetPos, ev.canvasPos);
            else
                continueBrushStroke(input, ev.widgetPos);
        }
        updateBrushCursorFromPointer(ev);
        break;
    }
    case PointerPhase::Release:
    case PointerPhase::Cancel:
        if (m_brushDrawing)
            endBrushStroke();
        updateBrushCursorFromPointer(ev);
        break;
    }
}

void CanvasView::dispatchSynthesizedMouse(const PointerInputEvent& ev, QEvent::Type type)
{
    // Build a QMouseEvent from the pen and run it through our own mouse handlers so
    // every non-paint tool reacts to the pen exactly as it does to the mouse —
    // without depending on the OS's (unreliable) synthetic mouse. m_synthesizing*
    // marks the re-entry so those handlers don't treat it as a duplicate to drop.
    Qt::MouseButton button = ev.button;
    Qt::MouseButtons buttons = ev.buttons;
    if (type == QEvent::MouseButtonPress) {
        // A pen "press" is the tip touching: model it as the left button so click
        // tools (Select, Crop, Zoom, Move, ...) behave as with a left-click.
        if (button == Qt::NoButton) button = Qt::LeftButton;
        buttons |= Qt::LeftButton;
    } else if (type == QEvent::MouseButtonRelease) {
        if (button == Qt::NoButton) button = Qt::LeftButton;
        buttons &= ~Qt::LeftButton;
    }

    QMouseEvent me(type, ev.widgetPos, ev.globalPos, button, buttons, ev.modifiers);

    m_synthesizingMouseFromTablet = true;
    switch (type) {
    case QEvent::MouseButtonPress:   mousePressEvent(&me); break;
    case QEvent::MouseMove:          mouseMoveEvent(&me); break;
    case QEvent::MouseButtonRelease: mouseReleaseEvent(&me); break;
    default: break;
    }
    m_synthesizingMouseFromTablet = false;
}

void CanvasView::tabletEvent(QTabletEvent* e)
{
    const PointerPhase phase = CanvasInputMapper::mapTabletPhase(e->type());
    PointerInputEvent ev = CanvasInputMapper::fromTabletEvent(e, phase);

    // A QMouseEvent transfers keyboard focus to the widget automatically; a
    // QTabletEvent does not. Without this the canvas never gains focus from the
    // pen, so canvas shortcuts (brush size, tool keys, etc.) stay dead until the
    // user deliberately clicks with the mouse. Grab focus as soon as the pen
    // touches or hovers the canvas. Guarded so we don't re-focus on every sample.
    if (!hasFocus()
        && (phase == PointerPhase::Press
            || phase == PointerPhase::Move
            || phase == PointerPhase::Hover
            || phase == PointerPhase::Enter)) {
        setFocus(Qt::MouseFocusReason);
    }

    // Tablet input is the source of truth while the pen is in use; record it so the
    // OS's own synthetic QMouseEvent twin is suppressed (see mouse path).
    auto* layer = m_doc ? m_doc->activeLayer() : nullptr;
    fillPointerCanvasCoords(ev, layer);
    m_inputState.noteTabletEvent(ev);

    if (phase == PointerPhase::Leave) {
        releaseTabletViewportCursor();
        m_inputState.noteTabletLeave();
        logPointerInput(ev);
        if (m_brushPreviewOverlay)
            m_brushPreviewOverlay->clear();
        update();
        e->accept();
        return;
    }

    updateToolCursor();
    syncTabletViewportCursor();

    if (!m_doc) {
        e->ignore();
        return;
    }

    // Non-paint tools keep their QMouseEvent logic; drive it from the pen by
    // synthesising the matching mouse event ourselves. This is the single root-cause
    // fix that makes Hand (incl. Space-pan), Move, Select, Crop, Gradient, Fill,
    // Eyedropper, Zoom, Text and Shape all respond to the pen. Space-pan is part of
    // mousePressEvent, so it just works through this path too.
    // Keep the override in sync with the live modifier state for the pen too (the
    // mouse path does this on every move); the pen has no mouse-move safety net.
    refreshTemporaryOverride();
    // Gate on the EFFECTIVE tool so a contextual override (e.g. Alt → Eyedropper)
    // routes the pen through the synthesized-mouse path and is processed by the
    // override exactly like a mouse, instead of painting a brush stroke.
    const Tool tool = effectiveTool();
    const bool paintTool = tool == Tool::Brush
                        || tool == Tool::Eraser
                        || isCloneOrHealingTool();
    const bool penPanning = m_spacePanActive || m_panning;
    if (!paintTool || penPanning) {
        switch (phase) {
        case PointerPhase::Press:
            dispatchSynthesizedMouse(ev, QEvent::MouseButtonPress);
            break;
        case PointerPhase::Move:
        case PointerPhase::Hover:
            dispatchSynthesizedMouse(ev, QEvent::MouseMove);
            break;
        case PointerPhase::Release:
            dispatchSynthesizedMouse(ev, QEvent::MouseButtonRelease);
            break;
        default:
            break;
        }
        syncTabletViewportCursor();
        e->accept();
        return;
    }

    // Proximity-in carries no usable position on some drivers; just accept it.
    if (phase == PointerPhase::Enter) {
        logPointerInput(ev);
        e->accept();
        return;
    }

    dispatchBrushPointer(ev);
    syncTabletViewportCursor();
    e->accept();
}

QPointF CanvasView::screenToImage(QPointF screenPos, Layer* layer)
{
    if (!layer) return {};
    float ndcX = 2.0f * static_cast<float>(screenPos.x()) / width() - 1.0f;
    float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / height();

    float invZoom = 1.0f / m_doc->zoom;
    float docNdcX = (ndcX - static_cast<float>(m_doc->panOffset.x())) * invZoom;
    float docNdcY = (ndcY - static_cast<float>(m_doc->panOffset.y())) * invZoom;

    float invHx = 1.0f / m_canvasHalfExtents.x();
    float invHy = 1.0f / m_canvasHalfExtents.y();
    float canvasNdcX = docNdcX * invHx;
    float canvasNdcY = docNdcY * invHy;

    const bool shapeMaskEdit = m_editingMask
        && layer->isShapeLayer()
        && !layer->maskImage.isNull();
    // Masked shapes never render through the sprite (shapeSpriteRenderable), so
    // mask coordinates must be mapped in layer-image space to stay in sync with
    // both the live GPU path and the CPU projection.
    const bool shapeSpriteActive = shapeMaskEdit
        && layer->shapeSpriteRenderable();
    const QTransform t = shapeSpriteActive
        ? layer->shapeCache.spriteTransform
        : (layer->owner ? layer->owner->accumulatedTransform() : QTransform());
    QTransform inv = t.inverted();
    qreal lx, ly;
    inv.map(canvasNdcX, canvasNdcY, &lx, &ly);

    const QSize imgSize = shapeSpriteActive
        ? layer->shapeCache.image.size()
        : (layer->renderRasterStorage().isEnabled()
            ? layer->rasterBaseSize()
            : layer->cpuImage.size());
    float imgX = (static_cast<float>(lx) + 1.0f) * 0.5f * imgSize.width();
    float imgY = (1.0f - static_cast<float>(ly)) * 0.5f * imgSize.height();
    const QPointF rawImagePos(imgX, imgY);

    if (shapeSpriteActive && imgSize.width() > 0 && imgSize.height() > 0) {
        imgX = static_cast<float>(layer->maskOrigin.x())
            + imgX * static_cast<float>(layer->maskImage.width())
                / static_cast<float>(imgSize.width());
        imgY = static_cast<float>(layer->maskOrigin.y())
            + imgY * static_cast<float>(layer->maskImage.height())
                / static_cast<float>(imgSize.height());
    }

    if (m_editingMask) {
        const bool isTiled = layer->renderRasterStorage().isEnabled();
        const QRect lb = isTiled ? layer->renderRasterStorage().logicalBounds() : QRect();
        const QRectF maskBounds(QPointF(layer->maskOrigin), QSizeF(layer->maskImage.size()));
    }

    return {imgX, imgY};
}

void CanvasView::expandLayer(QPointF imagePos)
{
    auto* layer = m_doc->activeLayer();
    if (!layer) return;
    if (!m_editingMask && layer->renderRasterStorage().isEnabled()) return;
    if (m_editingMask) return;

    auto* node = m_doc->activeNode();
    const float r = m_brushSettings.size + 20.0f;

    const QRect required(
        static_cast<int>(imagePos.x() - r),
        static_cast<int>(imagePos.y() - r),
        static_cast<int>(r * 2) + 1,
        static_cast<int>(r * 2) + 1);

    // Early-exit before makeCurrent and GPU readback if no expansion needed.
    // This is the hot path — called on every mouse move during a stroke.
    if (QRect(0, 0, layer->cpuImage.width(), layer->cpuImage.height()).contains(required))
        return;

    makeCurrent();

    // GPU→CPU readback needed so ensureWritableRect can preserve current stroke pixels.
    if (m_brushDrawing && m_gpuViewport)
        m_gpuViewport->syncLayerFromGpu(layer);

    // Pre-compute mask offset before cpuImage grows (same formula as ensureWritableRect).
    const int offX = std::max(0, -required.left());
    const int offY = std::max(0, -required.top());

    QTransform adj;
    const bool expanded = layer->ensureWritableRect(required, &adj);

    if (expanded) {
        if (m_gpuViewport)
            m_gpuViewport->uploadLayerTextureCpu(layer);
        if (m_gpuViewport)
            m_gpuViewport->setupLayerFBO(layer);

        const int newW = layer->cpuImage.width();
        const int newH = layer->cpuImage.height();
        if (layer->tiledSystem) {
            layer->tileManager.resize(newW, newH);
            layer->tileManager.markAllDirty();
        }

        if (node && !adj.isIdentity())
            node->setBaseTransform(adj * node->transform());

        // Grow the mask to match the new cpuImage size, preserving existing content.
        if (!layer->maskImage.isNull() && layer->maskImage.size() != layer->cpuImage.size()) {
            if (m_controller)
                m_controller->syncLayerMaskFromGpu(layer);
            QImage newMask(newW, newH, QImage::Format_Grayscale8);
            newMask.fill(255);
            QPainter mp(&newMask);
            mp.drawImage(offX, offY, layer->maskImage);
            mp.end();
            layer->maskImage = newMask;
            if (m_controller)
                m_controller->syncLayerMaskToGpu(layer);
            if (m_gpuViewport)
                m_gpuViewport->setupMaskFBO(layer);
        }
    }

    doneCurrent();
}

bool CanvasView::expandLayerToRect(LayerTreeNode* node, Layer* layer,
                                   const QRect& requiredLayerRect, QPoint* outOffset)
{
    if (outOffset) *outOffset = QPoint(0, 0);
    if (!layer || layer->cpuImage.isNull() || requiredLayerRect.isEmpty())
        return false;

    const QRect current(0, 0, layer->cpuImage.width(), layer->cpuImage.height());
    if (current.contains(requiredLayerRect))
        return false;

    makeCurrent();

    // Offset existing content shifts by when the canvas grows toward -x/-y (same
    // formula ensureWritableRect uses internally).
    const int offX = std::max(0, -requiredLayerRect.left());
    const int offY = std::max(0, -requiredLayerRect.top());

    QTransform adj;
    const bool expanded = layer->ensureWritableRect(requiredLayerRect, &adj);
    if (expanded) {
        if (m_gpuViewport) {
            m_gpuViewport->uploadLayerTextureCpu(layer);
            m_gpuViewport->setupLayerFBO(layer);
        }

        const int newW = layer->cpuImage.width();
        const int newH = layer->cpuImage.height();
        if (layer->tiledSystem) {
            layer->tileManager.resize(newW, newH);
            layer->tileManager.markAllDirty();
        }

        if (node && !adj.isIdentity())
            node->setBaseTransform(adj * node->transform());

        // Grow the mask to match, preserving existing content at the same offset.
        if (!layer->maskImage.isNull()
            && layer->maskImage.size() != layer->cpuImage.size()) {
            if (m_controller)
                m_controller->syncLayerMaskFromGpu(layer);
            QImage newMask(newW, newH, QImage::Format_Grayscale8);
            newMask.fill(255);
            QPainter mp(&newMask);
            mp.drawImage(offX, offY, layer->maskImage);
            mp.end();
            layer->maskImage = newMask;
            if (m_controller)
                m_controller->syncLayerMaskToGpu(layer);
            if (m_gpuViewport)
                m_gpuViewport->setupMaskFBO(layer);
        }

        if (outOffset) *outOffset = QPoint(offX, offY);
    }

    doneCurrent();
    return expanded;
}

void CanvasView::growMaskToTiledLayer(Layer* layer)
{
    // The mask must stay locked to the layer image bounds. The tiled storage
    // logicalBounds() is an allocation envelope and may include tile padding; use
    // base image bounds united with real content bounds instead.
    if (!layer || layer->maskImage.isNull()) return;
    if (!layer->renderRasterStorage().isEnabled()) return;

    const QRect target = layer->maskTargetBounds();
    if (target.isEmpty()) return;

    const QRect curRect(layer->maskOrigin, layer->maskImage.size());
    if (target.topLeft() == layer->maskOrigin && target.size() == layer->maskImage.size())
        return;

    // The CPU maskImage is authoritative here: this runs right after a *layer*
    // stroke, which never touches the mask (any prior mask edit was already
    // synced GPU→CPU in endBrushStroke). So we resize the CPU buffer directly,
    // with no readback, then re-upload.
    makeCurrent();

    // Offset of the existing mask content inside the target buffer. If undo/crop
    // made the layer bounds smaller, drawing through QPainter clips the stale
    // outside-mask pixels instead of carrying them forward.
    const int offX = layer->maskOrigin.x() - target.left();
    const int offY = layer->maskOrigin.y() - target.top();
    QImage newMask(target.size(), QImage::Format_Grayscale8);
    newMask.fill(255); // newly revealed area is fully visible
    {
        QPainter mp(&newMask);
        mp.drawImage(offX, offY, layer->maskImage);
    }
    layer->maskImage = newMask;
    layer->maskOrigin = target.topLeft();

    if (m_controller)
        m_controller->syncLayerMaskToGpu(layer);
    if (m_gpuViewport)
        m_gpuViewport->setupMaskFBO(layer);
    layer->maskThumbDirty = true;
    doneCurrent();

}

// ── Brush Stroke Methods ─────────────────────────────────────

void CanvasView::beginCloneStampStroke(const BrushInputState& input,
                                       QPointF screenPos,
                                       QPointF documentPos)
{
    if (!m_doc || !m_cloneSourceDefined) {
        m_cloneNeedsSourceFeedback = true;
        updateBrushPreviewOverlay();
        update();
        return;
    }
    if (m_controller)
        m_controller->prepareRasterCelForEdit();
    auto* layer = m_doc->activeLayer();
    if (!layer || !canPaintActiveRasterLayer()) {
        m_brushDrawing = false;
        return;
    }

    if (layer->isDistortLayer())
        layer->distortData.reset();

    layer->enableRasterStorage(m_doc ? m_doc->tileSize() : 256);
    m_brushUsingRasterTiles = layer->renderRasterStorage().isEnabled();
    if (!m_brushUsingRasterTiles)
        return;

    if (!m_cloneAligned || !m_cloneOffsetValid) {
        m_cloneSourceOffset = documentPos - m_cloneSourcePoint;
        m_cloneOffsetValid = true;
    }

    // Snapshot converted ONCE here: both the dab sampler and the per-event
    // source preview read it as RGBA8888, so neither pays a full-document
    // format conversion during the stroke.
    m_cloneStrokeContext.sourceImage =
        cloneSourceSnapshot().convertToFormat(QImage::Format_RGBA8888);
    m_cloneStrokeContext.sourceOffset = m_cloneSourceOffset;
    m_cloneStrokeContext.documentSize = m_doc->size;
    m_cloneStrokeContext.healing = m_stampHealing;
    m_cloneStrokeContext.diffusion = m_healingDiffusion;
    if (!m_cloneStrokeContext.isValid()) {
        m_brushUsingRasterTiles = false;
        return;
    }

    layer->renderRasterStorage().beginChangeTracking();

    BrushInputState state = input;
    state.imagePos = screenToImage(screenPos, layer);
    m_brushSettings.mode = BrushMode::Paint;
    m_brushRenderer.beginStroke();
    m_brushEngine.beginStroke(state, false);

    {
        unsigned int selTex = (m_doc->selection.active() && m_gpuViewport) ? m_gpuViewport->selectMaskTexture() : 0;
        int selW = m_doc->selection.active() ? m_doc->selection.width() : 0;
        int selH = m_doc->selection.active() ? m_doc->selection.height() : 0;
        const QImage* selImage = m_doc->selection.active() ? &m_doc->selection.image() : nullptr;
        m_brushEngine.strokeCloneTo(state, m_brushSettings, m_brushRenderer, layer,
                                    selTex, selW, selH, m_cloneStrokeContext, selImage);
    }

    m_brushDrawing = true;
    m_brushScreenLastPos = screenPos;
    m_airbrushLastPos = state.imagePos;
    m_airbrushLastState = state;
    m_lastBrushImagePos = state.imagePos;
    m_cloneCurrentSourcePoint = documentPos - m_cloneSourceOffset;
    m_cloneNeedsSourceFeedback = false;
    m_brushStrokeTimer.start();

    if (m_brushSettings.airbrush) {
        if (!m_airbrushTimer) {
            m_airbrushTimer = new QTimer(this);
            connect(m_airbrushTimer, &QTimer::timeout, this, &CanvasView::onAirbrushTick);
        }
        // Timed dabs at the preset's rate (dabs per second).
        m_airbrushTimer->start(std::max(5,
            static_cast<int>(std::lround(1000.0f / std::max(1.0f, m_brushSettings.airbrushRate)))));
    }

    update();
    updateBrushPreviewOverlay();
}

void CanvasView::continueCloneStampStroke(const BrushInputState& input,
                                          QPointF screenPos,
                                          QPointF documentPos)
{
    auto* layer = m_doc ? m_doc->activeLayer() : nullptr;
    if (!layer || !m_cloneStrokeContext.isValid())
        return;

    BrushInputState state = input;
    state.imagePos = screenToImage(screenPos, layer);
    m_airbrushLastPos = state.imagePos;
    m_airbrushLastState = state;
    m_cloneCurrentSourcePoint = documentPos - m_cloneSourceOffset;

    {
        unsigned int selTex = (m_doc->selection.active() && m_gpuViewport) ? m_gpuViewport->selectMaskTexture() : 0;
        int selW = m_doc->selection.active() ? m_doc->selection.width() : 0;
        int selH = m_doc->selection.active() ? m_doc->selection.height() : 0;
        const QImage* selImage = m_doc->selection.active() ? &m_doc->selection.image() : nullptr;
        m_brushEngine.strokeCloneTo(state, m_brushSettings, m_brushRenderer, layer,
                                    selTex, selW, selH, m_cloneStrokeContext, selImage);
    }

    m_lastBrushImagePos = state.imagePos;
    update();
    updateBrushPreviewOverlay();
}

void CanvasView::beginBrushStroke(const BrushInputState& input,
                                  QPointF screenPos)
{
    if (m_controller && !m_editingMask)
        m_controller->prepareRasterCelForEdit();
    auto* layer = m_doc->activeLayer();
    if (!layer) return;

    m_brushUsingRasterTiles = false;
    // Lock gate (evaluated once, at the call site). Mask painting only honours
    // Lock All; image painting honours Lock Image Pixels / Lock All (and rejects
    // Text/Shape) — both routed through the controller's central guards so the
    // status message is consistent with every other tool.
    if (m_editingMask) {
        if (m_controller && !m_controller->checkMaskEditable(layer)) {
            m_brushDrawing = false;
            return;
        }
    } else {
        if (m_controller && !m_controller->checkDestructiveOp(layer)) {
            m_brushDrawing = false;
            return;
        }
    }
    // Painting on a distort layer forfeits its re-editability (pixels diverge
    // from the source warp), so drop the distort metadata before the stroke.
    if (!m_editingMask && layer->isDistortLayer())
        layer->distortData.reset();
    if (!m_editingMask && !layer->isTextLayer() && !layer->isShapeLayer()) {
        layer->enableRasterStorage(m_doc ? m_doc->tileSize() : 256);
        m_brushUsingRasterTiles = layer->renderRasterStorage().isEnabled();
        if (m_brushUsingRasterTiles)
            layer->renderRasterStorage().beginChangeTracking();
    }

    if (m_editingMask) {
      makeCurrent();
    }

    // Capture pre-expansion mask state for ALL mask layer types so undo can revert
    // the mask to its exact size before this stroke (including any expansion).
    m_maskStrokeBeforeCpu = QImage();
    if (m_editingMask && !layer->maskImage.isNull()) {
        m_brushBeforeImage = layer->maskImage.copy();
        m_brushBeforeOrigin = layer->maskOrigin;
        if (!layer->renderRasterStorage().isEnabled()) {
            auto* node = m_doc->activeNode();
            m_maskStrokeBeforeCpu = layer->cpuImage.copy();
            m_maskStrokeBeforeTransform = node ? node->transform() : QTransform();
        }
    }

    // Sync CPU mask→GPU before expanding so the GPU texture dimensions match the
    // current (post-undo) CPU mask dimensions. Without this, a stale large GPU texture
    // can overflow the CPU mask buffer inside glGetTexImage later in the stroke.
    // If the layer texture is stale (textureOutdated=true, set by FilterCommand on undo),
    // the GPU mask texture is also stale and the maskFbo has the wrong dimensions.
    // Re-sync CPU mask→GPU and recreate the FBO before any GL operations below.
    if (!m_brushUsingRasterTiles && m_editingMask
            && !layer->maskImage.isNull() && layer->textureOutdated
            && m_gpuViewport && m_controller) {
        makeCurrent();
        m_controller->syncLayerMaskToGpu(layer);
        m_gpuViewport->setupMaskFBO(layer);
        doneCurrent();
    }

    if (!m_brushUsingRasterTiles)
        expandLayer(input.imagePos);
    layer = m_doc->activeLayer();
    if (!layer) return;

    BrushInputState state = input;
    state.imagePos = screenToImage(screenPos, layer);

    if (m_editingMask && !pointInsideMaskBounds(layer, state.imagePos)) {
        return;
    }

    makeCurrent();
    if (!m_editingMask || layer->maskImage.isNull()) {
        // Non-mask stroke: capture the current CPU image as the undo baseline.
        if (!m_brushUsingRasterTiles)
            m_brushBeforeImage = layer->cpuImage.copy();
        else
            m_brushBeforeImage = QImage();
    }
    // Mask stroke: m_brushBeforeImage was already captured before expandLayer above.
    m_brushRenderer.updateStamp(m_brushSettings);
    m_brushRenderer.beginStroke();
    m_brushEngine.beginStroke(state, m_editingMask);

    {   unsigned int selTex = (m_doc->selection.active() && m_gpuViewport) ? m_gpuViewport->selectMaskTexture() : 0;
        int selW = m_doc->selection.active() ? m_doc->selection.width() : 0;
        int selH = m_doc->selection.active() ? m_doc->selection.height() : 0;
        const QImage* selImage = m_doc->selection.active() ? &m_doc->selection.image() : nullptr;
        m_brushEngine.strokeTo(state, m_brushSettings, m_brushRenderer, layer,
                               selTex, selW, selH, selImage);
    }
    previewAdjustmentMaskEdit(layer);
    doneCurrent();

    m_brushDrawing = true;
    m_brushScreenLastPos = screenPos;
    m_airbrushLastPos = state.imagePos;
    m_airbrushLastState = state;

    // Track dirty region for tiled system
    m_lastBrushImagePos = state.imagePos;
    if (layer->tiledSystem && !m_brushUsingRasterTiles) {
        float r = m_brushSettings.size * 0.5f;
        if (r < 1.0f) r = 1.0f;
        layer->dirtyRegion.clear();
        layer->dirtyRegion.addCircle(
            QPoint(static_cast<int>(state.imagePos.x()),
                   static_cast<int>(state.imagePos.y())),
            static_cast<int>(r));
    }
    m_brushStrokeTimer.start();

    if (m_brushSettings.airbrush) {
        if (!m_airbrushTimer) {
            m_airbrushTimer = new QTimer(this);
            connect(m_airbrushTimer, &QTimer::timeout, this, &CanvasView::onAirbrushTick);
        }
        // Timed dabs at the preset's rate (dabs per second).
        m_airbrushTimer->start(std::max(5,
            static_cast<int>(std::lround(1000.0f / std::max(1.0f, m_brushSettings.airbrushRate)))));
    }

    update();
}

void CanvasView::previewAdjustmentMaskEdit(Layer* layer)
{
    // A mask painted on the GPU FBO is invisible until synced back whenever the
    // mask is baked into a CPU "effected" image rather than sampled live by the
    // GPU mask uniform. Two such cases involve adjustment layers:
    //   1. Editing a Single-Layer-Mode adjustment's own mask — baked into the
    //      parent layer's effected image. (Normal-Mode/stack adjustments sample
    //      the GPU mask directly and are already live.)
    //   2. Editing the mask of a layer that *hosts* Single-Layer-Mode
    //      adjustments — that layer renders through its effected image, so its
    //      own mask is baked too.
    // In both cases sync GPU→CPU + invalidate the bake each dab so the effect
    // previews in real time.
    if (!m_editingMask || !m_controller || !layer || layer->maskImage.isNull())
        return;
    auto* node = m_doc ? m_doc->activeNode() : nullptr;
    if (!node)
        return;

    bool maskIsBaked = false;
    if (node->type == LayerTreeNode::Type::Adjustment) {
        // Case 1: only when nested under a layer (stack adjustments are live).
        maskIsBaked = node->parent
                   && node->parent->type == LayerTreeNode::Type::Layer;
    } else if (node->type == LayerTreeNode::Type::Layer) {
        // Case 2: layer hosting Single-Layer-Mode adjustments.
        maskIsBaked = node->hasVisibleAdjustmentChildren();
    }
    if (!maskIsBaked)
        return;

    m_controller->syncLayerMaskFromGpu(layer);
    node->invalidateEffects();   // case 1 propagates into the parent's bake
}

void CanvasView::continueBrushStroke(const BrushInputState& input,
                                     QPointF screenPos)
{
    auto* layer = m_doc->activeLayer();
    if (!layer) return;

    if (!m_brushUsingRasterTiles)
        expandLayer(input.imagePos);
    layer = m_doc->activeLayer();
    if (!layer) return;

    BrushInputState state = input;
    state.imagePos = screenToImage(screenPos, layer);

    if (m_editingMask) {
        if (!pointInsideMaskBounds(layer, state.imagePos)) {
            return;
        }
    }

    m_airbrushLastPos = state.imagePos;
    m_airbrushLastState = state;

    // Track dirty region for tiled system
    if (layer->tiledSystem && !m_brushUsingRasterTiles) {
        float r = m_brushSettings.size * 0.5f;
        if (r < 1.0f) r = 1.0f;
        QPointF prev = m_lastBrushImagePos;
        QPointF cur = state.imagePos;
        QRect segRect(
            static_cast<int>(std::floor(std::min(prev.x(), cur.x()) - r)),
            static_cast<int>(std::floor(std::min(prev.y(), cur.y()) - r)),
            static_cast<int>(std::ceil(std::abs(cur.x() - prev.x()) + r * 2 + 1)),
            static_cast<int>(std::ceil(std::abs(cur.y() - prev.y()) + r * 2 + 1))
        );
        layer->dirtyRegion.addRect(segRect);
        m_lastBrushImagePos = cur;
    }

    makeCurrent();
    {   unsigned int selTex = (m_doc->selection.active() && m_gpuViewport) ? m_gpuViewport->selectMaskTexture() : 0;
        int selW = m_doc->selection.active() ? m_doc->selection.width() : 0;
        int selH = m_doc->selection.active() ? m_doc->selection.height() : 0;
        const QImage* selImage = m_doc->selection.active() ? &m_doc->selection.image() : nullptr;
        m_brushEngine.strokeTo(state, m_brushSettings, m_brushRenderer, layer,
                               selTex, selW, selH, selImage);
    }
    previewAdjustmentMaskEdit(layer);

    update();
}

void CanvasView::endBrushStroke()
{
    m_brushDrawing = false;
    if (m_airbrushTimer)
        m_airbrushTimer->stop();
    m_brushEngine.endStroke();
    m_brushRenderer.endStroke();
    if (m_controller) {
        auto* layer = m_doc->activeLayer();
        if (!layer) {
            m_brushUsingRasterTiles = false;
            // Close any grouped-undo opened by an auto-created layer so it never
            // leaks past the stroke.
            m_controller->history().endMacro();
            return;
        }
        makeCurrent();
        if (m_editingMask && !layer->maskImage.isNull()) {
            m_controller->syncLayerMaskFromGpu(layer);
            layer->maskTextureOutdated = true;
            if (layer->owner) {
                layer->owner->invalidateEffects();
                layer->owner->thumbnailDirty = true;
            }
        } else if (m_brushUsingRasterTiles && layer->renderRasterStorage().isEnabled()) {
            layer->pendingGpuUpload = true;
            layer->textureOutdated = true;
            if (layer->owner) {
                layer->owner->invalidateEffects();
                layer->owner->thumbnailDirty = true;
            }
        } else {
            m_controller->syncLayerFromGpu(layer);
            if (m_gpuViewport)
                m_gpuViewport->invalidateMipmaps(layer);
            if (layer->owner) {
                layer->owner->invalidateEffects();
                layer->owner->thumbnailDirty = true;
            }
        }
        doneCurrent();

        // A raster *layer* stroke may allocate more storage tiles. That is not
        // layer-image growth by itself; keep an existing mask tied to
        // rasterBaseSize(), not to the tile envelope.
        if (!m_editingMask && m_brushUsingRasterTiles
                && layer->renderRasterStorage().isEnabled() && !layer->maskImage.isNull()) {
            growMaskToTiledLayer(layer);
        }

        // Mark dirty tiles for the stroke area (tiled system)
        if (layer->tiledSystem && !m_brushUsingRasterTiles) {
            layer->dirtyRegion.consolidate();
            QRect dirtyBounds = layer->dirtyRegion.boundingRect();
            if (!dirtyBounds.isEmpty()) {
                layer->tileManager.markDirty(dirtyBounds);
                layer->pendingGpuUpload = true;
            }
            layer->dirtyRegion.clear();
        }

        clearSnapBoundsCache();
        invalidateMultiOutlineCache();
        if (m_doc) ++m_doc->compositionGeneration;

        if (m_editingMask && !layer->maskImage.isNull()) {
            // If a flat-image layer was expanded during the stroke, include the layer
            // image restoration in the undo so the layer size is fully reverted.
            if (!m_maskStrokeBeforeCpu.isNull() &&
                m_maskStrokeBeforeCpu.size() != layer->cpuImage.size()) {
                auto* node = m_doc->activeNode();
                auto composite = std::make_unique<CompositeCommand>("brush_stroke");
                composite->add(std::make_unique<FilterCommand>(
                    m_doc, m_doc->activeFlatIndex,
                    m_maskStrokeBeforeCpu, m_maskStrokeBeforeTransform,
                    layer->cpuImage.copy(), node ? node->transform() : QTransform(),
                    "brush_stroke_layer"));
                composite->add(std::make_unique<MaskEditCommand>(
                    m_doc, m_doc->activeFlatIndex,
                    m_brushBeforeImage, layer->maskImage.copy(),
                    "brush_stroke_mask",
                    m_brushBeforeOrigin, layer->maskOrigin));
                m_controller->pushCommand(std::move(composite));
            } else {
                m_controller->pushMaskEditSnapshot("brush_stroke",
                    m_doc->activeFlatIndex, m_brushBeforeImage,
                    m_brushBeforeOrigin, layer->maskOrigin);
            }
        } else if (m_brushUsingRasterTiles && layer->renderRasterStorage().isEnabled()) {
            auto changes = layer->renderRasterStorage().endChangeTracking();
            m_controller->pushRasterTileSnapshot("brush_stroke",
                m_doc->activeFlatIndex, std::move(changes));
        } else {
            m_controller->pushLayerSnapshot("brush_stroke",
                m_doc->activeFlatIndex, m_brushBeforeImage);
        }
        // Commit the grouped-undo step (layer-create + stroke) as one entry. A
        // no-op when this stroke did not auto-create a layer.
        m_controller->history().endMacro();
    }
    m_brushUsingRasterTiles = false;
    m_cloneStrokeContext = CloneStampContext();
}

QPointF CanvasView::constrainedGradientPoint(QPointF start, QPointF current,
                                             Qt::KeyboardModifiers modifiers) const
{
    if (!(modifiers & Qt::ShiftModifier))
        return current;

    QPointF delta = current - start;
    const double length = std::hypot(delta.x(), delta.y());
    if (length < 1e-6)
        return current;

    constexpr double step = 3.14159265358979323846 / 12.0;
    const double angle = std::atan2(delta.y(), delta.x());
    const double snapped = std::round(angle / step) * step;
    return QPointF(start.x() + std::cos(snapped) * length,
                   start.y() + std::sin(snapped) * length);
}

void CanvasView::beginGradientDrag(QPointF screenPos)
{
    if (!m_doc || !m_controller)
        return;

    // The drag/overlay live in screen space, so they work before a target layer
    // exists. Image-space endpoints are (re)computed at apply time against the
    // layer that is active then — the current one, or one auto-created on commit.
    m_gradientDragging = true;
    m_gradientStartScreen = screenPos;
    m_gradientCurrentScreen = screenPos;
    if (auto* layer = m_doc->activeLayer()) {
        m_gradientStartImage = screenToImage(screenPos, layer);
        m_gradientCurrentImage = m_gradientStartImage;
    }
    if (m_gradientOverlay)
        m_gradientOverlay->setLine(m_gradientStartScreen, m_gradientCurrentScreen,
                                   m_gradientDefinition.kind);
    update();
}

void CanvasView::updateGradientDrag(QPointF screenPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_gradientDragging || !m_doc)
        return;

    const QPointF constrainedScreen = constrainedGradientPoint(
        m_gradientStartScreen, screenPos, modifiers);
    QPointF overlayStart = m_gradientStartScreen;
    QPointF overlayEnd = constrainedScreen;
    if ((modifiers & Qt::AltModifier)
        && (m_gradientDefinition.kind == GradientKind::Linear
            || m_gradientDefinition.kind == GradientKind::Reflected)) {
        overlayStart = m_gradientStartScreen - (constrainedScreen - m_gradientStartScreen);
    }

    m_gradientCurrentScreen = constrainedScreen;
    // Image-space endpoint is only needed at apply; skip it when no layer yet.
    if (auto* layer = m_doc->activeLayer())
        m_gradientCurrentImage = screenToImage(constrainedScreen, layer);
    if (m_gradientOverlay)
        m_gradientOverlay->setLine(overlayStart, overlayEnd, m_gradientDefinition.kind);
    update();
}

void CanvasView::endGradientDrag(QPointF screenPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_gradientDragging || !m_doc || !m_controller)
        return;

    const QPointF constrainedScreen = constrainedGradientPoint(
        m_gradientStartScreen, screenPos, modifiers);

    // Only a real drag applies a gradient (a bare click is a no-op). Gate in
    // screen space so this works before any target layer exists.
    const double screenDist = std::hypot(constrainedScreen.x() - m_gradientStartScreen.x(),
                                          constrainedScreen.y() - m_gradientStartScreen.y());

    // End the gesture before any auto-create: newLayer() emits activeLayerChanged,
    // whose handler cancels an in-progress gradient drag.
    m_gradientDragging = false;
    if (m_gradientOverlay)
        m_gradientOverlay->finish();

    if (screenDist >= 1.0 && ensurePaintTargetLayer(tr("Gradient"))) {
        // Recompute both endpoints against the layer that is active now (the
        // existing one, or the one just auto-created — which has an identity
        // transform, so screen→image maps to document space).
        if (auto* layer = m_doc->activeLayer()) {
            QPointF startImage = screenToImage(m_gradientStartScreen, layer);
            QPointF endImage = screenToImage(constrainedScreen, layer);
            if ((modifiers & Qt::AltModifier)
                && (m_gradientDefinition.kind == GradientKind::Linear
                    || m_gradientDefinition.kind == GradientKind::Reflected)) {
                startImage = startImage - (endImage - startImage);
            }
            const double distance = std::hypot(endImage.x() - startImage.x(),
                                               endImage.y() - startImage.y());
            if (distance >= 1.0) {
                GradientApplication application;
                application.definition = m_gradientDefinition;
                application.definition.normalize();
                application.startPoint = startImage;
                application.endPoint = endImage;
                application.blendMode = m_gradientBlendMode;
                application.opacity = m_gradientOpacity;
                m_controller->applyGradient(application);
            }
        }
        m_controller->history().endMacro();   // commit grouped undo (no-op if none)
    }

    update();
}

void CanvasView::cancelGradientDrag()
{
    m_gradientDragging = false;
    if (m_gradientOverlay)
        m_gradientOverlay->finish();
    updateToolCursor();
    update();
}

// Lock Transparent Pixels honoured at the call site (no render-path changes):
// a paint op may recolour pixels but must never alter their alpha. Copies the
// alpha channel of `before` back over `result` (matching geometry). Pixels that
// were fully transparent stay transparent — i.e. paint only lands where alpha>0.
static void restoreAlphaFromSnapshot(QImage& result, const QImage& before)
{
    if (result.isNull() || before.isNull() || result.size() != before.size())
        return;
    if (result.format() != QImage::Format_RGBA8888)
        result = result.convertToFormat(QImage::Format_RGBA8888);
    const QImage b = before.convertToFormat(QImage::Format_RGBA8888);
    const int w = result.width(), h = result.height();
    for (int y = 0; y < h; ++y) {
        uchar* rp = result.scanLine(y);
        const uchar* bp = b.constScanLine(y);
        for (int x = 0; x < w; ++x)
            rp[x * 4 + 3] = bp[x * 4 + 3];
    }
}

void CanvasView::fillBucket(QPointF screenPos)
{
    auto* node = m_doc->activeNode();
    auto* layer = m_doc->activeLayer();
    if (!node || !layer) return;
    if (!m_controller) return;

    if (layer->renderRasterStorage().isEnabled()) {
        layer->cpuImage = layer->compositeImage();
        layer->renderRasterStorage().clear();
        layer->textureOutdated = true;
    }

    QPointF imgPos = screenToImage(screenPos, layer);
    // floor (not truncate) so clicks outside the layer to the left/top map to the
    // correct negative pixel — needed before the layer is expanded below.
    int ix = static_cast<int>(std::floor(imgPos.x()));
    int iy = static_cast<int>(std::floor(imgPos.y()));

    QColor color = m_fillBucketColor;
    int tolerance = m_fillBucketTolerance;

    const bool hasSelection = m_doc->selection.active() && !m_doc->selection.isEmpty();

    // Snapshot BEFORE any expansion so undo restores the original pixels AND the
    // original transform (the expansion adjusts node->transform()).
    QImage before = layer->cpuImage.copy();
    QTransform beforeT = node->transform();

    // A flood fill (no selection) can create content in transparent areas outside
    // the layer's current bounds: grow the layer's internal canvas to span the
    // document so the fill floods the whole visible area, then carry on at the
    // click point. Consistent with brush, which grows the layer on demand.
    // Transparency-locked layers must not gain opacity in empty areas, so skip.
    if (!hasSelection && !node->isTransparencyLocked()) {
        bool invOk = false;
        const QTransform inv = node->accumulatedTransform().inverted(&invOk);
        if (invOk) {
            const QSize docSize = m_doc->size;
            const QSize imgSize = layer->cpuImage.size();
            auto docToLayer = [&](double dx, double dy) {
                const double cnx = dx / std::max(1, docSize.width()) * 2.0 - 1.0;
                const double cny = 1.0 - dy / std::max(1, docSize.height()) * 2.0;
                qreal lx = 0.0, ly = 0.0;
                inv.map(cnx, cny, &lx, &ly);
                return QPointF((lx + 1.0) * 0.5 * imgSize.width(),
                               (1.0 - ly) * 0.5 * imgSize.height());
            };
            QPolygonF docPoly;
            docPoly << docToLayer(0, 0)
                    << docToLayer(docSize.width(), 0)
                    << docToLayer(docSize.width(), docSize.height())
                    << docToLayer(0, docSize.height());
            QRect required = QRect(0, 0, imgSize.width(), imgSize.height())
                                 .united(docPoly.boundingRect().toAlignedRect())
                                 .united(QRect(ix, iy, 1, 1));
            // Safety cap: a heavily down-scaled layer maps the document to a huge
            // layer-pixel area — don't request a gigantic buffer.
            constexpr int kMaxFillDim = 16384;
            if (required.width() <= kMaxFillDim && required.height() <= kMaxFillDim) {
                QPoint off;
                if (expandLayerToRect(node, layer, required, &off)) {
                    ix += off.x();
                    iy += off.y();
                }
            }
        }
    }

    int lw = layer->cpuImage.width();
    int lh = layer->cpuImage.height();
    if (ix < 0 || iy < 0 || ix >= lw || iy >= lh) return;

    cv::Mat cvImg = ImageEngine::toCvMatFast(layer->cpuImage);
    if (cvImg.empty()) return;

    if (hasSelection) {
        cv::Mat layerMask = m_controller->makeLayerMask(layer);
        cv::Mat filled(cvImg.size(), cvImg.type(),
            ImageEngine::qColorToScalar(color));
        if (!layerMask.empty())
            filled.copyTo(cvImg, layerMask);
        else
            cvImg = filled;
    } else {
        cv::Scalar cvColor = ImageEngine::qColorToScalar(color);
        cv::Mat result = ImageEngine::fillRegion(cvImg, ix, iy, cvColor,
            tolerance / 255.0f);
        cvImg = result;
    }

    if (cvImg.empty()) return;

    layer->cpuImage = ImageEngine::toQImageFast(cvImg);

    // Lock Transparent Pixels: fill recolours existing pixels but cannot create
    // opacity in transparent areas. (Expansion was skipped in this case, so the
    // snapshot still matches the current image size.)
    if (node->isTransparencyLocked())
        restoreAlphaFromSnapshot(layer->cpuImage, before);

    m_controller->markLayerDirty(layer);
    layer->textureOutdated = true;
    m_controller->syncLayerToGpu(layer);
    // Push manually (not pushLayerSnapshot) so the before/after transforms differ
    // when the layer was expanded — undo must restore the pre-expansion frame.
    m_controller->history().push(std::make_unique<FilterCommand>(
        m_doc, m_doc->activeFlatIndex,
        std::move(before), beforeT,
        layer->cpuImage.copy(), node->transform(),
        tr("Fill Bucket")));
    emit m_controller->imageChanged();
    update();
}

void CanvasView::onAirbrushTick()
{
    if (!m_brushDrawing) return;
    auto* layer = m_doc->activeLayer();
    if (!layer) return;

    // Paint with the pen's current sample (pressure/tilt/rotation) held at the
    // last known position, so a stationary airbrush builds up with the real
    // pressure instead of a neutral full-pressure state.
    BrushInputState s = m_airbrushLastState;
    s.imagePos = m_airbrushLastPos;
    s.velocity = 0.0f;

    makeCurrent();
    {   unsigned int selTex = (m_doc->selection.active() && m_gpuViewport) ? m_gpuViewport->selectMaskTexture() : 0;
        int selW = m_doc->selection.active() ? m_doc->selection.width() : 0;
        int selH = m_doc->selection.active() ? m_doc->selection.height() : 0;
        const QImage* selImage = m_doc->selection.active() ? &m_doc->selection.image() : nullptr;
        if (isCloneOrHealingTool() && m_cloneStrokeContext.isValid()) {
            m_brushEngine.airbrushCloneDab(s, m_brushSettings, m_brushRenderer, layer,
                                           selTex, selW, selH, m_cloneStrokeContext, selImage);
        } else {
            m_brushEngine.airbrushDab(s, m_brushSettings, m_brushRenderer, layer,
                                      selTex, selW, selH, selImage);
        }
    }
    doneCurrent();

    // Track dirty region for tiled system
    if (layer->tiledSystem && !m_brushUsingRasterTiles) {
        float r = m_brushSettings.size * 0.5f;
        if (r < 1.0f) r = 1.0f;
        layer->dirtyRegion.addCircle(
            QPoint(static_cast<int>(s.imagePos.x()),
                   static_cast<int>(s.imagePos.y())),
            static_cast<int>(r));
    }

    update();
}

// ── Selection Methods ────────────────────────────────────────

QPointF CanvasView::screenToDocument(QPointF screenPos)
{
    float ndcX = 2.0f * static_cast<float>(screenPos.x()) / width() - 1.0f;
    float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / height();

    float invZoom = 1.0f / m_doc->zoom;
    float docNdcX = (ndcX - static_cast<float>(m_doc->panOffset.x())) * invZoom;
    float docNdcY = (ndcY - static_cast<float>(m_doc->panOffset.y())) * invZoom;

    float invHx = 1.0f / m_canvasHalfExtents.x();
    float invHy = 1.0f / m_canvasHalfExtents.y();
    float canvasNdcX = docNdcX * invHx;
    float canvasNdcY = docNdcY * invHy;

    float docX = (canvasNdcX + 1.0f) * 0.5f * m_doc->size.width();
    float docY = (1.0f - canvasNdcY) * 0.5f * m_doc->size.height();

    return {docX, docY};
}

// ── AI Object Selection tool ─────────────────────────────────

AiObjectSelectionController* CanvasView::aiSelectionController()
{
    if (!m_aiSelectController && m_controller)
        m_aiSelectController = new AiObjectSelectionController(m_controller, this, this);
    return m_aiSelectController;
}

AiRemoveObjectController* CanvasView::aiRemoveController()
{
    if (!m_aiRemoveController && m_controller)
        m_aiRemoveController = new AiRemoveObjectController(m_controller, this, this);
    return m_aiRemoveController;
}

AiSelectionOperation CanvasView::aiResolveOperation(Qt::KeyboardModifiers mods) const
{
    // Modifiers map to selection operations, matching the other selection tools:
    // Shift = Add, Alt = Subtract, Shift+Alt = Intersect; otherwise the options-
    // bar default the controller holds.
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    if (shift && alt) return AiSelectionOperation::Intersect;
    if (shift)        return AiSelectionOperation::Add;
    if (alt)          return AiSelectionOperation::Subtract;
    return m_aiSelectController ? m_aiSelectController->operation()
                               : AiSelectionOperation::Replace;
}

void CanvasView::aiSelectMousePress(QMouseEvent* e)
{
    aiSelectionController(); // ensure created
    m_aiPressScreen = e->position();
    m_aiPressDoc = screenToDocument(e->position());
    m_aiMoved = false;
    m_aiBoxDragging = false;
}

void CanvasView::aiSelectMouseMove(QMouseEvent* e)
{
    emit mouseImageCoordChanged(screenToDocument(e->position()));

    if (!(e->buttons() & Qt::LeftButton))
        return;

    const QPointF cur = e->position();
    if (!m_aiMoved) {
        // Promote to a box drag only after a small threshold so a slightly shaky
        // click is still treated as a click.
        if ((cur - m_aiPressScreen).manhattanLength() < 4)
            return;
        m_aiMoved = true;
        m_aiBoxDragging = true;
        if (m_selectDragOverlay)
            m_selectDragOverlay->beginRect(m_aiPressScreen, false);
    }
    if (m_aiBoxDragging && m_selectDragOverlay)
        m_selectDragOverlay->updateRect(cur);
}

void CanvasView::aiSelectMouseRelease(QMouseEvent* e)
{
    auto* controller = aiSelectionController();
    if (m_aiBoxDragging && m_selectDragOverlay)
        m_selectDragOverlay->finish();
    m_aiBoxDragging = false;

    if (!controller)
        return;

    const AiSelectionOperation op = aiResolveOperation(e->modifiers());

    if (m_aiMoved) {
        const QPointF startDoc = m_aiPressDoc;
        const QPointF endDoc = screenToDocument(e->position());
        const QRectF box = QRectF(startDoc, endDoc).normalized();
        controller->boxAt(box, op);
    } else {
        controller->clickAt(screenToDocument(e->position()), op, /*foreground*/ true);
    }
    m_aiMoved = false;
}

void CanvasView::aiRemoveMousePress(QMouseEvent* e)
{
    aiRemoveController();
    m_aiRemoveLassoDrawing = true;
    m_aiRemoveLassoPoints.clear();
    const QPointF docPos = screenToDocument(e->position());
    m_aiRemoveLassoPoints.push_back(docPos);
    if (m_selectDragOverlay)
        m_selectDragOverlay->beginLasso(e->position());
}

void CanvasView::aiRemoveMouseMove(QMouseEvent* e)
{
    emit mouseImageCoordChanged(screenToDocument(e->position()));
    if (!m_aiRemoveLassoDrawing || !(e->buttons() & Qt::LeftButton))
        return;
    const QPointF docPos = screenToDocument(e->position());
    if (m_aiRemoveLassoPoints.empty() || docPos != m_aiRemoveLassoPoints.back()) {
        m_aiRemoveLassoPoints.push_back(docPos);
        if (m_selectDragOverlay)
            m_selectDragOverlay->addLassoPoint(e->position());
    }
}

void CanvasView::aiRemoveMouseRelease(QMouseEvent* e)
{
    if (!m_aiRemoveLassoDrawing)
        return;
    const QPointF docPos = screenToDocument(e->position());
    if (m_aiRemoveLassoPoints.empty() || docPos != m_aiRemoveLassoPoints.back())
        m_aiRemoveLassoPoints.push_back(docPos);
    finishAiRemoveLasso();
}

void CanvasView::cancelAiRemoveLasso()
{
    m_aiRemoveLassoDrawing = false;
    m_aiRemoveLassoPoints.clear();
    if (m_selectDragOverlay)
        m_selectDragOverlay->finish();
}

void CanvasView::finishAiRemoveLasso()
{
    if (m_selectDragOverlay)
        m_selectDragOverlay->finish();
    m_aiRemoveLassoDrawing = false;

    if (!m_doc || m_aiRemoveLassoPoints.size() < 3) {
        m_aiRemoveLassoPoints.clear();
        return;
    }

    SelectionMask temp;
    temp.create(m_doc->size.width(), m_doc->size.height());
    temp.setPolygon(m_aiRemoveLassoPoints, SelectMode::Replace, true);
    const QImage mask = temp.image().copy();
    m_aiRemoveLassoPoints.clear();

    if (auto* controller = aiRemoveController())
        controller->removeObject(mask);
}


void CanvasView::updateMagneticEdgeMap()
{
    m_magneticEdgeReady = false;
    if (!m_doc || m_doc->size.isEmpty()) return;

    // Edge-detect the document composite in document space: the active layer
    // alone misses edges from other layers, reads stale pixels on tiled
    // layers, and its pixel grid distorts the snap radius once transformed.
    RenderContext ctx;
    ctx.document = m_doc;
    ctx.outputSize = m_doc->size;
    ctx.documentRect = QRectF(QPointF(0, 0), m_doc->size);
    ctx.targetType = RenderTargetType::Canvas;
    ctx.highQuality = true;
    QImage comp = DocumentCompositor::composite(m_doc, ctx);
    if (comp.isNull()) return;

    cv::Mat cvImg = ImageEngine::toCvMat(comp);
    if (cvImg.empty()) return;

    cv::Mat gray;
    cv::cvtColor(cvImg, gray, cv::COLOR_BGRA2GRAY);

    cv::Mat gradX, gradY;
    cv::Sobel(gray, gradX, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gradY, CV_32F, 0, 1, 3);

    m_magneticEdgeW = gray.cols;
    m_magneticEdgeH = gray.rows;
    m_magneticEdgeMap.resize(m_magneticEdgeW * m_magneticEdgeH);

    float maxGrad = 0.0f;
    for (int y = 0; y < m_magneticEdgeH; ++y) {
        const float* gxRow = gradX.ptr<float>(y);
        const float* gyRow = gradY.ptr<float>(y);
        for (int x = 0; x < m_magneticEdgeW; ++x) {
            float g = std::sqrt(gxRow[x] * gxRow[x] + gyRow[x] * gyRow[x]);
            m_magneticEdgeMap[y * m_magneticEdgeW + x] = g;
            if (g > maxGrad) maxGrad = g;
        }
    }

    // Normalize to 0..1
    if (maxGrad > 0.001f) {
        for (auto& v : m_magneticEdgeMap)
            v /= maxGrad;
    }

    m_magneticEdgeReady = true;
}

QPointF CanvasView::documentToLayerImage(QPointF docPos, Layer* layer) const
{
    if (!m_doc || !layer || m_doc->size.width() <= 0 || m_doc->size.height() <= 0)
        return {};

    const QSize imgSize = layer->renderRasterStorage().isEnabled()
        ? layer->rasterBaseSize()
        : layer->cpuImage.size();
    if (imgSize.width() <= 0 || imgSize.height() <= 0)
        return {};

    const qreal canvasX = docPos.x() / static_cast<qreal>(m_doc->size.width()) * 2.0 - 1.0;
    const qreal canvasY = 1.0 - docPos.y() / static_cast<qreal>(m_doc->size.height()) * 2.0;

    const QTransform layerTransform = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
    const QTransform inv = layerTransform.inverted();
    qreal layerX = 0.0;
    qreal layerY = 0.0;
    inv.map(canvasX, canvasY, &layerX, &layerY);

    return QPointF((layerX + 1.0) * 0.5 * imgSize.width(),
                   (1.0 - layerY) * 0.5 * imgSize.height());
}

QPointF CanvasView::layerImageToDocument(QPointF imagePos, Layer* layer) const
{
    if (!m_doc || !layer || m_doc->size.width() <= 0 || m_doc->size.height() <= 0)
        return {};

    const QSize imgSize = layer->renderRasterStorage().isEnabled()
        ? layer->rasterBaseSize()
        : layer->cpuImage.size();
    if (imgSize.width() <= 0 || imgSize.height() <= 0)
        return {};

    const qreal layerX = imagePos.x() / static_cast<qreal>(imgSize.width()) * 2.0 - 1.0;
    const qreal layerY = 1.0 - imagePos.y() / static_cast<qreal>(imgSize.height()) * 2.0;

    const QTransform layerTransform = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
    qreal canvasX = 0.0;
    qreal canvasY = 0.0;
    layerTransform.map(layerX, layerY, &canvasX, &canvasY);

    return QPointF((canvasX + 1.0) * 0.5 * m_doc->size.width(),
                   (1.0 - canvasY) * 0.5 * m_doc->size.height());
}

QPointF CanvasView::snapToEdge(QPointF docPos)
{
    if (!m_magneticEdgeReady || !m_doc) return docPos;

    // The edge map is in document space; convert the screen-px search radius
    // to doc px so the magnet feels the same at any zoom.
    int cx = static_cast<int>(std::round(docPos.x()));
    int cy = static_cast<int>(std::round(docPos.y()));
    const float zoom = m_doc->zoom > 0.01f ? m_doc->zoom : 1.0f;
    int radius = std::max(1, static_cast<int>(std::round(m_magneticSearchRadius / zoom)));

    if (cx < 0 || cx >= m_magneticEdgeW || cy < 0 || cy >= m_magneticEdgeH)
        return docPos;

    float bestVal = -1.0f;
    int bestX = cx, bestY = cy;

    int x1 = std::max(0, cx - radius);
    int y1 = std::max(0, cy - radius);
    int x2 = std::min(m_magneticEdgeW - 1, cx + radius);
    int y2 = std::min(m_magneticEdgeH - 1, cy + radius);

    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            float g = m_magneticEdgeMap[y * m_magneticEdgeW + x];
            if (g > bestVal) {
                bestVal = g;
                bestX = x;
                bestY = y;
            }
        }
    }

    return QPointF(static_cast<qreal>(bestX), static_cast<qreal>(bestY));
}

QImage CanvasView::quickToolSourceImage(Layer* layer) const
{
    // compositeImage() folds rasterStorage tiles (the truth for tiled layers,
    // where cpuImage is stale) and falls back to a cpuImage copy otherwise.
    return layer ? layer->compositeImage() : QImage();
}

void CanvasView::pushSelectionGestureUndo(const char* name)
{
    if (!m_controller || !m_doc) return;
    QImage after = m_doc->selection.image().copy();
    bool afterActive = m_doc->selection.active();
    if (after == m_selectGestureBefore
        && afterActive == m_selectGestureBeforeActive)
        return;
    m_controller->history().push(std::make_unique<SelectionCommand>(
        m_doc, m_selectGestureBefore, after,
        m_selectGestureBeforeActive, afterActive, name));
    emit m_controller->selectionChanged();
}

void CanvasView::reprojectSelectionDragOverlay()
{
    if (!m_selectDragOverlay || m_currentTool != Tool::Select) return;

    if (m_selectDragging) {
        bool ellipse = m_selectType == SelectType::Elliptical;
        m_selectDragOverlay->beginRect(documentToScreen(m_selectStart), ellipse);
        m_selectDragOverlay->updateRect(documentToScreen(m_selectCurrent));
    } else if (m_lassoDrawing) {
        QVector<QPointF> pts;
        pts.reserve(static_cast<int>(m_lassoPoints.size()));
        for (const QPointF& p : m_lassoPoints)
            pts.push_back(documentToScreen(p));
        m_selectDragOverlay->setLassoPath(pts);
    } else if (m_polyLassoDrawing) {
        updatePolyLassoOverlay();
    } else if (m_movingSelection) {
        m_selectDragOverlay->setScreenRect(documentRectToScreen(
            m_selectMoveBoundsBefore.translated(m_selectMoveCurrentDelta)));
    }
}

QRect CanvasView::doQuickSelectDab(const QPointF& docPos)
{
    if (m_quickSelectSrcImage.isNull()) return {};

    auto* layer = m_doc->activeLayer();
    if (!layer) return {};

    int docW = m_doc->size.width();
    int docH = m_doc->size.height();
    int sw = m_quickSelectSrcImage.width();
    int sh = m_quickSelectSrcImage.height();
    const uchar* srcBits = m_quickSelectSrcImage.bits();

    // Compute inverse affine (doc-pixel → layer-pixel) if layer is transformed
    double inv00 = 1.0, inv01 = 0.0, inv02 = 0.0;
    double inv10 = 0.0, inv11 = 1.0, inv12 = 0.0;
    const QTransform lxf = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
    bool hasTransform = (lxf.m11() != 1.0 || lxf.m22() != 1.0 ||
                         lxf.m31() != 0.0 || lxf.m32() != 0.0 ||
                         lxf.m12() != 0.0 || lxf.m21() != 0.0);
    if (hasTransform) {
        QTransform invT = lxf.inverted();
        inv00 = sw * invT.m11() / docW;  inv01 = -sw * invT.m21() / docH;
        inv02 = sw * 0.5 * (1.0 - invT.m11() + invT.m21() + invT.m31());
        inv10 = -sh * invT.m12() / docW;  inv11 = sh * invT.m22() / docH;
        inv12 = sh * 0.5 * (1.0 + invT.m12() - invT.m22() - invT.m32());
    }

    // Dab center in doc-pixel coords
    int cx = static_cast<int>(std::round(docPos.x()));
    int cy = static_cast<int>(std::round(docPos.y()));
    int brushR = static_cast<int>(m_brushSettings.size * 0.5f);
    if (brushR < 1) brushR = 1;

    // Seed color: map dab center (doc-pixel) to layer pixel and read
    double seedLx = inv00 * cx + inv01 * cy + inv02;
    double seedLy = inv10 * cx + inv11 * cy + inv12;
    int seedLxI = std::clamp(static_cast<int>(seedLx + 0.5), 0, sw - 1);
    int seedLyI = std::clamp(static_cast<int>(seedLy + 0.5), 0, sh - 1);
    const uchar* sp = srcBits + (seedLyI * sw + seedLxI) * 4;
    int sR = sp[0], sG = sp[1], sB = sp[2], sA = sp[3];

    int mw = m_doc->selection.width();
    int mh = m_doc->selection.height();

    int x1 = std::clamp(cx - brushR, 0, docW - 1);
    int y1 = std::clamp(cy - brushR, 0, docH - 1);
    int x2 = std::clamp(cx + brushR, 0, docW - 1);
    int y2 = std::clamp(cy + brushR, 0, docH - 1);

    const int brushR2 = brushR * brushR;
    const bool intersect = m_selectMode == SelectMode::Intersect
                        && !m_quickSelectOriginal.isNull();

    for (int y = y1; y <= y2; ++y) {
        if (y >= mh) continue;
        const int dy = y - cy;
        uchar* maskRow = m_doc->selection.image().scanLine(y);
        const uchar* origRow = (intersect && y < m_quickSelectOriginal.height())
            ? m_quickSelectOriginal.constScanLine(y) : nullptr;
        for (int x = x1; x <= x2; ++x) {
            if (x >= mw) continue;
            // Restrict the dab footprint to a circle so the painted area matches
            // the circular brush cursor (was the full square bounding box).
            const int dx = x - cx;
            if (dx * dx + dy * dy > brushR2) continue;
            // Map doc-pixel to layer-pixel for color reading
            double lx = inv00 * x + inv01 * y + inv02;
            double ly = inv10 * x + inv11 * y + inv12;
            int lxI = std::clamp(static_cast<int>(lx + 0.5), 0, sw - 1);
            int lyI = std::clamp(static_cast<int>(ly + 0.5), 0, sh - 1);
            const uchar* px = srcBits + (lyI * sw + lxI) * 4;
            if (SelectionMask::colorMatches(px, sR, sG, sB, sA,
                                            m_quickSelectTolerance)) {
                if (m_selectMode == SelectMode::Subtract)
                    maskRow[x] = 0;
                else if (intersect)
                    maskRow[x] = origRow ? origRow[x] : 0;
                else
                    maskRow[x] = 255;
            }
        }
    }

    return QRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}



// ── Bounding Box / Transform ─────────────────────────────────



void CanvasView::bakeTextLayerResolution(LayerTreeNode* node)
{
    if (!node || !node->layer || !node->layer->textData) {
        return;
    }
    auto& td = *node->layer->textData;
    if (td.flowMode != TextFlowMode::Point || !m_doc) {
        return;
    }
    // Bake the layer's effective (WORLD) scale into the font so nested text inside
    // a scaled group re-renders crisp instead of scaling the raster cache. The
    // basis lengths are measured in VISUAL (document-pixel) space — canvas NDC
    // is anisotropic for non-square documents, so NDC lengths of a rotated
    // basis mix the aspect ratio into the "scale": a pure visual rotation would
    // read as scale ≠ 1, trigger a spurious bake, and distort the text (and
    // each later gesture would re-bake and compound it). Measured visually, a
    // rotation leaves the basis lengths unchanged and the bake is a no-op.
    const QTransform worldXf = node->accumulatedTransform();

    const int docW = std::max(1, m_doc->size.width());
    const int docH = std::max(1, m_doc->size.height());

    // Visual extents (in document pixels) the layer currently maps to, vs the
    // un-scaled baseline = the cpuImage's own pixel size.
    const double effW = std::hypot(worldXf.m11() * docW, worldXf.m12() * docH);
    const double effH = std::hypot(worldXf.m21() * docW, worldXf.m22() * docH);
    const double imgW = node->layer->cpuImage.width();
    const double imgH = node->layer->cpuImage.height();

    if (imgW < 1.0 || imgH < 1.0) {
        return;
    }

    const double scaleX = effW / imgW;
    const double scaleY = effH / imgH;
    const double scale = std::sqrt(scaleX * scaleY);
    if (std::abs(scale - 1.0) < 1e-4) {
        return;
    }

    for (int i = 0; i < (int)td.spans.size(); ++i) {
        td.spans[i].fontSize = std::max(1.0f, static_cast<float>(td.spans[i].fontSize * scale));
    }
    td.dirty = true;

    TextRenderer renderer;
    renderer.render(td, node->layer->cpuImage);

    // If layer has a mask, scale it to match the new cpuImage size
    auto* layer = node->layer.get();
    if (!layer->maskImage.isNull() && layer->maskImage.size() != layer->cpuImage.size()) {
        makeCurrent();
        if (layer->maskTextureId)
            m_controller->syncLayerMaskFromGpu(layer);
        doneCurrent();
        QImage newMask(layer->cpuImage.size(), QImage::Format_Grayscale8);
        newMask.fill(255);
        QPainter mp(&newMask);
        mp.setRenderHint(QPainter::SmoothPixmapTransform);
        mp.drawImage(QRect(QPoint(0,0), layer->cpuImage.size()), layer->maskImage);
        mp.end();
        layer->maskImage = newMask;
        layer->maskTextureOutdated = true;
    }

    fitTextLayerTransformToImage(node);

    node->layer->textureOutdated = true;
    node->invalidateEffects();
    // Refresh the display immediately (same as rerenderTextLayer): bump the
    // composition generation so the projection cache rebuilds and repaint.
    if (m_doc) ++m_doc->compositionGeneration;
    makeCurrent();
    syncLayersToGpu();
    doneCurrent();
    update();
}

bool CanvasView::bakeGroupVectorChildrenToComposite(
    LayerTreeNode* groupNode,
    const QString& cmdName,
    CompositeCommand& composite)
{
    // TODO - review
    if (!groupNode || groupNode->type != LayerTreeNode::Type::Group || !m_doc)
        return false;

    std::vector<int> textIdx;
    std::vector<TextLayerData> tdBefore;
    std::vector<QTransform> txfBefore;
    std::vector<int> shapeIdx;
    std::vector<ShapeData> shapeBefore;
    std::vector<QImage> shapeImgBefore;
    std::vector<QTransform> shapeXfBefore;

    const auto flat = m_doc->flatten();
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        auto* n = flat[i];
        if (!n || !n->layer) continue;
        bool inGroup = false;
        for (auto* p = n->parent; p; p = p->parent)
            if (p == groupNode) { inGroup = true; break; }
        if (!inGroup) continue;
        if (n->layer->textData
            && n->layer->textData->flowMode == TextFlowMode::Point) {
            textIdx.push_back(i);
            tdBefore.push_back(*n->layer->textData);
            txfBefore.push_back(n->transform());
        } else if (n->layer->shapeData) {
            shapeIdx.push_back(i);
            shapeBefore.push_back(*n->layer->shapeData);
            shapeImgBefore.push_back(n->layer->cpuImage.copy());
            shapeXfBefore.push_back(n->transform());
        }
    }

    if (textIdx.empty() && shapeIdx.empty())
        return false;

    for (int idx : textIdx) {
        if (auto* tn = m_doc->nodeAt(idx))
            bakeTextLayerResolution(tn);
    }
    std::vector<bool> shapeBaked(shapeIdx.size(), false);
    for (size_t i = 0; i < shapeIdx.size(); ++i)
        shapeBaked[i] =
            m_controller->bakeShapeLayerResolutionInPlace(shapeIdx[i]);

    for (size_t i = 0; i < textIdx.size(); ++i) {
        auto* tn = m_doc->nodeAt(textIdx[i]);
        if (!tn || !tn->layer || !tn->layer->textData) continue;
        composite.add(std::make_unique<TextEditCommand>(
            m_doc, textIdx[i], tdBefore[i], *tn->layer->textData,
            txfBefore[i], tn->transform(), cmdName));
    }
    for (size_t i = 0; i < shapeIdx.size(); ++i) {
        if (!shapeBaked[i]) continue;
        auto* sn = m_doc->nodeAt(shapeIdx[i]);
        if (!sn || !sn->layer || !sn->layer->shapeData) continue;
        composite.add(std::make_unique<ModifyShapeCommand>(
            m_doc, shapeIdx[i], shapeBefore[i], *sn->layer->shapeData,
            shapeImgBefore[i], sn->layer->cpuImage,
            shapeXfBefore[i], sn->transform(), cmdName));
    }

    // The bake mutated each child's content in place (text fontSize re-rendered,
    // shape geometry re-rasterised). The active node is the GROUP, so the
    // controller's commitTextEdit/bakeShapeTransform paths — which would have
    // emitted layerChanged for a directly-selected child — never ran. Notify the
    // UI per baked child so the options bar (text font size, etc.) reflects the
    // recalculated values, matching the directly-selected-layer behaviour.
    if (m_controller) {
        for (int idx : textIdx)
            emit m_controller->layerChanged(idx);
        for (size_t i = 0; i < shapeIdx.size(); ++i)
            if (shapeBaked[i])
                emit m_controller->layerChanged(shapeIdx[i]);
    }
    return true;
}

bool CanvasView::bakeVectorNodeToComposite(
    LayerTreeNode* node, int flatIndex,
    const QTransform& beforeTransform,
    const QString& cmdName,
    CompositeCommand& composite)
{
    if (!node || !node->layer || !m_doc || !m_controller)
        return false;

    // Point text: re-render at the new world scale (fontSize + cpuImage +
    // transform mutate together) and store a TextEditCommand restoring all three.
    if (node->layer->textData
        && node->layer->textData->flowMode == TextFlowMode::Point) {
        const TextLayerData tdBefore = *node->layer->textData;
        bakeTextLayerResolution(node);
        composite.add(std::make_unique<TextEditCommand>(
            m_doc, flatIndex, tdBefore, *node->layer->textData,
            beforeTransform, node->transform(), cmdName));
        emit m_controller->layerChanged(flatIndex);
        return true;
    }

    // Shape: re-rasterise at the new world scale, storing a ModifyShapeCommand.
    if (node->layer->shapeData) {
        const ShapeData shapeBefore = *node->layer->shapeData;
        const QImage imgBefore = node->layer->cpuImage.copy();
        if (m_controller->bakeShapeLayerResolutionInPlace(flatIndex)) {
            composite.add(std::make_unique<ModifyShapeCommand>(
                m_doc, flatIndex, shapeBefore, *node->layer->shapeData,
                imgBefore, node->layer->cpuImage,
                beforeTransform, node->transform(), cmdName));
            emit m_controller->layerChanged(flatIndex);
            return true;
        }
    }

    return false;
}

QPointF CanvasView::screenToCanvasNdc(QPointF screenPos) const
{
    float ndcX = 2.0f * static_cast<float>(screenPos.x()) / width() - 1.0f;
    float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / height();
    float invZoom = 1.0f / m_doc->zoom;
    float docNdcX = (ndcX - static_cast<float>(m_doc->panOffset.x())) * invZoom;
    float docNdcY = (ndcY - static_cast<float>(m_doc->panOffset.y())) * invZoom;
    float invHx = 1.0f / m_canvasHalfExtents.x();
    float invHy = 1.0f / m_canvasHalfExtents.y();
    return QPointF(docNdcX * invHx, docNdcY * invHy);
}

QPointF CanvasView::documentToScreen(QPointF docPos) const
{
    if (!m_doc || m_doc->size.width() <= 0 || m_doc->size.height() <= 0 || width() <= 0 || height() <= 0)
        return {};

    float canvasX = static_cast<float>(docPos.x()) / m_doc->size.width() * 2.0f - 1.0f;
    float canvasY = 1.0f - static_cast<float>(docPos.y()) / m_doc->size.height() * 2.0f;
    float ndcX = static_cast<float>(m_doc->panOffset.x())
               + m_doc->zoom * static_cast<float>(m_canvasHalfExtents.x()) * canvasX;
    float ndcY = static_cast<float>(m_doc->panOffset.y())
               + m_doc->zoom * static_cast<float>(m_canvasHalfExtents.y()) * canvasY;
    return QPointF((ndcX + 1.0f) * 0.5f * width(),
                   (1.0f - ndcY) * 0.5f * height());
}

QRectF CanvasView::documentRectToScreen(const QRectF& docRect) const
{
    QPointF topLeft = documentToScreen(docRect.topLeft());
    QPointF bottomRight = documentToScreen(docRect.bottomRight());
    return QRectF(topLeft, bottomRight).normalized();
}

int CanvasView::pickLayerAtScreenPos(QPointF screenPos, bool tightToContent)
{
    if (!m_doc || m_doc->flatCount() == 0) return -1;

    QPointF canvasPos = screenToCanvasNdc(screenPos);

    auto flat = m_doc->flatten();
    // Index 0 = topmost (inserted at roots.begin()), iterate forward
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        auto* node = flat[i];
        if (!node->isVisible()) continue;
        if (node->type != LayerTreeNode::Type::Layer) continue;
        if (!node->layer) continue;
        if (node->isPositionLocked()) continue;

        if (node->layer->shapeData) {
            const ShapeData& shape = *node->layer->shapeData;
            const QRectF baseBounds = ShapeRenderer::rasterBounds(shape);
            if (baseBounds.isEmpty())
                continue;

            QTransform baseFrame;
            baseFrame.setMatrix(baseBounds.width() * 0.5, 0.0, 0.0,
                                0.0, baseBounds.height() * 0.5, 0.0,
                                baseBounds.center().x(), baseBounds.center().y(), 1.0);
            bool baseInvertible = false;
            const QTransform visualDelta = baseFrame.inverted(&baseInvertible)
                * node->accumulatedTransform();
            if (!baseInvertible)
                continue;

            const QPainterPath path = visualDelta.map(ShapeRenderer::pathForShape(shape));
            bool hit = false;
            if (shape.path.closed)
                hit = path.contains(canvasPos);

            if (!hit && shape.style.strokeEnabled && shape.style.strokeWidth > 0.0) {
                QPainterPathStroker stroker;
                stroker.setWidth(shape.style.strokeWidth);
                stroker.setJoinStyle(Qt::RoundJoin);
                stroker.setCapStyle(shape.path.closed ? Qt::SquareCap : Qt::RoundCap);
                hit = stroker.createStroke(path).contains(canvasPos);
            }

            if (!hit)
                continue;
            return i;
        }

        QTransform inv = node->accumulatedTransform().inverted();
        QPointF localPos = inv.map(canvasPos);
        if (localPos.x() < -1.0 || localPos.x() > 1.0 ||
            localPos.y() < -1.0 || localPos.y() > 1.0)
            continue;

        // By default a click anywhere inside the full layer image rect counts.
        // For auto-select we instead restrict the hit to the painted (dab)
        // bounds so a small stroke on a canvas-sized layer is picked by its
        // actual content — matching the transform outline rather than the
        // transparent padding out to the canvas edges.
        if (tightToContent) {
            const QSize baseSize = node->layer->rasterBaseSize();
            const QRect content = cachedLayerVisiblePixelBounds(node->layer.get());
            if (!content.isEmpty() && !baseSize.isEmpty()) {
                const double px = (localPos.x() + 1.0) * 0.5 * baseSize.width();
                const double py = (1.0 - localPos.y()) * 0.5 * baseSize.height();
                if (!QRectF(content).contains(px, py))
                    continue;
            }
        }

        return i;
    }
    return -1;
}

void CanvasView::setAutoSelect(bool enabled)
{
    m_autoSelect = enabled;
}

void CanvasView::setAutoSelectGroup(bool group)
{
    m_autoSelectGroup = group;
}

void CanvasView::setShowTransformControls(bool show)
{
    m_showTransformControls = show;
    update();
}

void CanvasView::reloadRulerGuideSettings()
{
    if (m_rulerGuideOverlay)
        m_rulerGuideOverlay->reloadSettings();
    updateRulerGuideOverlay();
    update();
}

void CanvasView::updateRulerGuideOverlay()
{
    if (!m_rulerGuideOverlay)
        return;

    const bool overlayIsCanvasChild = (m_rulerGuideOverlay->parentWidget() == this);
    QWidget* overlayParent = m_rulerGuideOverlay->parentWidget();
    if (!overlayIsCanvasChild && overlayParent) {
        const auto& settings = m_rulerGuideOverlay->settings();
        const int rulerSize = settings.rulers.showRulers
            ? static_cast<int>(std::round(m_rulerGuideOverlay->effectiveRulerSize()))
            : 0;
        if (auto* layout = overlayParent->layout()) {
            QMargins margins = layout->contentsMargins();
            if (margins.left() != rulerSize || margins.top() != rulerSize
                || margins.right() != 0 || margins.bottom() != 0) {
                layout->setContentsMargins(rulerSize, rulerSize, 0, 0);
                layout->activate();
            }
        }
        m_rulerGuideOverlay->setGeometry(overlayParent->rect());
        m_rulerGuideOverlay->setViewportRect(geometry());
    } else {
        m_rulerGuideOverlay->setGeometry(rect());
        m_rulerGuideOverlay->setViewportRect(rect());
    }
    m_rulerGuideOverlay->setDocument(m_doc);
    m_rulerGuideOverlay->setCanvasHalfExtents(m_canvasHalfExtents);
    m_rulerGuideOverlay->setVisible(isVisible());
    m_rulerGuideOverlay->raise();
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][overlay-layout]"
            << "canvasSize" << size()
            << "canvasGeometryInParent" << geometry()
            << "overlayGeometry" << m_rulerGuideOverlay->geometry()
            << "overlayParent" << overlayParent
            << "overlayIsCanvasChild" << overlayIsCanvasChild
            << "overlayVisible" << m_rulerGuideOverlay->isVisible()
            << "canvasVisible" << isVisible()
            << "halfExtents" << m_canvasHalfExtents
            << "zoom" << (m_doc ? m_doc->zoom : 0.0f)
            << "pan" << (m_doc ? m_doc->panOffset : QPointF());
    }
    if (m_eyedropperOverlay)
        m_eyedropperOverlay->raise();
    if (m_transformOverlay)
        m_transformOverlay->raise();
}

QPointF CanvasView::canvasToRulerOverlayPoint(QPointF canvasPos) const
{
    if (!m_rulerGuideOverlay || m_rulerGuideOverlay->parentWidget() == this)
        return canvasPos;

    QWidget* overlayParent = m_rulerGuideOverlay->parentWidget();
    if (!overlayParent)
        return canvasPos;

    const QPoint mapped = mapTo(overlayParent, canvasPos.toPoint());
    return QPointF(mapped);
}

bool CanvasView::beginGuideInteraction(QPointF screenPos)
{
    if (!m_doc || !m_controller || !m_rulerGuideOverlay)
        return false;

    const auto& settings = m_rulerGuideOverlay->settings();
    if (settings.rulers.showRulers && m_rulerGuideOverlay->hitHorizontalRuler(screenPos)) {
        QPointF doc = m_rulerGuideOverlay->screenToDocument(screenPos);
        m_guideDragging = true;
        m_guideCreating = true;
        m_guideDragIndex = -1;
        m_guideDragOrientation = GuideOrientation::Horizontal;
        m_guideDragCurrentPosition = std::clamp<qreal>(doc.y(), 0.0, m_doc->size.height());
        m_rulerGuideOverlay->setGuidePreview(m_guideDragOrientation, m_guideDragCurrentPosition, true);
        setCursor(Qt::SplitVCursor);
        return true;
    }

    if (settings.rulers.showRulers && m_rulerGuideOverlay->hitVerticalRuler(screenPos)) {
        QPointF doc = m_rulerGuideOverlay->screenToDocument(screenPos);
        m_guideDragging = true;
        m_guideCreating = true;
        m_guideDragIndex = -1;
        m_guideDragOrientation = GuideOrientation::Vertical;
        m_guideDragCurrentPosition = std::clamp<qreal>(doc.x(), 0.0, m_doc->size.width());
        m_rulerGuideOverlay->setGuidePreview(m_guideDragOrientation, m_guideDragCurrentPosition, true);
        setCursor(Qt::SplitHCursor);
        return true;
    }

    if (settings.guides.showGuides) {
        const int guideIndex = m_rulerGuideOverlay->hitGuide(screenPos);
        if (guideIndex >= 0) {
            const Guide guide = m_doc->guideManager.guideAt(guideIndex);
            m_rulerGuideOverlay->setSelectedGuideIndex(guideIndex);
            if (settings.guides.lockGuides)
                return true;

            m_guideDragging = true;
            m_guideCreating = false;
            m_guideDragIndex = guideIndex;
            m_guideDragOrientation = guide.orientation;
            m_guideDragStartPosition = guide.position;
            m_guideDragCurrentPosition = guide.position;
            m_rulerGuideOverlay->setGuidePreview(guide.orientation, guide.position, true);
            setCursor(guide.orientation == GuideOrientation::Vertical
                ? Qt::SplitHCursor : Qt::SplitVCursor);
            return true;
        }
    }

    return false;
}

void CanvasView::updateGuideInteraction(QPointF screenPos)
{
    if (!m_doc || !m_rulerGuideOverlay || !m_guideDragging)
        return;

    const QPointF doc = m_rulerGuideOverlay->screenToDocument(screenPos);
    if (m_guideDragOrientation == GuideOrientation::Vertical)
        m_guideDragCurrentPosition = std::clamp<qreal>(doc.x(), 0.0, m_doc->size.width());
    else
        m_guideDragCurrentPosition = std::clamp<qreal>(doc.y(), 0.0, m_doc->size.height());

    if (!m_guideCreating && m_guideDragIndex >= 0)
        m_doc->guideManager.moveGuide(m_guideDragIndex, m_guideDragCurrentPosition);

    m_rulerGuideOverlay->setGuidePreview(m_guideDragOrientation, m_guideDragCurrentPosition, true);
    update();
}

void CanvasView::finishGuideInteraction(QPointF screenPos)
{
    if (!m_doc || !m_controller || !m_rulerGuideOverlay || !m_guideDragging)
        return;

    const bool releasedOnCanvas = m_rulerGuideOverlay->isPointInCanvas(screenPos);

    if (m_guideCreating) {
        if (releasedOnCanvas)
            m_controller->addGuide(m_guideDragOrientation, m_guideDragCurrentPosition);
    } else if (m_guideDragIndex >= 0) {
        if (!releasedOnCanvas) {
            m_doc->guideManager.moveGuide(m_guideDragIndex, m_guideDragStartPosition);
            m_controller->removeGuide(m_guideDragIndex);
        } else {
            m_controller->moveGuide(m_guideDragIndex,
                                    m_guideDragCurrentPosition,
                                    m_guideDragStartPosition);
        }
    }

    m_guideDragging = false;
    m_guideCreating = false;
    m_guideDragIndex = -1;
    m_rulerGuideOverlay->clearGuidePreview();
    m_rulerGuideOverlay->setSelectedGuideIndex(-1);
    updateToolCursor();
    update();
}

bool CanvasView::updateGuideHover(QPointF screenPos)
{
    if (!m_doc || !m_rulerGuideOverlay || m_guideDragging)
        return false;

    const int guideIndex = m_rulerGuideOverlay->hitGuide(screenPos);
    m_rulerGuideOverlay->setSelectedGuideIndex(guideIndex);
    if (guideIndex < 0)
        return false;

    if (m_rulerGuideOverlay->settings().guides.lockGuides) {
        setCursor(Qt::ArrowCursor);
        return true;
    }

    const Guide guide = m_doc->guideManager.guideAt(guideIndex);
    setCursor(guide.orientation == GuideOrientation::Vertical
        ? Qt::SplitHCursor : Qt::SplitVCursor);
    return true;
}

QRect CanvasView::cachedLayerVisiblePixelBounds(const Layer* layer) const
{
    if (!layer)
        return {};

    const auto cached = m_snapVisiblePixelBoundsCache.find(layer);
    if (cached != m_snapVisiblePixelBoundsCache.end())
        return cached->second;

    if (layer->usesRasterStorage()) {
        QRect result = layer->renderRasterStorage().contentBounds();
        if (result.isEmpty())
            result = layer->renderRasterStorage().logicalBounds();
        m_snapVisiblePixelBoundsCache.emplace(layer, result);
        if (snapDebugEnabled()) {
            qInfo().noquote()
                << "[Snap][content-bounds]"
                << "layer" << layer
                << "rasterStorage" << true
                << "baseSize" << layer->rasterBaseSize()
                << "visiblePixels" << result;
        }
        return result;
    }

    if (layer->cpuImage.isNull())
        return {};

    const QImage& source = layer->cpuImage;
    const int width = source.width();
    const int height = source.height();
    QRect result;

    // Exact alpha scans happen once per drag gesture. Very large images fall
    // back to full bounds to avoid introducing stutter during interactive move.
    constexpr qint64 kMaxExactAlphaScanPixels = 16000000;
    const qint64 pixelCount = static_cast<qint64>(width) * static_cast<qint64>(height);
    if (width > 0 && height > 0 && pixelCount <= kMaxExactAlphaScanPixels) {
        QImage converted;
        const QImage* image = &source;
        const QImage::Format format = source.format();
        if (format != QImage::Format_ARGB32
            && format != QImage::Format_ARGB32_Premultiplied
            && format != QImage::Format_RGB32) {
            converted = source.convertToFormat(QImage::Format_ARGB32);
            image = &converted;
        }

        int minX = width;
        int minY = height;
        int maxX = -1;
        int maxY = -1;

        for (int y = 0; y < height; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(image->constScanLine(y));
            for (int x = 0; x < width; ++x) {
                if (qAlpha(row[x]) == 0)
                    continue;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }

        if (maxX >= minX && maxY >= minY)
            result = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    } else if (width > 0 && height > 0) {
        result = QRect(0, 0, width, height);
    }

    m_snapVisiblePixelBoundsCache.emplace(layer, result);
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][content-bounds]"
            << "layer" << layer
            << "imageSize" << source.size()
            << "format" << static_cast<int>(source.format())
            << "visiblePixels" << result
            << "usedFullFallback" << (result == QRect(0, 0, width, height));
    }
    return result;
}

QPolygonF CanvasView::layerCanvasSnapCorners(const LayerTreeNode* node) const
{
    if (!node || !node->layer)
        return {};

    QPolygonF localCorners;
    const QRect visiblePixels = cachedLayerVisiblePixelBounds(node->layer.get());
    const QSize baseSize = node->layer->rasterBaseSize();

    if (!visiblePixels.isEmpty() && baseSize.width() > 0 && baseSize.height() > 0) {
        const qreal imageW = static_cast<qreal>(baseSize.width());
        const qreal imageH = static_cast<qreal>(baseSize.height());
        const qreal left = (static_cast<qreal>(visiblePixels.left()) / imageW) * 2.0 - 1.0;
        const qreal right = (static_cast<qreal>(visiblePixels.right() + 1) / imageW) * 2.0 - 1.0;
        const qreal top = 1.0 - (static_cast<qreal>(visiblePixels.top()) / imageH) * 2.0;
        const qreal bottom = 1.0 - (static_cast<qreal>(visiblePixels.bottom() + 1) / imageH) * 2.0;

        localCorners << QPointF(left, bottom)
                     << QPointF(right, bottom)
                     << QPointF(right, top)
                     << QPointF(left, top);
    } else {
        localCorners << QPointF(-1.0, -1.0)
                     << QPointF(1.0, -1.0)
                     << QPointF(1.0, 1.0)
                     << QPointF(-1.0, 1.0);
    }

    const QTransform transform = node->accumulatedTransform();
    QPolygonF corners;
    for (const QPointF& corner : localCorners)
        corners << transform.map(corner);
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][layer-corners]"
            << "node" << node
            << "name" << (node ? node->name : QString())
            << "visiblePixels" << visiblePixels
            << "localCorners" << localCorners
            << "canvasCorners" << corners;
    }
    return corners;
}

QRectF CanvasView::nodeDocumentBounds(const LayerTreeNode* node) const
{
    if (!m_doc || !node || m_doc->size.isEmpty())
        return {};

    auto ndcToDocument = [this](const QPointF& ndc) {
        return QPointF((ndc.x() + 1.0) * 0.5 * m_doc->size.width(),
                       (1.0 - ndc.y()) * 0.5 * m_doc->size.height());
    };

    qreal minX = std::numeric_limits<qreal>::max();
    qreal minY = std::numeric_limits<qreal>::max();
    qreal maxX = std::numeric_limits<qreal>::lowest();
    qreal maxY = std::numeric_limits<qreal>::lowest();
    bool hasBounds = false;
    auto includePoint = [&](const QPointF& p) {
        const QPointF doc = ndcToDocument(p);
        minX = std::min(minX, doc.x());
        minY = std::min(minY, doc.y());
        maxX = std::max(maxX, doc.x());
        maxY = std::max(maxY, doc.y());
        hasBounds = true;
    };

    std::function<void(const LayerTreeNode*)> visit = [&](const LayerTreeNode* n) {
        if (!n || !n->isVisible())
            return;
        if (n->type == LayerTreeNode::Type::Layer && n->layer) {
            const QPolygonF corners = layerCanvasSnapCorners(n);
            for (const QPointF& corner : corners)
                includePoint(corner);
            return;
        }
        for (const auto& child : n->children)
            visit(child.get());
    };

    visit(node);
    if (!hasBounds)
        return {};
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

QRectF CanvasView::currentTransformDocumentBounds() const
{
    if (!m_doc)
        return {};

    QRectF bounds;
    bool hasBounds = false;
    auto includeNode = [&](LayerTreeNode* node) {
        const QRectF nodeBounds = nodeDocumentBounds(node);
        if (nodeBounds.isNull() && nodeBounds.isEmpty())
            return;
        bounds = hasBounds ? bounds.united(nodeBounds) : nodeBounds;
        hasBounds = true;
    };

    if (m_transformState.mode == InteractionMode::Moving && m_multiMoveIndices.size() > 1) {
        for (int idx : m_multiMoveIndices)
            includeNode(m_doc->nodeAt(idx));
    } else if ((m_transformState.mode == InteractionMode::Resizing
                || m_transformState.mode == InteractionMode::Rotating)
               && m_multiResizeIndices.size() > 1) {
        for (int idx : m_multiResizeIndices)
            includeNode(m_doc->nodeAt(idx));
    } else {
        includeNode(m_doc->activeNode());
    }

    return bounds.normalized();
}

QPointF CanvasView::documentDeltaFromScreenDelta(QPointF screenDelta) const
{
    if (!m_doc || m_doc->size.isEmpty() || width() <= 0 || height() <= 0)
        return {};

    const qreal zoom = std::max<qreal>(m_doc->zoom, 0.0001);
    const qreal halfX = std::max<qreal>(std::abs(m_canvasHalfExtents.x()), 0.0001);
    const qreal halfY = std::max<qreal>(std::abs(m_canvasHalfExtents.y()), 0.0001);
    const qreal canvasDx = (2.0 * screenDelta.x() / width()) / zoom / halfX;
    const qreal canvasDy = (-2.0 * screenDelta.y() / height()) / zoom / halfY;

    return QPointF(canvasDx * m_doc->size.width() * 0.5,
                   -canvasDy * m_doc->size.height() * 0.5);
}

QTransform CanvasView::transformWithDocumentDelta(const LayerTreeNode* node,
                                                  const QTransform& startTransform,
                                                  QPointF deltaDoc) const
{
    if (!m_doc || !node || m_doc->size.isEmpty())
        return startTransform;

    const qreal canvasDx = 2.0 * deltaDoc.x() / m_doc->size.width();
    const qreal canvasDy = -2.0 * deltaDoc.y() / m_doc->size.height();

    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    const QTransform invParent = parentAccum.inverted();
    const qreal localDx = invParent.m11() * canvasDx + invParent.m21() * canvasDy;
    const qreal localDy = invParent.m12() * canvasDx + invParent.m22() * canvasDy;

    QTransform t = startTransform;
    t.setMatrix(t.m11(), t.m12(), t.m13(),
                t.m21(), t.m22(), t.m23(),
                t.m31() + localDx, t.m32() + localDy, t.m33());
    return t;
}

SnapContext CanvasView::currentSnapContext(const RulerGuideSettings& settings) const
{
    SnapContext context;
    if (!m_doc)
        return context;

    const QPointF origin = documentToScreen(QPointF(0.0, 0.0));
    const QPointF oneX = documentToScreen(QPointF(1.0, 0.0));
    const QPointF oneY = documentToScreen(QPointF(0.0, 1.0));

    context.documentSize = QSizeF(m_doc->size);
    context.guides = m_doc->guideManager.guides();
    context.snapSettings = settings.snap;
    context.guideSettings = settings.guides;
    context.screenPixelsPerDocumentX = std::max<qreal>(0.0001, std::abs(oneX.x() - origin.x()));
    context.screenPixelsPerDocumentY = std::max<qreal>(0.0001, std::abs(oneY.y() - origin.y()));
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][context]"
            << "documentSize" << context.documentSize
            << "originScreen" << origin
            << "oneXScreen" << oneX
            << "oneYScreen" << oneY
            << "pxPerDocX" << context.screenPixelsPerDocumentX
            << "pxPerDocY" << context.screenPixelsPerDocumentY
            << "enabled" << context.snapSettings.enabled
            << "canvasBounds" << context.snapSettings.snapToCanvasBounds
            << "canvasCenter" << context.snapSettings.snapToCanvasCenter
            << "guides" << context.snapSettings.snapToGuides
            << "guideCount" << context.guides.size()
            << "showGuides" << context.guideSettings.showGuides
            << "snapToGuides" << context.guideSettings.snapToGuides;
        for (const Guide& guide : context.guides) {
            qInfo().noquote()
                << "[Snap][context-guide]"
                << (guide.orientation == GuideOrientation::Vertical ? "vertical" : "horizontal")
                << "position" << guide.position
                << "id" << guide.id;
        }
    }
    return context;
}

void CanvasView::prepareSnapMoveBounds()
{
    updateCanvasRect();
    clearSnapBoundsCache();
    m_snapMoveIndices.clear();
    m_snapMoveStartTransforms.clear();
    m_snapMoveStartDocumentBounds = currentTransformDocumentBounds();
    m_snapMoveStartDocumentBoundsValid = !m_snapMoveStartDocumentBounds.isEmpty();

    if (m_multiMoveIndices.empty()) {
        if (m_doc && m_doc->activeFlatIndex >= 0) {
            if (auto* node = m_doc->activeNode()) {
                m_snapMoveIndices.push_back(m_doc->activeFlatIndex);
                m_snapMoveStartTransforms.push_back(node->transform());
            }
        }
    } else {
        const size_t count = std::min(m_multiMoveIndices.size(), m_multiMoveStartTransforms.size());
        for (size_t i = 0; i < count; ++i) {
            m_snapMoveIndices.push_back(m_multiMoveIndices[i]);
            m_snapMoveStartTransforms.push_back(m_multiMoveStartTransforms[i]);
        }
    }

    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][prepare-move]"
            << "valid" << m_snapMoveStartDocumentBoundsValid
            << "startBounds" << m_snapMoveStartDocumentBounds
            << "activeFlatIndex" << (m_doc ? m_doc->activeFlatIndex : -1)
            << "snapMoveCount" << m_snapMoveIndices.size()
            << "multiMoveCount" << m_multiMoveIndices.size()
            << "docSize" << (m_doc ? m_doc->size : QSize())
            << "canvasSize" << size()
            << "halfExtents" << m_canvasHalfExtents
            << "zoom" << (m_doc ? m_doc->zoom : 0.0f)
            << "pan" << (m_doc ? m_doc->panOffset : QPointF());
        for (size_t i = 0; i < m_snapMoveIndices.size(); ++i) {
            const QTransform& t = m_snapMoveStartTransforms[i];
            qInfo().noquote()
                << "[Snap][prepare-node]"
                << "idx" << m_snapMoveIndices[i]
                << "startTransform"
                << t.m11() << t.m12() << t.m21() << t.m22() << t.m31() << t.m32();
        }
    }
}

bool CanvasView::applySnapMoveFromStart(QPointF currentMouseScreen,
                                        Qt::KeyboardModifiers modifiers)
{
    if (!m_doc) {
        if (snapDebugEnabled())
            qInfo().noquote() << "[Snap][move] skipped: no document";
        clearSnapFeedback();
        return false;
    }

    updateCanvasRect();

    const QPointF proposedDeltaDoc =
        documentDeltaFromScreenDelta(currentMouseScreen - m_transformState.startMouseScreen);

    QPointF adjustedDeltaDoc = proposedDeltaDoc;
    SnapResult result;

    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][move]"
            << "mouseStart" << m_transformState.startMouseScreen
            << "mouseCurrent" << currentMouseScreen
            << "screenDelta" << (currentMouseScreen - m_transformState.startMouseScreen)
            << "proposedDeltaDoc" << proposedDeltaDoc
            << "boundsValidBefore" << m_snapMoveStartDocumentBoundsValid
            << "startBounds" << m_snapMoveStartDocumentBounds
            << "alt" << ((modifiers & Qt::AltModifier) != 0);
    }

    if (!m_snapMoveStartDocumentBoundsValid)
        prepareSnapMoveBounds();

    if (m_rulerGuideOverlay && m_snapMoveStartDocumentBoundsValid) {
        const auto& settings = m_rulerGuideOverlay->settings();
        if (snapDebugEnabled()) {
            qInfo().noquote()
                << "[Snap][move-settings]"
                << "snapEnabled" << settings.snap.enabled
                << "canvasBounds" << settings.snap.snapToCanvasBounds
                << "canvasCenter" << settings.snap.snapToCanvasCenter
                << "snapGuides" << settings.snap.snapToGuides
                << "tolerance" << settings.snap.toleranceScreenPx
                << "showIndicators" << settings.snap.showIndicators
                << "guideSnap" << settings.guides.snapToGuides
                << "showGuides" << settings.guides.showGuides;
        }
        if (!(modifiers & Qt::AltModifier) && settings.snap.enabled) {
            result = SnapEngine::snapMove(m_snapMoveStartDocumentBounds,
                                          proposedDeltaDoc,
                                          currentSnapContext(settings));
            adjustedDeltaDoc = result.adjustedDelta;
        }
    } else if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][move] no snap engine call"
            << "overlay" << m_rulerGuideOverlay
            << "boundsValid" << m_snapMoveStartDocumentBoundsValid;
    }

    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[Snap][move-result]"
            << "snapped" << result.snapped()
            << "snappedX" << result.snappedX
            << "snappedY" << result.snappedY
            << "snapOffset" << result.snapOffset
            << "adjustedDeltaDoc" << adjustedDeltaDoc
            << "indicatorCount" << result.indicators.size();
    }

    QSet<int> appliedIndices;
    auto applyIndex = [&](int idx, const QTransform& startTransform) {
        if (appliedIndices.contains(idx))
            return;
        appliedIndices.insert(idx);
        auto* node = m_doc->nodeAt(idx);
        if (!node)
            return;
        const QTransform before = node->transform();
        const QTransform after = transformWithDocumentDelta(node, startTransform, adjustedDeltaDoc);
        m_controller->previewNodeTransform(node, after);
        if (snapDebugEnabled()) {
            qInfo().noquote()
                << "[Snap][apply-node]"
                << "idx" << idx
                << "beforeTx" << before.m31()
                << "beforeTy" << before.m32()
                << "startTx" << startTransform.m31()
                << "startTy" << startTransform.m32()
                << "afterTx" << after.m31()
                << "afterTy" << after.m32();
        }
    };

    const size_t moveCount = std::min(m_snapMoveIndices.size(), m_snapMoveStartTransforms.size());
    for (size_t i = 0; i < moveCount; ++i)
        applyIndex(m_snapMoveIndices[i], m_snapMoveStartTransforms[i]);

    if (moveCount == 0 && m_doc->activeFlatIndex >= 0)
        applyIndex(m_doc->activeFlatIndex, m_transformState.startTransform);

    if (m_multiResizeGroupBboxValid && multiOutlineMatchesSelection()
        && m_doc->size.width() > 0 && m_doc->size.height() > 0) {
        QPointF metric(std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                       std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
        const qreal canvasDx = 2.0 * adjustedDeltaDoc.x() / m_doc->size.width();
        const qreal canvasDy = -2.0 * adjustedDeltaDoc.y() / m_doc->size.height();
        m_multiResizeGroupBboxCenter = QPointF(
            (m_multiResizeGroupStartCenterVis.x() / metric.x()) + canvasDx,
            (m_multiResizeGroupStartCenterVis.y() / metric.y()) + canvasDy);
    }

    if (result.snapped() && m_rulerGuideOverlay)
        m_rulerGuideOverlay->setSnapIndicators(result.indicators);
    else {
        if (snapDebugEnabled())
            qInfo().noquote() << "[Snap][move-result] clearing visual feedback";
        clearSnapFeedback();
    }

    if (m_freeTransformActive)
        m_freeTransformDirty = true;

    return result.snapped();
}

void CanvasView::translateNodeByDocumentDelta(LayerTreeNode* node, QPointF deltaDoc)
{
    if (!m_doc || !node || m_doc->size.isEmpty())
        return;
    if (std::abs(deltaDoc.x()) < 0.001 && std::abs(deltaDoc.y()) < 0.001)
        return;

    const qreal canvasDx = 2.0 * deltaDoc.x() / m_doc->size.width();
    const qreal canvasDy = -2.0 * deltaDoc.y() / m_doc->size.height();

    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    const QTransform invParent = parentAccum.inverted();
    const qreal localDx = invParent.m11() * canvasDx + invParent.m21() * canvasDy;
    const qreal localDy = invParent.m12() * canvasDx + invParent.m22() * canvasDy;

    QTransform t = node->transform();
    t.setMatrix(t.m11(), t.m12(), t.m13(),
                t.m21(), t.m22(), t.m23(),
                t.m31() + localDx, t.m32() + localDy, t.m33());
    m_controller->previewNodeTransform(node, t);
}

void CanvasView::applySnapForCurrentTransform(Qt::KeyboardModifiers modifiers)
{
    if (!m_doc || !m_rulerGuideOverlay || (modifiers & Qt::AltModifier)) {
        clearSnapFeedback();
        return;
    }

    if (m_transformState.mode != InteractionMode::Moving
        && m_transformState.mode != InteractionMode::Resizing) {
        clearSnapFeedback();
        return;
    }

    const auto& settings = m_rulerGuideOverlay->settings();
    if (!settings.snap.enabled) {
        clearSnapFeedback();
        return;
    }

    const QRectF bounds = currentTransformDocumentBounds();
    if (bounds.isEmpty()) {
        clearSnapFeedback();
        return;
    }

    // During a resize, only the edges that actually move may snap. The handle
    // name doesn't reliably map to a bbox edge (the Y axis is inverted between
    // the visual handle space and the document bbox), so detect movement by
    // comparing against the bounds captured at resize start: the fixed anchor
    // edges stay put, the dragged edges change. This stops the anchor edge from
    // winning the snap (which made scale snap "not work", e.g. top/bottom).
    SnapSources sources;
    if (m_transformState.mode == InteractionMode::Resizing
        && !m_resizeStartDocBounds.isEmpty()) {
        const qreal eps = 0.5; // document pixels
        const QRectF& s = m_resizeStartDocBounds;
        sources.left   = std::abs(bounds.left()   - s.left())   > eps;
        sources.right  = std::abs(bounds.right()  - s.right())  > eps;
        sources.top    = std::abs(bounds.top()    - s.top())    > eps;
        sources.bottom = std::abs(bounds.bottom() - s.bottom()) > eps;
        sources.centerX = false;
        sources.centerY = false;
    }

    const SnapResult result = SnapEngine::snapBounds(bounds, currentSnapContext(settings), sources);
    if (!result.snapped()) {
        clearSnapFeedback();
        return;
    }

    QSet<int> appliedIndices;
    auto applyIndex = [&](int idx) {
        if (appliedIndices.contains(idx))
            return;
        appliedIndices.insert(idx);
        translateNodeByDocumentDelta(m_doc->nodeAt(idx), result.snapOffset);
    };

    if (m_transformState.mode == InteractionMode::Moving && m_multiMoveIndices.size() > 1) {
        for (int idx : m_multiMoveIndices)
            applyIndex(idx);
    } else if ((m_transformState.mode == InteractionMode::Resizing
                || m_transformState.mode == InteractionMode::Rotating)
               && m_multiResizeIndices.size() > 1) {
        for (int idx : m_multiResizeIndices)
            applyIndex(idx);
    } else if (m_doc->activeFlatIndex >= 0) {
        applyIndex(m_doc->activeFlatIndex);
    }

    if (m_multiResizeGroupBboxValid && m_doc->size.width() > 0 && m_doc->size.height() > 0) {
        m_multiResizeGroupBboxCenter += QPointF(
            2.0 * result.snapOffset.x() / m_doc->size.width(),
            -2.0 * result.snapOffset.y() / m_doc->size.height());
    }

    if (m_freeTransformActive)
        m_freeTransformDirty = true;

    m_rulerGuideOverlay->setSnapIndicators(result.indicators);
}

void CanvasView::clearSnapFeedback()
{
    if (m_rulerGuideOverlay)
        m_rulerGuideOverlay->clearSnapIndicators();
}

void CanvasView::clearSnapBoundsCache()
{
    m_snapVisiblePixelBoundsCache.clear();
}

static QTransform alignedLayerTransform(LayerTreeNode* node, const QPointF& canvasDelta)
{
    if (!node) return {};
    // Convert the canvas-NDC delta into the PARENT's space — the node's local
    // translation lives in parent coordinates (same conversion as
    // transformWithDocumentDelta). The previous version unmapped through the
    // node's own transform, which works for root nodes but scales/rotates the
    // delta by the parent chain for children of transformed groups (align
    // moved nested layers by the wrong amount/direction).
    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    const QTransform invParent = parentAccum.inverted();
    const qreal localDx = invParent.m11() * canvasDelta.x()
                          + invParent.m21() * canvasDelta.y();
    const qreal localDy = invParent.m12() * canvasDelta.x()
                          + invParent.m22() * canvasDelta.y();
    QTransform t = node->transform();
    t.setMatrix(t.m11(), t.m12(), t.m13(),
                t.m21(), t.m22(), t.m23(),
                t.m31() + localDx, t.m32() + localDy, t.m33());
    return t;
}

static void computeAlignDelta(const QPolygonF& corners, int alignmentType,
                              float refMinX, float refMaxX,
                              float refMinY, float refMaxY,
                              float& dx, float& dy)
{
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (auto& c : corners) {
        if (c.x() < minX) minX = static_cast<float>(c.x());
        if (c.x() > maxX) maxX = static_cast<float>(c.x());
        if (c.y() < minY) minY = static_cast<float>(c.y());
        if (c.y() > maxY) maxY = static_cast<float>(c.y());
    }
    dx = 0; dy = 0;
    switch (alignmentType) {
    case 0: dx = refMinX - minX;                          break; // Align Left
    case 1: dx = (refMinX + refMaxX - minX - maxX) * 0.5f; break; // Align Center H
    case 2: dx = refMaxX - maxX;                          break; // Align Right
    case 3: dy = refMaxY - maxY;                          break; // Align Top
    case 4: dy = (refMinY + refMaxY - minY - maxY) * 0.5f; break; // Align Middle V
    case 5: dy = refMinY - minY;                          break; // Align Bottom
    }
}

// Unions the visual corners of every descendant *leaf* layer of a group into
// an axis-aligned box (canvas NDC). Groups carry no pixels of their own, so the
// group's reference box for Align/Move is the aggregate of its children.
static void accumulateLeafCorners(const LayerTreeNode* node,
                                  float& minX, float& maxX,
                                  float& minY, float& maxY, bool& any)
{
    if (!node) return;
    for (const auto& child : node->children) {
        if (child->type == LayerTreeNode::Type::Group) {
            accumulateLeafCorners(child.get(), minX, maxX, minY, maxY, any);
        } else if (child->layer) {
            const QPolygonF c = TransformController::cornersFromNode(child.get());
            for (const auto& p : c) {
                any = true;
                const float px = static_cast<float>(p.x());
                const float py = static_cast<float>(p.y());
                if (px < minX) minX = px;
                if (px > maxX) maxX = px;
                if (py < minY) minY = py;
                if (py > maxY) maxY = py;
            }
        }
    }
}

// Visual bounding-box corners of a node in canvas NDC. A plain layer reuses the
// existing per-node corners; a Group reports its aggregated child bounds so the
// whole group aligns/moves as a single entity while children keep their offsets.
static QPolygonF nodeAlignCorners(const LayerTreeNode* node)
{
    if (!node) return {};
    if (node->type != LayerTreeNode::Type::Group)
        return TransformController::cornersFromNode(node);

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    bool any = false;
    accumulateLeafCorners(node, minX, maxX, minY, maxY, any);
    if (!any)
        return TransformController::cornersFromNode(node);

    QPolygonF poly;
    poly << QPointF(minX, minY) << QPointF(maxX, minY)
         << QPointF(maxX, maxY) << QPointF(minX, maxY);
    return poly;
}

static void computeAlignRef(const Document* doc, bool fromSelection,
                            float& refMinX, float& refMaxX,
                            float& refMinY, float& refMaxY)
{
    if (fromSelection && doc->selectedFlatIndices.size() > 1) {
        auto* targetNode = doc->activeNode();
        if (targetNode && targetNode->canTransform()) {
            refMinX = 1e9f; refMaxX = -1e9f;
            refMinY = 1e9f; refMaxY = -1e9f;
            QPolygonF c = nodeAlignCorners(targetNode);
            for (auto& p : c) {
                const float px = static_cast<float>(p.x());
                const float py = static_cast<float>(p.y());
                if (px < refMinX) refMinX = px;
                if (px > refMaxX) refMaxX = px;
                if (py < refMinY) refMinY = py;
                if (py > refMaxY) refMaxY = py;
            }
        } else {
            refMinX = -1.0f; refMaxX = 1.0f;
            refMinY = -1.0f; refMaxY = 1.0f;
        }
    } else {
        refMinX = -1.0f; refMaxX = 1.0f;
        refMinY = -1.0f; refMaxY = 1.0f;
    }
}

void CanvasView::doAlignLayer(int alignmentType)
{
    if (!m_controller || !m_doc) return;

    const bool toSelection = (m_alignTarget == 1);

    float refMinX, refMaxX, refMinY, refMaxY;
    computeAlignRef(m_doc, toSelection, refMinX, refMaxX, refMinY, refMaxY);

    const bool multiSelect = m_doc->selectedFlatIndices.size() > 1;

    if (multiSelect) {
        // Membership set used to drop nodes whose ancestor is also selected,
        // so a child isn't moved twice (once on its own, once via its group).
        QSet<LayerTreeNode*> selectedNodes;
        for (int idx : m_doc->selectedFlatIndices) {
            if (auto* node = m_doc->nodeAt(idx))
                selectedNodes.insert(node);
        }
        auto hasSelectedAncestor = [&selectedNodes](LayerTreeNode* node) -> bool {
            for (auto* p = node ? node->parent : nullptr; p; p = p->parent) {
                if (selectedNodes.contains(p))
                    return true;
            }
            return false;
        };

        // Top-level participants of the selection (groups included).
        std::vector<int> partIndices;
        std::vector<LayerTreeNode*> partNodes;
        for (int idx : m_doc->selectedFlatIndices) {
            auto* node = m_doc->nodeAt(idx);
            if (!node || !node->canTransform())
                continue;
            // Only spatial entities take part — adjustment/effect nodes have no
            // visual bounds and would otherwise pollute the combined box.
            if (node->type != LayerTreeNode::Type::Layer
                && node->type != LayerTreeNode::Type::Group)
                continue;
            if (hasSelectedAncestor(node))
                continue;
            // "Align to Selection" treats the active layer as the key object the
            // others align to, so the key itself stays put.
            if (toSelection && idx == m_doc->activeFlatIndex)
                continue;
            partIndices.push_back(idx);
            partNodes.push_back(node);
        }
        if (partNodes.empty()) { update(); return; }

        std::vector<int> indices;
        std::vector<QTransform> oldTransforms;
        std::vector<QTransform> newTransforms;

        if (toSelection) {
            // Key-object align: each participant aligns individually to the
            // active layer's bounds (legacy behavior, preserved).
            for (size_t i = 0; i < partNodes.size(); ++i) {
                auto* node = partNodes[i];
                QPolygonF corners = nodeAlignCorners(node);
                float dx = 0, dy = 0;
                computeAlignDelta(corners, alignmentType,
                                  refMinX, refMaxX, refMinY, refMaxY, dx, dy);
                if (dx == 0.0f && dy == 0.0f)
                    continue;
                indices.push_back(partIndices[i]);
                oldTransforms.push_back(node->transform());
                const QTransform after = alignedLayerTransform(node, QPointF(dx, dy));
                m_controller->previewNodeTransform(node, after);
                newTransforms.push_back(after);
            }
            // Participants moved by differing amounts, so the cached group
            // outline no longer describes them — drop it and let the live path
            // recompute from the new corners.
            if (!indices.empty())
                m_multiResizeGroupBboxValid = false;
        } else {
            // Align to Canvas: treat the whole selection as one entity. Compute
            // its combined bounding box, derive a single offset, and shift every
            // participant by that same offset so internal spacing is preserved.
            float cMinX = 1e9f, cMaxX = -1e9f, cMinY = 1e9f, cMaxY = -1e9f;
            for (auto* node : partNodes) {
                const QPolygonF corners = nodeAlignCorners(node);
                for (const auto& p : corners) {
                    const float px = static_cast<float>(p.x());
                    const float py = static_cast<float>(p.y());
                    if (px < cMinX) cMinX = px;
                    if (px > cMaxX) cMaxX = px;
                    if (py < cMinY) cMinY = py;
                    if (py > cMaxY) cMaxY = py;
                }
            }
            QPolygonF combined;
            combined << QPointF(cMinX, cMinY) << QPointF(cMaxX, cMinY)
                     << QPointF(cMaxX, cMaxY) << QPointF(cMinX, cMaxY);

            float dx = 0, dy = 0;
            computeAlignDelta(combined, alignmentType,
                              refMinX, refMaxX, refMinY, refMaxY, dx, dy);
            if (dx == 0.0f && dy == 0.0f) { update(); return; }

            for (size_t i = 0; i < partNodes.size(); ++i) {
                auto* node = partNodes[i];
                indices.push_back(partIndices[i]);
                oldTransforms.push_back(node->transform());
                const QTransform after = alignedLayerTransform(node, QPointF(dx, dy));
                m_controller->previewNodeTransform(node, after);
                newTransforms.push_back(after);
            }
            // The whole selection shifted by the same canvas offset, so keep the
            // cached group outline (and its rotation/size) following along.
            if (multiOutlineMatchesSelection()) {
                m_multiResizeGroupBboxCenter += QPointF(dx, dy);
                m_multiResizeGroupStartCenterVis = QPointF(
                    m_multiResizeGroupBboxCenter.x()
                        * std::max(1e-6, m_canvasHalfExtents.x() * std::max(1, size().width())),
                    m_multiResizeGroupBboxCenter.y()
                        * std::max(1e-6, m_canvasHalfExtents.y() * std::max(1, size().height())));
            }
        }

        if (!indices.empty())
            m_controller->setNodeTransforms(indices, newTransforms, oldTransforms);
        update();
        return;
    }

    // Single selection — a plain layer or a group (groups use aggregate bounds).
    auto* node = m_doc->activeNode();
    if (!node || !node->canTransform()) return;
    if (node->type != LayerTreeNode::Type::Layer
        && node->type != LayerTreeNode::Type::Group)
        return;

    QPolygonF corners = nodeAlignCorners(node);

    float dx = 0, dy = 0;
    computeAlignDelta(corners, alignmentType,
                      refMinX, refMaxX, refMinY, refMaxY, dx, dy);
    if (dx == 0.0f && dy == 0.0f) return;

    QTransform oldTransform = node->transform();
    const QTransform aligned = alignedLayerTransform(node, QPointF(dx, dy));
    m_controller->previewNodeTransform(node, aligned);
    if (node->type == LayerTreeNode::Type::Group) {
        // Groups have no Layer, so they commit through the node-transform path.
        m_controller->setNodeTransforms({m_doc->activeFlatIndex},
                                        {aligned}, {oldTransform});
    } else {
        m_controller->setLayerTransform(m_doc->activeFlatIndex,
                                        aligned, &oldTransform);
    }
    update();
}

LayerTreeNode* CanvasView::freeTransformNode() const
{
    if (!m_doc || m_freeTransformFlatIndex < 0 || m_freeTransformFlatIndex >= m_doc->flatCount())
        return nullptr;
    return m_doc->nodeAt(m_freeTransformFlatIndex);
}

// ── Distort / Perspective (non-destructive smart layer) ──────────────────
//
// The warped pixels live in the layer's cpuImage and are composited through the
// normal affine pipeline (so masks/effects/blend/thumbnail/save keep working
// untouched). DistortData preserves the original pixels + editable quad so the
// warp can be re-edited at any time. The perspective never enters a transform.

bool CanvasView::activeLayerSupportsDistort() const
{
    if (!m_doc) return false;
    auto* node = m_doc->activeNode();
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
        return false;
    if (node->isPositionLocked()) return false;
    // Text/shape keep their own non-destructive model; only raster or an
    // existing distort layer qualifies.
    if (node->layer->isTextLayer() || node->layer->isShapeLayer())
        return false;
    return !node->layer->compositeImage().isNull();
}

bool CanvasView::activeLayerHasDistort() const
{
    if (!m_doc) return false;
    auto* node = m_doc->activeNode();
    return node && node->layer && node->layer->distortData != nullptr;
}

void CanvasView::resetDistort()
{
    if (!m_doc || !m_controller) return;

    // When a session is open, target that layer; otherwise the active layer.
    const int flatIndex = m_distortActive ? m_distortFlatIndex
                                           : m_doc->activeFlatIndex;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->distortData) return;
    Layer* layer = node->layer.get();
    DistortData& dd = *layer->distortData;

    // Already at the original shape — nothing to reset.
    if (dd.quad.toPolygon() == dd.sourceQuad.toPolygon())
        return;

    // Snapshot for a single undo step.
    QImage beforeImage = layer->cpuImage.copy();
    QTransform beforeTransform = node->transform();
    auto beforeData = std::make_shared<DistortData>(dd);

    // Reset the quad to the original and re-warp at full quality.
    dd.quad = dd.sourceQuad;
    if (m_distortActive && flatIndex == m_distortFlatIndex) {
        m_distortQuad = dd.quad;
        m_distortLastValidQuad = m_distortQuad;
        renderDistortLayer(/*highQuality=*/true);
        updateDistortOverlay();
    } else {
        // No live session: render through the same helper by briefly pointing
        // the session index at this layer (renderDistortLayer uses it).
        const int savedIdx = m_distortFlatIndex;
        m_distortFlatIndex = flatIndex;
        renderDistortLayer(/*highQuality=*/true);
        m_distortFlatIndex = savedIdx;
    }

    auto afterData = std::make_shared<DistortData>(dd);
    m_controller->history().push(std::make_unique<DistortCommand>(
        m_doc, flatIndex,
        std::move(beforeImage), beforeTransform, std::move(beforeData),
        layer->cpuImage.copy(), node->transform(), std::move(afterData),
        tr("Reset Distort")));
    if (m_distortActive)
        ++m_distortSessionSteps;

    emit m_controller->layerChanged(flatIndex);
    emit m_controller->imageChanged();
    update();
    emit distortStateChanged();
}

// Convert a canvas-NDC point (as returned by cornersFromNode) to document px.
static QPointF canvasNdcToDocPx(const QPointF& ndc, const QSize& docSize)
{
    return QPointF((ndc.x() + 1.0) * 0.5 * docSize.width(),
                   (1.0 - ndc.y()) * 0.5 * docSize.height());
}

// Inverse of canvasNdcToDocPx: document px → canvas NDC.
static QPointF docPxToCanvasNdc(const QPointF& doc, const QSize& docSize)
{
    const double w = std::max(1, docSize.width());
    const double h = std::max(1, docSize.height());
    return QPointF(doc.x() / w * 2.0 - 1.0,
                   1.0 - doc.y() / h * 2.0);
}

// Remap a document-px quad through the change in node transform (oldXf → newXf).
// Used to keep the editable quad anchored to the layer after it was moved/scaled/
// rotated outside a distort session.
static TransformQuad remapQuadByTransformDelta(const TransformQuad& quad,
                                               const QTransform& oldXf,
                                               const QTransform& newXf,
                                               const QSize& docSize)
{
    bool ok = false;
    const QTransform oldInv = oldXf.inverted(&ok);
    if (!ok) return quad; // singular — leave as-is
    const QTransform remap = oldInv * newXf; // local-NDC stays fixed, world moves

    TransformQuad out;
    for (int i = 0; i < 4; ++i) {
        const QPointF ndc = docPxToCanvasNdc(quad[i], docSize);
        out[i] = canvasNdcToDocPx(remap.map(ndc), docSize);
    }
    return out;
}

// Warp `src` (whose full rect maps onto `srcQuadDoc` in document px) into the
// quad `dstQuadDoc`. Returns the rendered RGBA image and its document-px origin.
// `highQuality` selects cubic (commit/idle) vs linear (live drag) interpolation.
// `previewScale` (≤1.0) renders into a proportionally smaller buffer for live
// drags — the GPU upsamples it to the same document footprint, so distorting a
// huge layer stays cheap. `outFootprint` reports the full document-px footprint
// the result covers (independent of the buffer's pixel size), used to place it.
static QImage warpDistort(const QImage& src,
                          const TransformQuad& srcQuadDoc,
                          const TransformQuad& dstQuadDoc,
                          bool highQuality,
                          double previewScale,
                          QPoint* outOrigin,
                          QSize* outFootprint)
{
    if (src.isNull() || !dstQuadDoc.isValid())
        return QImage();

    QRect dstBounds = dstQuadDoc.toPolygon().boundingRect().toAlignedRect();
    if (dstBounds.width() < 1 || dstBounds.height() < 1)
        return QImage();
    const int kMaxDim = 16384;
    if (dstBounds.width() > kMaxDim || dstBounds.height() > kMaxDim)
        return QImage();

    if (outOrigin) *outOrigin = dstBounds.topLeft();
    if (outFootprint) *outFootprint = dstBounds.size();

    // Buffer size = footprint × previewScale. The destination points are scaled
    // to match so the homography fills the (smaller) buffer; placement uses the
    // full document footprint, so the GPU upsamples the buffer to cover it.
    const double scale = std::clamp(previewScale, 0.001, 1.0);
    const int outW = std::max(1, qRound(dstBounds.width()  * scale));
    const int outH = std::max(1, qRound(dstBounds.height() * scale));
    const double sx = static_cast<double>(outW) / dstBounds.width();
    const double sy = static_cast<double>(outH) / dstBounds.height();

    const QImage srcRgba = src.format() == QImage::Format_RGBA8888
        ? src : src.convertToFormat(QImage::Format_RGBA8888);

    // Source points: where srcQuadDoc lands in source-image pixels. srcQuadDoc
    // maps the image's full rect, so invert that rect→quad map and apply it to
    // the image corners — but since srcQuad is the image's own bounding mapping,
    // the source points are simply the image corners.
    std::vector<cv::Point2f> srcPts = {
        { 0.0f, 0.0f },
        { static_cast<float>(srcRgba.width()), 0.0f },
        { static_cast<float>(srcRgba.width()), static_cast<float>(srcRgba.height()) },
        { 0.0f, static_cast<float>(srcRgba.height()) }
    };
    auto rel = [&](const QPointF& p) {
        return cv::Point2f(static_cast<float>((p.x() - dstBounds.left()) * sx),
                           static_cast<float>((p.y() - dstBounds.top())  * sy));
    };
    std::vector<cv::Point2f> dstPts = {
        rel(dstQuadDoc.topLeft), rel(dstQuadDoc.topRight),
        rel(dstQuadDoc.bottomRight), rel(dstQuadDoc.bottomLeft)
    };

    cv::Mat H = cv::getPerspectiveTransform(srcPts, dstPts);
    cv::Mat srcMat(srcRgba.height(), srcRgba.width(), CV_8UC4,
                   const_cast<uchar*>(srcRgba.bits()),
                   static_cast<size_t>(srcRgba.bytesPerLine()));
    cv::Mat dstMat(outH, outW, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    cv::warpPerspective(srcMat, dstMat, H,
                        cv::Size(outW, outH),
                        highQuality ? cv::INTER_CUBIC : cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));

    QImage out(dstMat.data, dstMat.cols, dstMat.rows,
               static_cast<int>(dstMat.step), QImage::Format_RGBA8888);
    out = out.copy(); // detach from cv::Mat storage
    (void)srcQuadDoc; // srcQuad currently always == source image rect
    return out;
}

// Build the affine transform that places a result image covering the
// document-px footprint (size footprintDocPx, origin resultOrigin) at the
// correct spot on the canvas. The footprint is in document pixels and is
// independent of the image's own pixel resolution — a low-res preview buffer
// covers the same footprint and is upsampled by the GPU.
static QTransform placementTransform(const QSize& footprintDocPx,
                                     const QPoint& resultOrigin,
                                     const QSize& docSize)
{
    const int docW = std::max(1, docSize.width());
    const int docH = std::max(1, docSize.height());
    const double halfW = static_cast<double>(footprintDocPx.width()) / docW;
    const double halfH = static_cast<double>(footprintDocPx.height()) / docH;
    const double centerDocX = resultOrigin.x() + footprintDocPx.width() * 0.5;
    const double centerDocY = resultOrigin.y() + footprintDocPx.height() * 0.5;
    const double cx = centerDocX / docW * 2.0 - 1.0;
    const double cy = 1.0 - centerDocY / docH * 2.0;
    QTransform t;
    t.setMatrix(halfW, 0.0, 0.0,
                0.0, halfH, 0.0,
                cx, cy, 1.0);
    return t;
}

// Re-render the layer's cpuImage from its DistortData quad and reposition it.
// Shared by the live drag, commit, and (future) zoom refresh.
void CanvasView::renderDistortLayer(bool highQuality)
{
    auto* node = m_doc ? m_doc->nodeAt(m_distortFlatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->distortData) return;
    Layer* layer = node->layer.get();
    DistortData& dd = *layer->distortData;

    QPoint origin;
    QImage warped;
    QSize footprint;
    if (dd.quad.toPolygon() == dd.sourceQuad.toPolygon()) {
        // Quad back at the original shape: use the source pixels verbatim (no
        // resample) so a reset restores full original quality.
        warped = dd.sourceImage;
        origin = dd.sourceQuad.toPolygon().boundingRect().toAlignedRect().topLeft();
        footprint = warped.size();
    } else {
        // Live drag (low quality) warps into a viewport-sized buffer so large
        // layers stay responsive; commit/idle (high quality) warps at full res.
        double previewScale = 1.0;
        if (!highQuality) {
            const QRect fp = dd.quad.toPolygon().boundingRect().toAlignedRect();
            previewScale = distortPreviewScale(fp);
        }
        warped = warpDistort(dd.sourceImage, dd.sourceQuad, dd.quad,
                             highQuality, previewScale, &origin, &footprint);
    }
    if (warped.isNull()) return;

    dd.resultOrigin = origin;
    if (layer->renderRasterStorage().isEnabled())
        layer->renderRasterStorage().clear();
    layer->cpuImage = warped;
    layer->tiledSystem = false;
    layer->tileManager.clear();
    layer->dirtyRegion.clear();
    layer->textureOutdated = true;
    layer->pendingGpuUpload = true;
    // placementTransform() positions the warped footprint in canvas NDC (the
    // quad lives in document pixels) — that's the layer's WORLD frame. Strip
    // the parent chain to store it as the LOCAL transform, otherwise a layer
    // nested in a transformed group gets the group transform applied twice.
    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    node->setBaseTransform(placementTransform(footprint, origin, m_doc->size)
                      * parentAccum.inverted());
    // Record the WORLD transform the quad is consistent with, so a later move/
    // scale/rotate of the layer (or of an ancestor group) can be reconciled
    // when distort is re-entered.
    dd.quadTransform = node->accumulatedTransform();
    node->invalidateEffects();
    if (m_doc) ++m_doc->compositionGeneration;
}

// Choose the live-drag warp resolution: render no more pixels than the viewport
// actually shows (≈ on-screen zoom), with an absolute cap so even a fully
// zoomed-in huge layer stays bounded. The full-res cubic warp on release keeps
// the committed result crisp.
double CanvasView::distortPreviewScale(const QRect& footprintDoc) const
{
    const double footLong = std::max(footprintDoc.width(), footprintDoc.height());
    if (footLong <= 1.0) return 1.0;
    const QPointF a = documentToScreen(footprintDoc.topLeft());
    const QPointF b = documentToScreen(footprintDoc.bottomRight());
    const double screenLong = std::max(std::abs(b.x() - a.x()),
                                       std::abs(b.y() - a.y()));
    double scale = screenLong / footLong;              // ≈ on-screen scale
    constexpr double kMaxPreviewLongSide = 2048.0;     // absolute safety cap
    scale = std::min(scale, kMaxPreviewLongSide / footLong);
    return std::clamp(scale, 0.02, 1.0);
}

bool CanvasView::beginDistort(TransformMode mode)
{
    if (mode != TransformMode::Distort && mode != TransformMode::Perspective)
        return false;
    if (!m_doc || !m_controller) return false;
    if (!activeLayerSupportsDistort()) return false;

    if (m_currentTool != Tool::Skew)
        setTool(Tool::Skew);

    // Switching Distort↔Perspective mid-session keeps the current quad.
    if (m_distortActive && m_distortFlatIndex == m_doc->activeFlatIndex) {
        m_distortMode = mode;
        auto* n = m_doc->nodeAt(m_distortFlatIndex);
        if (n && n->layer && n->layer->distortData)
            n->layer->distortData->mode = mode;
        updateDistortOverlay();
        emit distortStateChanged();
        return true;
    }

    if (m_freeTransformActive)
        commitFreeTransform();

    auto* node = m_doc->activeNode();
    if (!node || !node->layer) return false;
    Layer* layer = node->layer.get();

    m_distortActive = true;
    m_distortDirty = false;
    m_distortMode = mode;
    m_distortFlatIndex = m_doc->activeFlatIndex;
    m_distortOriginalTransform = node->transform();
    m_distortDragCorner = -1;
    m_distortSessionSteps = 0;

    // Snapshot the pre-session layer state for undo/cancel. The expanded variant
    // also captures rasterStorage tiles painted OUTSIDE baseSize (out-of-bounds
    // brush dabs that grew the layer) — compositeImage() would clip them to the
    // original layer rect, so the warp dropped them. srcBounds is the layer-pixel
    // rect the snapshot covers (origin may be negative / past baseSize).
    QRect srcBounds;
    m_distortBeforeImage = layer->compositeImageExpanded(&srcBounds)
                               .convertToFormat(QImage::Format_RGBA8888);
    m_distortBeforeData = layer->distortData
        ? std::make_shared<DistortData>(*layer->distortData) : nullptr;

    if (!layer->distortData) {
        // First time: create DistortData from the current pixels + visual quad.
        // sourceImage covers srcBounds (≥ baseSize), so map srcBounds' four corners
        // explicitly: layer pixels → layer NDC (relative to baseSize) → the layer's
        // WORLD (accumulated) transform → document px. Mapping the EXPANDED rect
        // (not the fixed ±1 base quad) makes the editable outline and the warp
        // source span the out-of-bounds dabs. The accumulated transform pulls in
        // the parent chain so a layer nested in a transformed group lands right.
        // Layer-local NDC has y=+1 at the image top and x=-1 at the left, so layer
        // pixel (0,0) is NDC(-1,+1); document y grows downward (canvasNdcToDocPx).
        auto dd = std::make_shared<DistortData>();
        dd->sourceImage = m_distortBeforeImage;
        dd->mode = mode;

        const QTransform xf = node->accumulatedTransform();
        const QSize docSize = m_doc->size;
        const QSize baseSize = layer->rasterBaseSize();
        const double bw = std::max(1, baseSize.width());
        const double bh = std::max(1, baseSize.height());
        auto pxToDoc = [&](double px, double py) {
            return canvasNdcToDocPx(
                xf.map(QPointF(px / bw * 2.0 - 1.0, 1.0 - py / bh * 2.0)), docSize);
        };
        const double l = srcBounds.left();
        const double t = srcBounds.top();
        const double r = srcBounds.left() + srcBounds.width();
        const double b = srcBounds.top() + srcBounds.height();
        dd->sourceQuad.topLeft     = pxToDoc(l, t);
        dd->sourceQuad.topRight    = pxToDoc(r, t);
        dd->sourceQuad.bottomRight = pxToDoc(r, b);
        dd->sourceQuad.bottomLeft  = pxToDoc(l, b);
        dd->quad = dd->sourceQuad;
        dd->resultOrigin = dd->sourceQuad.toPolygon().boundingRect()
                               .toAlignedRect().topLeft();
        dd->quadTransform = xf;
        layer->distortData = dd;
    } else {
        // Re-editing an existing distort layer. If the layer was moved/scaled/
        // rotated since the last distort session (directly or via a parent
        // group), its WORLD transform changed but the doc-px quads did not —
        // reconcile them so the outline tracks the layer's current position
        // instead of snapping it back on first drag.
        DistortData& dd = *layer->distortData;
        dd.mode = mode;
        const QTransform worldXf = node->accumulatedTransform();
        if (dd.quadTransform != worldXf) {
            dd.quad = remapQuadByTransformDelta(dd.quad, dd.quadTransform,
                                                worldXf, m_doc->size);
            dd.sourceQuad = remapQuadByTransformDelta(dd.sourceQuad, dd.quadTransform,
                                                      worldXf, m_doc->size);
            dd.quadTransform = worldXf;
        }
    }

    m_distortQuad = layer->distortData->quad;
    m_distortLastValidQuad = m_distortQuad;

    updateToolCursor();
    updateDistortOverlay();
    update();
    emit toolChanged(static_cast<int>(m_currentTool));
    emit distortStateChanged();
    return true;
}

QPolygonF CanvasView::distortQuadToScreen() const
{
    QPolygonF poly;
    poly << documentToScreen(m_distortQuad.topLeft)
         << documentToScreen(m_distortQuad.topRight)
         << documentToScreen(m_distortQuad.bottomRight)
         << documentToScreen(m_distortQuad.bottomLeft);
    return poly;
}

void CanvasView::updateDistortOverlay()
{
    if (!m_distortOverlay) return;
    if (!m_distortActive) { m_distortOverlay->finish(); return; }
    // The layer's cpuImage already shows the warp; the overlay only draws the
    // quad outline + corner handles on top.
    m_distortOverlay->setQuad(distortQuadToScreen(), m_distortDragCorner);
}

void CanvasView::distortMousePress(QMouseEvent* e)
{
    const QPolygonF screenQuad = distortQuadToScreen();
    const double grabRadius = 12.0;
    int best = -1;
    double bestDist = grabRadius * grabRadius;
    for (int i = 0; i < 4; ++i) {
        QPointF d = screenQuad[i] - e->position();
        double dist2 = d.x() * d.x() + d.y() * d.y();
        if (dist2 <= bestDist) {
            bestDist = dist2;
            best = i;
        }
    }
    if (best < 0) {
        m_distortDragCorner = -1;
        return;
    }

    m_distortDragCorner = best;
    m_distortDragStartQuad = m_distortQuad;
    m_distortDragStartCorner = m_distortQuad[best];
    m_distortAxisLocked = false;
    m_distortAxisHorizontal = true;
    if (m_distortMode == TransformMode::Perspective
        && (e->modifiers() & Qt::ShiftModifier)) {
        m_distortAxisLocked = true;
    }

    // Snapshot the layer state at the start of this drag so the whole drag can
    // be pushed as a single undo step when the handle is released.
    auto* node = m_doc ? m_doc->nodeAt(m_distortFlatIndex) : nullptr;
    if (node && node->layer) {
        m_distortDragBeforeImage = node->layer->cpuImage.copy();
        m_distortDragBeforeTransform = node->transform();
        m_distortDragBeforeData = node->layer->distortData
            ? std::make_shared<DistortData>(*node->layer->distortData) : nullptr;
    }
    updateDistortOverlay();
}

void CanvasView::distortMouseMove(QMouseEvent* e)
{
    if (m_distortDragCorner < 0) return;

    const QPointF mouseDoc = screenToDocument(e->position());
    const bool shift = e->modifiers() & Qt::ShiftModifier;

    TransformQuad candidate;
    if (m_distortMode == TransformMode::Perspective) {
        if (shift && !m_distortAxisLocked) {
            QPointF d = mouseDoc - m_distortDragStartCorner;
            m_distortAxisHorizontal = std::abs(d.x()) >= std::abs(d.y());
            m_distortAxisLocked = true;
        }
        candidate = TransformController::perspectiveDragCorner(
            m_distortDragStartQuad, m_distortDragCorner,
            m_distortDragStartCorner, mouseDoc,
            m_distortAxisLocked, m_distortAxisHorizontal);
    } else {
        candidate = TransformController::distortDragCorner(
            m_distortDragStartQuad, m_distortDragCorner,
            m_distortDragStartCorner, mouseDoc, shift);
    }

    if (candidate.isValid())
        m_distortLastValidQuad = candidate;
    else
        candidate = m_distortLastValidQuad;

    m_distortQuad = candidate;
    m_distortDirty = true;

    // Live, non-destructive re-warp (linear interpolation during drag).
    auto* node = m_doc ? m_doc->nodeAt(m_distortFlatIndex) : nullptr;
    if (node && node->layer && node->layer->distortData) {
        node->layer->distortData->quad = m_distortQuad;
        renderDistortLayer(/*highQuality=*/false);
    }
    updateDistortOverlay();
    update();
}

void CanvasView::distortMouseRelease(QMouseEvent* /*e*/)
{
    if (m_distortDragCorner < 0) return;

    // Final position: re-warp at full quality.
    auto* node = m_doc ? m_doc->nodeAt(m_distortFlatIndex) : nullptr;
    const bool moved = m_distortQuad.toPolygon() != m_distortDragStartQuad.toPolygon();
    if (node && node->layer && node->layer->distortData) {
        node->layer->distortData->quad = m_distortQuad;
        renderDistortLayer(/*highQuality=*/true);

        // Push this handle drag as its own undo step (before → after of this
        // single drag) so undo/redo replays each movement of the outline.
        if (moved && m_controller) {
            const QString name = m_distortMode == TransformMode::Perspective
                ? tr("Perspective") : tr("Distort");
            auto afterData = node->layer->distortData
                ? std::make_shared<DistortData>(*node->layer->distortData) : nullptr;
            m_controller->history().push(std::make_unique<DistortCommand>(
                m_doc, m_distortFlatIndex,
                m_distortDragBeforeImage, m_distortDragBeforeTransform,
                m_distortDragBeforeData,
                node->layer->cpuImage.copy(), node->transform(), afterData, name));
            ++m_distortSessionSteps;
            m_distortDirty = true;
            if (m_controller) {
                emit m_controller->layerChanged(m_doc->activeFlatIndex);
                emit m_controller->imageChanged();
            }
        }
    }

    m_distortDragCorner = -1;
    m_distortDragBeforeImage = QImage();
    m_distortDragBeforeData = nullptr;
    updateDistortOverlay();
    update();
}

void CanvasView::commitDistort()
{
    if (!m_distortActive) return;

    auto* node = m_doc ? m_doc->nodeAt(m_distortFlatIndex) : nullptr;
    if (!node || !node->layer) { cancelDistort(); return; }

    // Each handle drag was already pushed as its own undo step on release, so
    // commit just leaves edit mode — nothing more to record. The layer stays a
    // re-editable distort layer.
    m_distortActive = false;
    m_distortDirty = false;
    m_distortFlatIndex = -1;
    m_distortDragCorner = -1;
    m_distortSessionSteps = 0;
    m_distortBeforeImage = QImage();
    m_distortBeforeData = nullptr;
    m_distortDragBeforeImage = QImage();
    m_distortDragBeforeData = nullptr;
    if (m_distortOverlay) m_distortOverlay->finish();
    if (m_doc) ++m_doc->compositionGeneration;

    updateToolCursor();
    update();
    emit distortStateChanged();
}

void CanvasView::cancelDistort()
{
    if (!m_distortActive) return;

    // Each handle drag of this session was pushed as its own undo step. Roll
    // them back through history so the undo stack stays consistent (rather than
    // restoring pixels manually and leaving orphaned commands behind).
    const int stepsToUndo = m_distortSessionSteps;
    m_distortSessionSteps = 0;
    m_distortActive = false;
    m_distortDirty = false;
    m_distortFlatIndex = -1;
    m_distortDragCorner = -1;
    m_distortBeforeImage = QImage();
    m_distortBeforeData = nullptr;
    m_distortDragBeforeImage = QImage();
    m_distortDragBeforeData = nullptr;
    if (m_distortOverlay) m_distortOverlay->finish();

    if (m_controller && stepsToUndo > 0) {
        for (int i = 0; i < stepsToUndo && m_controller->history().canUndo(); ++i)
            m_controller->history().undo();
        emit m_controller->historyChanged();
        emit m_controller->layerChanged(m_doc->activeFlatIndex);
        emit m_controller->imageChanged();
    }
    if (m_doc) ++m_doc->compositionGeneration;
    updateToolCursor();
    update();
    emit distortStateChanged();
}

bool CanvasView::beginFreeTransform()
{
    if (!m_doc || !m_controller) return false;
    if (m_doc->activeFlatIndex < 0 || m_doc->activeFlatIndex >= m_doc->flatCount()) return false;
    auto* node = m_doc->activeNode();
    const bool isGroup = node && node->type == LayerTreeNode::Type::Group;
    // Groups free-transform as a unit (applied to the group node, children
    // follow); raster/text/shape layers need a real layer.
    if (!node || (!isGroup && (node->type != LayerTreeNode::Type::Layer || !node->layer)))
        return false;
    if (node->isPositionLocked()) return false;

    if (m_freeTransformActive && m_freeTransformFlatIndex == m_doc->activeFlatIndex)
        return true;

    if (m_freeTransformActive)
        commitFreeTransform();

    if (m_currentTool != Tool::Move)
        setTool(Tool::Move);

    m_freeTransformActive = true;
    m_freeTransformDirty = false;
    m_freeTransformFlatIndex = m_doc->activeFlatIndex;
    m_freeTransformOriginal = node->transform();
    // Session capture: gestures during the session record every node they
    // touch (first-seen transform); the active node anchors the list.
    m_freeTransformSessionIndices.clear();
    m_freeTransformSessionStartTransforms.clear();
    m_freeTransformSessionIndices.push_back(m_freeTransformFlatIndex);
    m_freeTransformSessionStartTransforms.push_back(node->transform());
    if (node->layer && node->layer->textData) {
        m_textLayerIndex = m_freeTransformFlatIndex;
        m_textBeforeSnapshot = *node->layer->textData;
        m_textBeforeTransform = node->transform();
    }
    m_transformState.mode = InteractionMode::Idle;
    m_moving = false;
    if (m_transformOverlay)
        m_transformOverlay->setVisible(false);
    updateToolCursor();
    emit freeTransformStateChanged(true);
    emit activeTransformChanged();
    update();
    return true;
}

void CanvasView::commitFreeTransform()
{
    if (!m_freeTransformActive) return;

    auto* node = freeTransformNode();

    if (node && m_controller && m_freeTransformDirty) {
        bool isText = node->layer && node->layer->textData;
        bool isShape = node->layer && node->layer->shapeData;

        // Gestures during the session pushed nothing — consolidate here.
        // Text/shape commit through their own commands; every other touched
        // session node (active raster/group included) goes into one
        // NodeTransformCommand spanning session start → now.
        bool activeCommitted = false;
        if (auto* textData = node->layer ? node->layer->textData.get() : nullptr) {
            if (textData->flowMode == TextFlowMode::Paragraph) {
                resizeParagraphTextBoxToTransform(node, false);
            } else {
                bakeTextLayerResolution(node);
            }
            // commitTextEdit pushes nothing when the text data is unchanged
            // (pure move/rotate); the transform then commits via the batch.
            activeCommitted = m_controller->commitTextEdit(m_freeTransformFlatIndex,
                m_textBeforeSnapshot, m_textBeforeTransform);
        } else if (node->layer && node->layer->shapeData) {
            // Re-render at the new world scale/rotation so the shape stays crisp
            // (world-aware: correct for top-level and grouped shapes alike).
            m_controller->bakeShapeTransform(m_freeTransformFlatIndex,
                m_freeTransformOriginal);
            activeCommitted = true;
        } else if (node->type == LayerTreeNode::Type::Group) {
            // TODO - review
            auto composite = std::make_unique<CompositeCommand>(tr("Transform Group"));
            composite->add(std::make_unique<NodeTransformCommand>(
                m_doc, std::vector<int>{m_freeTransformFlatIndex},
                std::vector<QTransform>{m_freeTransformOriginal},
                std::vector<QTransform>{node->transform()}, tr("Transform Group")));
            if (bakeGroupVectorChildrenToComposite(
                    node, tr("Transform Group"), *composite)) {
                m_controller->pushCommand(std::move(composite));
                activeCommitted = true;
            }
        }

        std::vector<int> indices;
        std::vector<QTransform> beforeXfs, afterXfs;
        for (size_t i = 0; i < m_freeTransformSessionIndices.size(); ++i) {
            const int idx = m_freeTransformSessionIndices[i];
            if (activeCommitted && idx == m_freeTransformFlatIndex)
                continue;
            auto* n = m_doc->nodeAt(idx);
            if (!n || n->transform() == m_freeTransformSessionStartTransforms[i])
                continue;
            indices.push_back(idx);
            beforeXfs.push_back(m_freeTransformSessionStartTransforms[i]);
            afterXfs.push_back(n->transform());
        }
        if (!indices.empty())
            m_controller->setNodeTransforms(indices, afterXfs, beforeXfs);
    }

    m_freeTransformActive = false;
    m_freeTransformDirty = false;
    m_freeTransformFlatIndex = -1;
    m_freeTransformOriginal = QTransform();
    m_freeTransformSessionIndices.clear();
    m_freeTransformSessionStartTransforms.clear();
    m_transformState.mode = InteractionMode::Idle;
    m_moving = false;
    if (m_transformOverlay)
        m_transformOverlay->setVisible(false);
    updateToolCursor();
    emit freeTransformStateChanged(false);
    emit activeTransformChanged();
    update();
}

void CanvasView::cancelFreeTransform()
{
    if (!m_freeTransformActive) return;

    // Restore every node touched during the session (gestures pushed nothing
    // to the history, so this is a full cancel).
    for (size_t i = 0; i < m_freeTransformSessionIndices.size(); ++i) {
        if (auto* n = m_doc ? m_doc->nodeAt(m_freeTransformSessionIndices[i]) : nullptr)
            m_controller->previewNodeTransform(
                n, m_freeTransformSessionStartTransforms[i]);
    }

    // Paragraph text mutates its box live during resize gestures — restore the
    // begin-of-session text data and re-render so cancel is visually complete.
    auto* node = freeTransformNode();
    if (node && node->layer && node->layer->textData
        && !m_textBeforeSnapshot.spans.empty()
        && m_textLayerIndex == m_freeTransformFlatIndex) {
        *node->layer->textData = m_textBeforeSnapshot;
        node->layer->textData->dirty = true;
        TextRenderer renderer;
        renderer.render(*node->layer->textData, node->layer->cpuImage);
        node->layer->textureOutdated = true;
        node->invalidateEffects();
        makeCurrent();
        syncLayersToGpu();
        doneCurrent();
    }
    if (m_doc)
        ++m_doc->compositionGeneration;

    m_freeTransformActive = false;
    m_freeTransformDirty = false;
    m_freeTransformFlatIndex = -1;
    m_freeTransformOriginal = QTransform();
    m_freeTransformSessionIndices.clear();
    m_freeTransformSessionStartTransforms.clear();
    m_transformState.mode = InteractionMode::Idle;
    m_moving = false;
    if (m_transformOverlay)
        m_transformOverlay->setVisible(false);
    updateToolCursor();
    emit freeTransformStateChanged(false);
    emit activeTransformChanged();
    update();
}

bool CanvasView::isInRotateZone(const QPolygonF& cornersScreenNdc, QPointF screenPos) const
{
    if (cornersScreenNdc.size() < 4 || width() <= 0 || height() <= 0)
        return false;

    auto ndcToScreen = [this](const QPointF& ndc) -> QPointF {
        return QPointF(
            (ndc.x() + 1.0) * 0.5 * width(),
            (1.0 - ndc.y()) * 0.5 * height());
    };

    // Place the handle EXACTLY as GPUViewport::renderTransformOverlay draws it:
    // in screen-NDC (clip) space, with the 18px stem expressed in NDC-Y units, and
    // the stem direction normalised in NDC. The previous hit-test built the handle
    // in screen PIXELS (isotropic) while the overlay builds it in screen-NDC
    // (anisotropic on non-square viewports), so the two drifted apart as the box
    // rotated — past a certain angle the hit area no longer covered the drawn
    // handle. Mirroring the overlay's math keeps the hit centre on the visual one
    // for every rotation/aspect; only the final radius test is done in pixels.
    const QPointF c0 = cornersScreenNdc[0];
    const QPointF c1 = cornersScreenNdc[1];
    const QPointF c2 = cornersScreenNdc[2];
    const QPointF c3 = cornersScreenNdc[3];
    const QPointF centerNdc = (c0 + c1 + c2 + c3) * 0.25;
    // Local top edge midpoint (corners 2-3): +Y is visually up in canvas NDC.
    const QPointF topMidNdc = (c2 + c3) * 0.5;

    QPointF dirNdc = topMidNdc - centerNdc;
    const double lenNdc = std::hypot(dirNdc.x(), dirNdc.y());
    if (lenNdc < 1e-5)
        return false;
    dirNdc /= lenNdc;

    const double stemLenNdc = (2.0 * 18.0) / std::max(1, height());
    const QPointF handleNdc = topMidNdc + dirNdc * stemLenNdc;

    // Hit radius in pixels, kept >= the ~6px drawn circle so the hit area fully
    // covers the visual handle with a comfortable grab margin.
    const double radiusPx = 10.0;
    const QPointF d = screenPos - ndcToScreen(handleNdc);
    return (d.x() * d.x() + d.y() * d.y()) <= (radiusPx * radiusPx);
}

void CanvasView::alignLeft()        { doAlignLayer(0); }
void CanvasView::alignCenterH()     { doAlignLayer(1); }
void CanvasView::alignRight()       { doAlignLayer(2); }
void CanvasView::alignTop()         { doAlignLayer(3); }
void CanvasView::alignMiddleV()     { doAlignLayer(4); }
void CanvasView::alignBottom()      { doAlignLayer(5); }

void CanvasView::setAlignTarget(int target) { m_alignTarget = target; }

bool CanvasView::applyTransformFromPanel(const QTransform& newTransform)
{
    if (!m_doc || !m_controller) return false;
    if (m_doc->activeFlatIndex < 0 || m_doc->activeFlatIndex >= m_doc->flatCount())
        return false;
    auto* node = m_doc->activeNode();
    if (!node || !node->canTransform())
        return false;
    if (node->isPositionLocked()) return false;
    if (newTransform == node->transform()) return false;

    if (m_freeTransformActive) {
        m_controller->previewNodeTransform(node, newTransform);
        m_freeTransformDirty = true;
        emit activeTransformChanged();
        update();
        return true;
    }

    // Open a one-shot transform session mirroring beginFreeTransform()'s capture,
    // but WITHOUT its UX side effects (tool switch, overlay toggle): the panel is
    // a numeric editor, not a canvas gesture. commitFreeTransform() then applies
    // the per-type baking (text font size, shape re-render) and pushes one undo.
    m_freeTransformActive = true;
    m_freeTransformDirty = true;
    m_freeTransformFlatIndex = m_doc->activeFlatIndex;
    m_freeTransformOriginal = node->transform();
    m_freeTransformSessionIndices.clear();
    m_freeTransformSessionStartTransforms.clear();
    m_freeTransformSessionIndices.push_back(m_freeTransformFlatIndex);
    m_freeTransformSessionStartTransforms.push_back(node->transform());
    if (node->layer && node->layer->textData) {
        m_textLayerIndex = m_freeTransformFlatIndex;
        m_textBeforeSnapshot = *node->layer->textData;
        m_textBeforeTransform = node->transform();
    }

    m_controller->previewNodeTransform(node, newTransform);
    commitFreeTransform();
    return true;
}

bool CanvasView::activeIsSingleGroup() const
{
    if (!m_doc) return false;
    if (m_doc->selectedFlatIndices.size() > 1) return false;
    auto* node = m_doc->activeNode();
    return node && node->type == LayerTreeNode::Type::Group;
}

QTransform CanvasView::groupFrameTransform(const LayerTreeNode* group, bool* ok) const
{
    if (ok) *ok = false;
    if (!group) return {};

    const QTransform accum = group->accumulatedTransform();
    bool invertible = false;
    const QTransform accumInv = accum.inverted(&invertible);
    if (!invertible) return {};

    // AABB of every visible leaf descendant, expressed in the group's LOCAL
    // space (world corners mapped back through the group's accumulated transform).
    // This local box is invariant while only the group node's transform changes,
    // so mapping it back out by `accum` produces a frame that rotates/scales with
    // the group and always matches the children — no cache, no staleness.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    bool any = false;
    std::function<void(const LayerTreeNode*)> visit = [&](const LayerTreeNode* n) {
        if (!n || !n->isVisible()) return;
        if (n->type == LayerTreeNode::Type::Layer && n->layer) {
            const QPolygonF wc = TransformController::cornersFromNode(n);
            for (const QPointF& cp : wc) {
                const QPointF lp = accumInv.map(cp);
                minX = std::min(minX, static_cast<float>(lp.x()));
                maxX = std::max(maxX, static_cast<float>(lp.x()));
                minY = std::min(minY, static_cast<float>(lp.y()));
                maxY = std::max(maxY, static_cast<float>(lp.y()));
                any = true;
            }
            return;
        }
        for (const auto& child : n->children)
            visit(child.get());
    };
    visit(group);
    if (!any) return {};

    const float lhw = (maxX - minX) * 0.5f;
    const float lhh = (maxY - minY) * 0.5f;
    const QPointF lc((minX + maxX) * 0.5, (minY + maxY) * 0.5);
    // Local-frame (unit square → local AABB) then out to world via accum.
    const QTransform localFrame = TransformController::compose(lhw, lhh, lc, 0.0f);
    if (ok) *ok = true;
    return localFrame * accum;
}

QPolygonF CanvasView::groupUnionCorners(const LayerTreeNode* group) const
{
    bool ok = false;
    const QTransform t = groupFrameTransform(group, &ok);
    if (!ok) return {};
    return TransformController::cornersFromTransform(t);
}

std::vector<int> CanvasView::collectTransformableSelectedIndices(bool requireLayer)
{
    std::vector<int> result;
    if (!m_doc) return result;

    QSet<LayerTreeNode*> selNodes;
    for (int idx : m_doc->selectedFlatIndices) {
        auto* mn = m_doc->nodeAt(idx);
        if (mn && mn->canTransform())
            selNodes.insert(mn);
    }

    for (int idx : m_doc->selectedFlatIndices) {
        auto* mn = m_doc->nodeAt(idx);
        if (!mn) continue;
        if (mn->isPositionLocked()) continue;
        if (requireLayer && !mn->layer) continue;

        bool ancestorSelected = false;
        for (auto* p = mn->parent; p; p = p->parent) {
            if (selNodes.contains(p)) { ancestorSelected = true; break; }
        }
        if (ancestorSelected) continue;

        result.push_back(idx);
    }
    return result;
}

void CanvasView::invalidateMultiOutlineCache()
{
    m_multiResizeGroupBboxValid = false;
    m_multiResizeGroupSelection.clear();
}

std::vector<int> CanvasView::multiSelectionSignature() const
{
    std::vector<int> signature;
    if (!m_doc) return signature;

    signature.reserve(m_doc->selectedFlatIndices.size());
    for (int idx : m_doc->selectedFlatIndices)
        signature.push_back(idx);
    std::sort(signature.begin(), signature.end());
    return signature;
}

bool CanvasView::multiOutlineMatchesSelection() const
{
    // Multi-selection only. A single active group does NOT use this cached-outline
    // path — its frame is recomputed fresh each frame (groupFrameTransform).
    return m_multiResizeGroupBboxValid
        && m_doc
        && m_doc->selectedFlatIndices.size() > 1
        && m_multiResizeGroupSelection == multiSelectionSignature();
}

void CanvasView::captureMultiResizeStartTransforms()
{
    if (!m_doc) return;
    m_multiResizeIndices.clear();
    m_multiResizeStartTransforms.clear();
    m_multiResizeStartParentAccums.clear();

    // Groups included (requireLayer=false) — they resize/rotate as units via
    // the same world-delta application the move path already uses.
    auto indices = collectTransformableSelectedIndices(false);
    for (int idx : indices) {
        auto* mn = m_doc->nodeAt(idx);
        if (!mn) continue;
        m_multiResizeIndices.push_back(idx);
        m_multiResizeStartTransforms.push_back(mn->transform());
        QTransform parentAccum;
        for (auto* p = mn->parent; p; p = p->parent)
            parentAccum = parentAccum * p->transform();
        m_multiResizeStartParentAccums.push_back(parentAccum);
    }
}

bool CanvasView::computeMultiSelectionBbox()
{
    m_multiResizeGroupBboxValid = false;
    m_multiResizeGroupSelection.clear();
    if (!m_doc || m_doc->selectedFlatIndices.size() <= 1)
        return false;

    // Axis-aligned union of every transformable participant — groups
    // contribute their aggregated child bounds (nodeAlignCorners), exactly
    // like the align tool, so a selection mixing layers and groups gets one
    // frame that covers everything the gesture will actually transform.
    float cMinX = 1e9f, cMaxX = -1e9f, cMinY = 1e9f, cMaxY = -1e9f;
    bool any = false;
    for (int idx : collectTransformableSelectedIndices(false)) {
        auto* sn = m_doc->nodeAt(idx);
        if (!sn) continue;
        const QPolygonF sc = nodeAlignCorners(sn);
        for (const QPointF& cp : sc) {
            const float px = static_cast<float>(cp.x());
            const float py = static_cast<float>(cp.y());
            if (px < cMinX) cMinX = px;
            if (px > cMaxX) cMaxX = px;
            if (py < cMinY) cMinY = py;
            if (py > cMaxY) cMaxY = py;
            any = true;
        }
    }
    if (!any)
        return false;

    m_multiResizeGroupBboxCenter = QPointF((cMinX + cMaxX) * 0.5f,
                                           (cMinY + cMaxY) * 0.5f);
    m_multiResizeGroupBboxHw = (cMaxX - cMinX) * 0.5f;
    m_multiResizeGroupBboxHh = (cMaxY - cMinY) * 0.5f;
    m_multiResizeGroupBboxRotation = 0.0f;
    m_multiResizeGroupSelection = multiSelectionSignature();
    m_multiResizeGroupBboxValid = true;
    return true;
}

// ── Eyedropper Tool ─────────────────────────────────────────

void CanvasView::eyedropperSample(QPointF screenPos, Qt::MouseButton button,
                                   Qt::KeyboardModifiers modifiers)
{
    if (!m_doc) return;

    QColor color = eyedropperSampleColor(screenPos);
    if (!color.isValid()) return;

    bool setBackground = (button == Qt::RightButton)
                         || (modifiers & Qt::ControlModifier);

    if (setBackground)
        emit backgroundColorSampled(color);
    else
        emit colorSampled(color);
}

QColor CanvasView::eyedropperSampleColor(QPointF screenPos)
{
    if (!m_doc || m_doc->size.isEmpty()) return {};

    if (m_eyedropperSampleMode == SampleMode::Composite) {
        QImage fb = grabFramebuffer();
        const qreal dpr = devicePixelRatioF();
        const QPointF fbPos(screenPos.x() * dpr, screenPos.y() * dpr);
        return ColorSamplerService::sampleCompositeFramebuffer(
            fb, fbPos, m_eyedropperSampleSize);
    }

    // Current Layer mode
    auto* node = m_doc->activeNode();
    auto* layer = m_doc->activeLayer();
    if (!node || !layer || !node->isVisible()) return {};
    if (node->type != LayerTreeNode::Type::Layer) return {};

    QPointF imgPos = screenToImage(screenPos, layer);
    int px = static_cast<int>(std::round(imgPos.x()));
    int py = static_cast<int>(std::round(imgPos.y()));

    int lw = layer->cpuImage.width();
    int lh = layer->cpuImage.height();
    if (px < 0 || py < 0 || px >= lw || py >= lh) return {};

    return ColorSamplerService::sampleLayer(
        layer, imgPos, m_eyedropperSampleSize);
}

// ── Shape Tool ────────────────────────────────────────────

void CanvasView::setShapeType(int type)
{
    m_shapeToolType = static_cast<ShapeToolMode>(type);
}

void CanvasView::setShapeFillColor(const QColor& c)
{
    m_shapeFillColor = c;
}

void CanvasView::setShapeFillEnabled(bool enabled)
{
    m_shapeFillEnabled = enabled;
}

void CanvasView::setShapeStrokeColor(const QColor& c)
{
    m_shapeStrokeColor = c;
}

void CanvasView::setShapeStrokeEnabled(bool enabled)
{
    m_shapeStrokeEnabled = enabled;
}

void CanvasView::setShapeStrokeWidth(float w)
{
    m_shapeStrokeWidth = std::max(0.0f, w);
}

void CanvasView::setShapeOpacity(float opacity)
{
    m_shapeOpacity = std::clamp(opacity, 0.0f, 1.0f);
}

void CanvasView::setShapeCornerRadius(float r)
{
    m_shapeCornerRadius = std::max(0.0f, r);
}

void CanvasView::setShapeSides(int s)
{
    m_shapeSides = std::max(3, s);
}

void CanvasView::setShapeAntiAlias(bool aa)
{
    m_shapeAntiAlias = aa;
}

void CanvasView::setCustomShapeIcon(const ShapeIconInfo& icon)
{
    m_customShapeTemplateValid = false;
    m_customShapeError.clear();

    SvgShapeConverter converter;
    const SvgShapeConversionResult converted = converter.convertFromResource(icon.resourcePath);
    if (!converted.success) {
        m_customShapeError = converted.errorMessage;
        if (m_controller)
            emit m_controller->operationBlocked(m_customShapeError);
        return;
    }

    m_customShapeTemplate = converted.shapeData;
    m_customShapeTemplate.metadata.parameters.insert(QStringLiteral("sourceIconId"), icon.id);
    m_customShapeTemplate.metadata.parameters.insert(QStringLiteral("sourceIconName"), icon.name);
    m_customShapeTemplate.metadata.parameters.insert(QStringLiteral("sourceIconCategory"), icon.category);
    m_customShapeTemplate.metadata.parameters.insert(QStringLiteral("sourceResourcePath"), icon.resourcePath);
    m_customShapeTemplate.metadata.parametricEditable = false;
    m_customShapeTemplateValid = true;
    m_shapeToolType = ShapeToolMode::CustomShape;
}

void CanvasView::beginShapeDrag(QPointF ndcStart)
{
    if (m_shapeToolType == ShapeToolMode::CustomShape && !m_customShapeTemplateValid) {
        if (m_controller) {
            emit m_controller->operationBlocked(
                m_customShapeError.isEmpty()
                    ? tr("Select a custom shape first.")
                    : m_customShapeError);
        }
        return;
    }

    m_shapeDragging = true;
    m_shapeStartNdc = ndcStart;
    m_shapeCurrentNdc = ndcStart;
    if (m_shapePreviewOverlay)
        m_shapePreviewOverlay->finish();
}

void CanvasView::updateShapeDrag(QPointF ndcCurrent, Qt::KeyboardModifiers mods)
{
    if (!m_shapeDragging) return;
    m_shapeCurrentNdc = ndcCurrent;

    QPointF start = m_shapeStartNdc;
    QPointF current = ndcCurrent;
    bool shift = mods & Qt::ShiftModifier;
    bool alt = mods & Qt::AltModifier;

    if (m_shapeToolType == ShapeToolMode::Line) {
        if (shift) {
            QPointF d = current - start;
            double angle = std::atan2(d.y(), d.x());
            double snapped = std::round(angle / (M_PI / 4.0)) * (M_PI / 4.0);
            double len = std::hypot(d.x(), d.y());
            current = start + QPointF(len * std::cos(snapped), len * std::sin(snapped));
        }
    } else if (m_shapeToolType == ShapeToolMode::Polygon) {
        const QPointF center = start;
        QPointF d = current - start;
        double radius = std::hypot(d.x(), d.y());
        if (shift) {
            double angle = std::atan2(d.y(), d.x());
            double snapped = std::round(angle / (M_PI / 12.0)) * (M_PI / 12.0);
            current = start + QPointF(radius * std::cos(snapped), radius * std::sin(snapped));
        }
        radius = std::hypot((current - start).x(), (current - start).y());
        start = center - QPointF(radius, radius);
        current = center + QPointF(radius, radius);
    } else {
        QPointF topLeft = start;
        QPointF bottomRight = current;

        if (alt) {
            // Draw from center
            topLeft = start - (current - start);
            bottomRight = current;
        }

        if (shift) {
            QSizeF size(bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y());
            float s = std::max(std::abs(static_cast<float>(size.width())),
                               std::abs(static_cast<float>(size.height())));
            float sx = size.width() >= 0 ? s : -s;
            float sy = size.height() >= 0 ? s : -s;
            bottomRight = QPointF(topLeft.x() + sx, topLeft.y() + sy);
        }

        QRectF r(topLeft, bottomRight);
        r = r.normalized();
        start = r.topLeft();
        current = r.bottomRight();
    }

    QColor fill = m_shapeFillColor;
    fill.setAlphaF(fill.alphaF() * m_shapeOpacity);
    QColor stroke = m_shapeStrokeColor;
    stroke.setAlphaF(stroke.alphaF() * m_shapeOpacity);
    m_shapePreviewData = makeShapeDataFromDrag(m_shapeToolType,
                                               start,
                                               current,
                                               fill,
                                               stroke,
                                               m_shapeStrokeWidth,
                                               m_shapeCornerRadius,
                                               m_shapeSides,
                                               m_shapeAntiAlias,
                                               m_shapeToolType == ShapeToolMode::CustomShape && m_customShapeTemplateValid
                                                   ? &m_customShapeTemplate
                                                   : nullptr,
                                               m_doc ? m_doc->size : QSize());
    m_shapePreviewData.style.fillEnabled = m_shapeFillEnabled;
    m_shapePreviewData.style.strokeEnabled = m_shapeStrokeEnabled;
    if (m_shapeToolType == ShapeToolMode::Line) {
        m_shapePreviewData.style.fillEnabled = false;
    }

    if (m_shapePreviewOverlay) {
        const float hx = static_cast<float>(m_canvasHalfExtents.x());
        const float hy = static_cast<float>(m_canvasHalfExtents.y());
        QTransform xf;
        xf.setMatrix(width() * 0.5 * hx * m_doc->zoom, 0.0, 0.0,
                     0.0, -height() * 0.5 * hy * m_doc->zoom, 0.0,
                     (m_doc->panOffset.x() + 1.0) * width() * 0.5,
                     (1.0 - m_doc->panOffset.y()) * height() * 0.5,
                     1.0);
        const qreal strokePx = m_shapePreviewData.style.strokeEnabled
            ? m_shapePreviewData.style.strokeWidth * hx * m_doc->zoom * width() * 0.5
            : 0.0;
        m_shapePreviewOverlay->setPreview(
            xf.map(ShapeRenderer::pathForShape(m_shapePreviewData)),
            m_shapePreviewData.style.fillEnabled ? m_shapePreviewData.style.fillColor : Qt::transparent,
            m_shapePreviewData.style.strokeEnabled ? m_shapePreviewData.style.strokeColor : Qt::transparent,
            strokePx,
            m_shapeAntiAlias);
    }

    update();
}

void CanvasView::endShapeDrag()
{
    if (!m_shapeDragging) return;
    updateShapeDrag(m_shapeCurrentNdc, QApplication::keyboardModifiers());
    ShapeData data = m_shapePreviewData;
    m_shapeDragging = false;
    if (m_shapePreviewOverlay)
        m_shapePreviewOverlay->finish();

    if (m_controller)
        m_controller->createShapeLayer(data);

    update();
}

void CanvasView::cancelShapeDrag()
{
    m_shapeDragging = false;
    if (m_shapePreviewOverlay)
        m_shapePreviewOverlay->finish();
    update();
}

void CanvasView::renderShapePreview(QPainter& p, const QMatrix4x4& vm, float zoom)
{
    if (!m_shapeDragging) return;
    if (!m_doc || m_doc->size.isEmpty()) return;

    p.setRenderHint(QPainter::Antialiasing, m_shapeAntiAlias);

    double cw = m_doc->size.width();
    double ch = m_doc->size.height();

    // Build painter transform: screen pixel -> NDC
    QTransform xf;
    xf.scale(2.0 / width(), -2.0 / height());
    xf.translate(-width() * 0.5, -height() * 0.5);
    xf.scale(1.0 / zoom, 1.0 / zoom);
    xf.translate(-m_doc->panOffset.x(), -m_doc->panOffset.y());

    QPainterPath path = ShapeRenderer::pathForShape(m_shapePreviewData);

    // Apply view + canvas aspect transform to path
    QTransform canvasXf;
    canvasXf.scale(m_canvasHalfExtents.x(), m_canvasHalfExtents.y());
    path = canvasXf.map(path);

    p.save();
    p.setTransform(xf);

    if (m_shapePreviewData.path.closed && m_shapePreviewData.style.fillEnabled) {
        QColor fill = m_shapePreviewData.style.fillColor;
        fill.setAlpha(fill.alpha() / 2); // semi-transparent preview
        p.fillPath(path, fill);
    }
    if (m_shapePreviewData.style.strokeEnabled && m_shapePreviewData.style.strokeWidth > 0.0) {
        QPen pen(m_shapePreviewData.style.strokeColor);
        pen.setWidthF(m_shapePreviewData.style.strokeWidth);
        pen.setStyle(Qt::DashLine);
        p.strokePath(path, pen);
    }

    p.restore();
}
