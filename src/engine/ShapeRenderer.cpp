#include "ShapeRenderer.hpp"
#include <QPainter>
#include <algorithm>
#include <cstdint>

QRectF ShapeRenderer::rasterBounds(const ShapeData& data)
{
    QPainterPath path = pathForShape(data);
    if (path.isEmpty())
        return QRectF();

    const qreal margin = static_cast<qreal>(
        (data.style.strokeEnabled ? std::max(0.0, data.style.strokeWidth) * 0.5 : 0.0) + 0.01);
    return path.boundingRect().adjusted(-margin, -margin, margin, margin);
}

QImage ShapeRenderer::render(const ShapeData& data, QSize canvasSize)
{
    if (canvasSize.isEmpty())
        return QImage();

    const double cw = canvasSize.width();
    const double ch = canvasSize.height();

    const QRectF bounds = rasterBounds(data);
    if (bounds.isEmpty())
        return QImage();

    // Convert NDC bounding box to pixel coordinates
    double pixelLeft   = (bounds.left() + 1.0) * 0.5 * cw;
    double pixelTop    = (1.0 - bounds.bottom()) * 0.5 * ch;
    double pixelRight  = (bounds.right() + 1.0) * 0.5 * cw;
    double pixelBottom = (1.0 - bounds.top()) * 0.5 * ch;

    int px = static_cast<int>(std::floor(pixelLeft));
    int py = static_cast<int>(std::floor(pixelTop));
    int pw = static_cast<int>(std::ceil(pixelRight - pixelLeft));
    int ph = static_cast<int>(std::ceil(pixelBottom - pixelTop));

    if (pw < 1 || ph < 1)
        return QImage();

    constexpr int kMaxShapeRenderDim = 8192;
    constexpr int64_t kMaxShapeRenderPixels = 64LL * 1024LL * 1024LL;
    if (pw > kMaxShapeRenderDim || ph > kMaxShapeRenderDim
        || static_cast<int64_t>(pw) * static_cast<int64_t>(ph) > kMaxShapeRenderPixels) {
        return QImage();
    }

    QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setRenderHint(QPainter::Antialiasing, data.style.antiAlias);

    // Set up transform: canvas NDC -> pixel within this cropped image.
    // Keep this explicit; chained QTransform operations are easy to get
    // subtly wrong and can make the cropped thumbnail look fully filled.
    QTransform xf;
    xf.setMatrix(cw * 0.5, 0.0, 0.0,
                 0.0, -ch * 0.5, 0.0,
                 cw * 0.5 - pixelLeft,
                 ch * 0.5 - pixelTop,
                 1.0);
    p.setTransform(xf);

    QPainterPath path = pathForShape(data);

    if (data.path.closed && data.style.fillEnabled) {
        p.fillPath(path, data.style.fillColor);
    }

    if (data.style.strokeEnabled && data.style.strokeWidth > 0.0) {
        QPen pen(data.style.strokeColor);
        pen.setWidthF(data.style.strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(data.path.closed ? Qt::SquareCap : Qt::RoundCap);
        p.strokePath(path, pen);
    }

    p.end();
    return img;
}
