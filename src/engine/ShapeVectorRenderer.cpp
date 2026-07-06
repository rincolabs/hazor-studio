#include "ShapeVectorRenderer.hpp"
#include "shape/ShapePresetFactory.hpp"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

bool fuzzyPointEquals(const QPointF& a, const QPointF& b)
{
    return std::abs(a.x() - b.x()) < 0.0001
        && std::abs(a.y() - b.y()) < 0.0001;
}

bool fuzzyTransformEquals(const QTransform& a, const QTransform& b)
{
    return std::abs(a.m11() - b.m11()) < 0.0001
        && std::abs(a.m12() - b.m12()) < 0.0001
        && std::abs(a.m13() - b.m13()) < 0.0001
        && std::abs(a.m21() - b.m21()) < 0.0001
        && std::abs(a.m22() - b.m22()) < 0.0001
        && std::abs(a.m23() - b.m23()) < 0.0001
        && std::abs(a.m31() - b.m31()) < 0.0001
        && std::abs(a.m32() - b.m32()) < 0.0001
        && std::abs(a.m33() - b.m33()) < 0.0001;
}

} // namespace

QPainterPath ShapeVectorRenderer::buildPath(const VectorPath& vectorPath)
{
    QPainterPath painterPath;
    painterPath.setFillRule(vectorPath.fillRule);

    for (const auto& command : vectorPath.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
            painterPath.moveTo(command.p1);
            break;
        case PathCommandType::LineTo:
            painterPath.lineTo(command.p1);
            break;
        case PathCommandType::QuadTo:
            painterPath.quadTo(command.p1, command.p2);
            break;
        case PathCommandType::CubicTo:
            painterPath.cubicTo(command.p1, command.p2, command.p3);
            break;
        case PathCommandType::ClosePath:
            painterPath.closeSubpath();
            break;
        }
    }

    return painterPath;
}

QPainterPath ShapeVectorRenderer::buildPath(const ShapeData& data)
{
    return data.transform.localToCanvas.map(buildPath(data.path));
}

QRectF ShapeVectorRenderer::transformedBounds(const ShapeData& data,
                                               const QTransform& accumulatedTransform)
{
    QPainterPath path = buildPath(data);
    return accumulatedTransform.map(path).boundingRect();
}

static QRectF spriteBoundsForShape(const ShapeData& data)
{
    QPainterPath path = ShapeVectorRenderer::buildPath(data);
    QRectF bounds = path.boundingRect();
    const qreal padNdc = static_cast<qreal>(std::max(0.0, data.style.strokeWidth) * 0.5 + 0.01);
    return bounds.adjusted(-padNdc, -padNdc, padNdc, padNdc);
}

ShapeSprite ShapeVectorRenderer::renderSprite(const ShapeData& data,
                                               const QTransform& accumulatedTransform,
                                               QSize documentSize,
                                               float zoom,
                                               const QPointF& canvasHalfExtents,
                                               int viewportW,
                                               int viewportH)
{
    ShapeSprite result;
    Q_UNUSED(viewportW);
    Q_UNUSED(viewportH);

    if (documentSize.isEmpty())
        return result;

    const QRectF baseSpriteBounds = spriteBoundsForShape(data);
    QTransform baseXf;
    baseXf.setMatrix(
        baseSpriteBounds.width() * 0.5, 0.0, 0.0,
        0.0, baseSpriteBounds.height() * 0.5, 0.0,
        baseSpriteBounds.center().x(),
        baseSpriteBounds.center().y(),
        1.0);

    bool invertible = false;
    const QTransform invBaseXf = baseXf.inverted(&invertible);
    const QTransform userTransform = invertible
        ? (invBaseXf * accumulatedTransform)
        : QTransform();

    // For a parametric rounded rectangle, rebuild the LOCAL path so its corners
    // stay circular under the *effective* transform (resting localToCanvas
    // composed with the live preview delta in userTransform). This mirrors the
    // commit-time rebuild in transformShapeData() exactly, so the interactive
    // preview matches the applied result instead of momentarily distorting the
    // corners during the drag.
    const QTransform effective = data.transform.localToCanvas * userTransform;
    const QPointF canvasToPixelScale(
        std::max(1, documentSize.width()) * 0.5,
        std::max(1, documentSize.height()) * 0.5);
    const VectorPath localPath = ShapePresetFactory::isParametricRoundedRect(data)
        ? ShapePresetFactory::roundedRectPathFor(data, effective, canvasToPixelScale)
        : data.path;

    QPainterPath renderPath =
        userTransform.map(data.transform.localToCanvas.map(buildPath(localPath)));
    QRectF renderBounds = renderPath.boundingRect();
    if (data.path.commands.empty())
        return result;

    // Keep sprite bounds aligned with ShapeRenderer::render()/shapeLayerTransform
    // so cached shape textures map 1:1 with the layer transform.
    const float padNdc = static_cast<float>(std::max(0.0, data.style.strokeWidth) * 0.5 + 0.01);
    QRectF ndcBounds = renderBounds.adjusted(-static_cast<qreal>(padNdc),
                                             -static_cast<qreal>(padNdc),
                                             static_cast<qreal>(padNdc),
                                             static_cast<qreal>(padNdc));

    double docW = documentSize.width();
    double docH = documentSize.height();
    const float hx = canvasHalfExtents.x();
    const float hy = canvasHalfExtents.y();

    double pixelsPerNdcX = docW / (2.0 * hx);
    double pixelsPerNdcY = docH / (2.0 * hy);
    double basePPNdc = std::max(pixelsPerNdcX, pixelsPerNdcY);

    float maxRenderScale = std::max(1.0f, zoom);

    int spriteW = static_cast<int>(std::ceil(ndcBounds.width() * basePPNdc * maxRenderScale));
    int spriteH = static_cast<int>(std::ceil(ndcBounds.height() * basePPNdc * maxRenderScale));

    constexpr int kMaxSpriteDim = 4096;
    spriteW = std::clamp(spriteW, 1, kMaxSpriteDim);
    spriteH = std::clamp(spriteH, 1, kMaxSpriteDim);

    QImage img(spriteW, spriteH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, data.style.antiAlias);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QTransform ndcToPixels;
    ndcToPixels.setMatrix(
        spriteW / ndcBounds.width(), 0, 0,
        0, -spriteH / ndcBounds.height(), 0,
        -ndcBounds.left() * spriteW / ndcBounds.width(),
        spriteH + ndcBounds.top() * spriteH / ndcBounds.height(),
        1.0);
    p.setTransform(ndcToPixels);

    if (data.path.closed && data.style.fillEnabled) {
        p.fillPath(renderPath, data.style.fillColor);
    }

    if (data.style.strokeEnabled && data.style.strokeWidth > 0.0) {
        QPen pen(data.style.strokeColor);
        pen.setWidthF(data.style.strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(data.path.closed ? Qt::SquareCap : Qt::RoundCap);
        p.strokePath(renderPath, pen);
    }

    p.end();

    result.image = img.convertToFormat(QImage::Format_RGBA8888);
    QTransform spriteXf;
    float halfW = static_cast<float>(ndcBounds.width()) * 0.5f;
    float halfH = static_cast<float>(ndcBounds.height()) * 0.5f;
    spriteXf.setMatrix(
        halfW, 0.0, 0.0,
        0.0, halfH, 0.0,
        static_cast<float>(ndcBounds.center().x()),
        static_cast<float>(ndcBounds.center().y()),
        1.0);
    result.spriteTransform = spriteXf;
    return result;
}

bool ShapeVectorRenderer::sameShapeData(const ShapeData& a, const ShapeData& b)
{
    if (a.path.closed != b.path.closed) return false;
    if (a.path.fillRule != b.path.fillRule) return false;
    if (a.path.commands.size() != b.path.commands.size()) return false;
    if (a.style.fillEnabled != b.style.fillEnabled) return false;
    if (a.style.strokeEnabled != b.style.strokeEnabled) return false;
    if (a.style.antiAlias != b.style.antiAlias) return false;
    if (a.style.fillColor != b.style.fillColor) return false;
    if (a.style.strokeColor != b.style.strokeColor) return false;
    if (std::abs(a.style.strokeWidth - b.style.strokeWidth) > 0.00001) return false;
    if (!fuzzyTransformEquals(a.transform.localToCanvas, b.transform.localToCanvas)) return false;

    for (size_t i = 0; i < a.path.commands.size(); ++i) {
        const auto& ac = a.path.commands[i];
        const auto& bc = b.path.commands[i];
        if (ac.type != bc.type) return false;
        if (!fuzzyPointEquals(ac.p1, bc.p1)) return false;
        if (!fuzzyPointEquals(ac.p2, bc.p2)) return false;
        if (!fuzzyPointEquals(ac.p3, bc.p3)) return false;
    }
    return true;
}
