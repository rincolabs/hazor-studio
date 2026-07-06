#include "BrushPreviewRenderer.hpp"
#include "BrushDab.hpp"
#include "DynamicsEvaluator.hpp"
#include "BrushInputState.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QByteArray>
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Low-pressure floor of the preview stroke's pressure envelope. The envelope
// tapers from this value at the stroke ends up to full pressure (1.0) in the
// middle, so pressure-driven dynamics (size/opacity/flow) are visible in the
// preview. A lower floor = wider swing = more obvious variation.
constexpr float kPreviewPressureFloor = 0.05f;

// Resolve an image-tip preset to a usable QImage (mirrors how MainWindow loads
// a preset's tip): prefer the in-memory image, then embedded base64, then path.
QImage resolveTipImage(const BrushSettings& s)
{
    if (!s.tipImage.isNull())
        return s.tipImage;
    if (!s.tipImageData.isEmpty()) {
        QImage img;
        if (img.loadFromData(QByteArray::fromBase64(s.tipImageData.toUtf8())))
            return img;
    }
    if (!s.tipImagePath.isEmpty()) {
        QImage img(s.tipImagePath);
        if (!img.isNull())
            return img;
    }
    return QImage();
}

// Build one dab sprite via the shared BrushDab rasterizer — the exact same code
// the canvas uses — so the preview is pixel-identical to a real stroke for tip
// shape, angle, roundness, flip, texture and dual brush. The preview supplies
// the resolved tip image and (for colour dynamics) the effective colour; with
// colour dynamics off it uses the theme ink so the stroke stays visible on the
// panel.
QImage buildDab(const DynamicDabParams& params, const BrushSettings& settings,
                const QImage& tipImage, const QColor& ink, QPointF center,
                QPointF& topLeftOut)
{
    const float radius = std::max(0.5f, params.effectiveSize);
    const int pad = 2;
    const int side = static_cast<int>(std::ceil(radius * 2.0f + pad * 2.0f));
    if (side <= 0)
        return QImage();

    const float cx = radius + pad;
    const float cy = radius + pad;
    topLeftOut = QPointF(-cx, -cy); // offset from dab centre to sprite top-left

    BrushSettings s = settings;
    s.tipImage = tipImage; // preset tips load from data/path into a local image
    // The effective colour already carries the (ink-based) colour-dynamics
    // variation; see renderStroke/renderTip where the base colour is set to ink.
    Q_UNUSED(ink);
    const QColor color = params.effectiveColor;

    QImage tex = settings.textureConfig.texture;
    if (!tex.isNull() && tex.format() != QImage::Format_Grayscale8)
        tex = tex.convertToFormat(QImage::Format_Grayscale8);

    // Anchor the texture to the preview-image coordinate space, exactly as the
    // canvas anchors it to layer space: the sprite's top-left in preview pixels is
    // passed as (left, top) so consecutive dabs reveal different, continuous parts
    // of the pattern. Passing 0,0 here (the old behaviour) made every dab sample
    // the pattern's fixed top-left corner — which, at high depth or in Subtract
    // mode, can be uniformly near-zero and blank the whole stroke/thumbnail.
    const QPointF origin = center + topLeftOut;
    const int left = static_cast<int>(std::floor(origin.x()));
    const int top = static_cast<int>(std::floor(origin.y()));

    return BrushDab::rasterize(params, s, color, left, top, cx, cy, side, tex);
}

void stampDab(QPainter& painter, const DynamicDabParams& params,
              const BrushSettings& settings, const QImage& tipImage,
              const QColor& ink, QPointF center)
{
    QPointF topLeft;
    const QImage dab = buildDab(params, settings, tipImage, ink, center, topLeft);
    if (dab.isNull())
        return;
    painter.drawImage(center + topLeft, dab);
}

} // namespace

namespace BrushPreviewRenderer {

QImage renderTip(const BrushSettings& settings, const QSize& size,
                 qreal devicePixelRatio, const QColor& ink)
{
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    QImage img(QSize(static_cast<int>(size.width() * dpr),
                     static_cast<int>(size.height() * dpr)),
               QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    if (img.isNull())
        return img;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const float boxW = static_cast<float>(size.width());
    const float boxH = static_cast<float>(size.height());
    const float maxRadius = std::max(2.0f, std::min(boxW, boxH) * 0.5f - 3.0f);

    // Show the tip at full strength so the shape reads clearly. The thumbnail is
    // an idealized icon of the tip shape, so the texture is dropped here: a sub-dab
    // texture sample carries no useful grain at icon scale and, in Subtract/high-
    // depth modes, could knock the whole icon to zero. The stroke preview keeps the
    // texture (anchored to preview space) to show the real grain.
    BrushSettings tip = settings;
    tip.opacity = 1.0f;
    tip.flow = 1.0f;
    tip.textureConfig.enabled = false;

    DynamicDabParams params;
    params.effectiveSize = maxRadius;
    params.effectiveOpacity = 1.0f;
    params.effectiveFlow = 1.0f;
    params.effectiveRoundness = 1.0f;
    params.effectiveColor = ink; // tip thumbnail uses the theme ink

    const QImage tipImage = resolveTipImage(tip);
    stampDab(p, params, tip, tipImage, ink, QPointF(boxW * 0.5f, boxH * 0.5f));
    p.end();
    return img;
}

QImage renderStroke(const BrushSettings& settings, const QSize& size,
                    qreal devicePixelRatio, const QColor& ink)
{
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    QImage img(QSize(static_cast<int>(size.width() * dpr),
                     static_cast<int>(size.height() * dpr)),
               QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    if (img.isNull())
        return img;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const float boxW = static_cast<float>(size.width());
    const float boxH = static_cast<float>(size.height());

    // Pick a preview radius that fits the strip vertically. Because spacing
    // (size*spacing) and scatter (amount*size) are proportional to size, the
    // dab *pattern* is scale invariant, so the preview faithfully reflects the
    // real brush behaviour at any display size.
    const float radius = std::max(1.5f, boxH * 0.20f);

    // Standard S-shaped stroke path — the same path for every preset, so only
    // the rendered result changes per brush. Two cubic Bézier humps (up then
    // down) read as a smooth horizontal "S" (conventional preview stroke shape), spanning
    // ~80% of the width and using the vertical space, without the tight curves
    // or loops that would distort the dab pattern.
    const float marginX = std::max(boxW * 0.10f, radius + 2.0f);
    const float pathW = std::max(1.0f, boxW - 2.0f * marginX);
    const float cy = boxH * 0.5f;
    // A two-hump cubic peaks at 0.75*amp; size amp so the peak plus the dab
    // radius leaves a ~2px safety margin from the top/bottom edges.
    const float amp = std::max(0.0f, (boxH * 0.5f - radius - 2.0f) / 0.75f);
    const float x0 = marginX;

    QPainterPath path;
    path.moveTo(x0, cy);
    path.cubicTo(x0 + pathW * 0.1667f, cy - amp,
                 x0 + pathW * 0.3333f, cy - amp,
                 x0 + pathW * 0.5f,    cy);
    path.cubicTo(x0 + pathW * 0.6667f, cy + amp,
                 x0 + pathW * 0.8333f, cy + amp,
                 x0 + pathW,           cy);

    BrushSettings s = settings;
    // Rescale the brush to the preview radius. size is a DIAMETER (the UI's
    // "N px"), so the preview dab diameter is 2×radius. Dab spacing and scatter
    // are already proportional to size, but the dual brush size is absolute, so
    // scale it by the same factor to keep the dual/primary ratio faithful.
    const float previewDiameter = radius * 2.0f;
    const float sizeScale = settings.size > 0.001f ? previewDiameter / settings.size : 1.0f;
    s.size = previewDiameter;
    s.dualBrushConfig.size = settings.dualBrushConfig.size * sizeScale;
    // Use the theme ink as the brush colour so the stroke is visible on the
    // panel; colour dynamics then varies around it and shows in the preview.
    s.color = ink;

    QImage tipImage = resolveTipImage(s);
    // The preview dab is small regardless of the brush's real size, so pre-shrink
    // a large source tip once instead of re-downscaling it on every dab.
    const int nominalSide = static_cast<int>(std::ceil(radius * 2.0f + 4.0f));
    if (!tipImage.isNull()
        && (tipImage.width() > nominalSide * 2 || tipImage.height() > nominalSide * 2)) {
        tipImage = tipImage.scaled(nominalSide, nominalSide,
                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    // Mirror the engine's spacing rule (incl. auto-spacing) so the preview's
    // dab density matches what a real stroke produces.
    float spacing;
    if (s.useAutoSpacing) {
        const float diam = s.size;   // size is a diameter
        const float v = diam < 1.0f ? diam : std::sqrt(diam);
        spacing = std::max(s.autoSpacingCoeff * v, 1.0f);
    } else {
        spacing = std::max(s.size * s.spacing, 1.0f);
    }
    const qreal pathLen = path.length();
    const int steps = std::max(1, static_cast<int>(pathLen / spacing));
    uint32_t seed = 1u;

    BrushInputState input;

    for (int i = 0; i <= steps; ++i) {
        const float t = steps > 0 ? static_cast<float>(i) / steps : 0.0f;
        // Walk the curve by arc length so dab spacing matches the real engine,
        // and take the tangent for the stroke direction (drives angle/direction
        // dynamics just like a real stroke).
        const qreal pct = pathLen > 0.0 ? path.percentAtLength(t * pathLen) : 0.0;
        const QPointF pos = path.pointAtPercent(pct);
        const QPointF ahead = path.pointAtPercent(std::min(1.0, pct + 0.01));
        input.direction = static_cast<float>(std::atan2(ahead.y() - pos.y(),
                                                        ahead.x() - pos.x()));
        if (input.direction < 0.0f)
            input.direction += 2.0f * kPi;
        // Natural pressure envelope (taper in/out) so pressure-driven dynamics
        // are visible, tapering in and out over the stroke. sin(t·π) is in
        // [0,1] over the stroke (0 at the ends, 1 in the middle), so this sweeps
        // from kPreviewPressureFloor up to full pressure and back.
        input.pressure = kPreviewPressureFloor
            + (1.0f - kPreviewPressureFloor) * std::sin(t * kPi);
        input.dabIndex = i;
        input.imagePos = pos;
        // Arc length so far, so a Distance-driven curve option is visible in the
        // preview the same way it ramps along a real stroke (time stays 0 — a
        // static thumbnail has no elapsed time).
        input.strokeDistance = static_cast<float>(t * pathLen);

        DynamicDabParams params = DynamicsEvaluator::evaluate(s, input, seed);
        stampDab(p, params, s, tipImage, ink, input.imagePos + params.scatterOffset);
        ++seed;
    }

    p.end();
    return img;
}

} // namespace BrushPreviewRenderer
