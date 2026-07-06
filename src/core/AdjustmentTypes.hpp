#pragma once

#include <QImage>
#include <QPoint>
#include <QString>
#include <QVariantMap>
#include <array>
#include <vector>

// Non-destructive adjustment-layer payload, stored on
// LayerTreeNode::adjustment (Type::Adjustment nodes only). `type` selects the
// effect in the registry below; `params` holds per-adjustment parameters
// (empty for Grayscale, reserved for Brightness/Contrast, Levels, ...).
struct AdjustmentData {
    QString type;
    QVariantMap params;
};

// Registry + CPU implementation of the available adjustment effects.
//
// The CPU path (DocumentCompositor for Normal Mode,
// LayerTreeNode::computeEffectedImage for Single Layer Mode) and the GPU live
// path (GPUViewport adjustment shader, keyed by shaderId) must stay in sync so
// the settled projection and the live preview produce identical pixels. New
// adjustments are added by extending registry() + apply() here and the
// matching `uAdjustmentType` branch in the adjustment shader
// (GPUViewport_Shaders.cpp) — no pipeline changes required.
namespace adjustments {

struct AdjustmentInfo {
    QString id;            // persisted identifier ("grayscale")
    QString displayName;   // UI label ("Grayscale")
    int shaderId;          // uAdjustmentType in the GPU adjustment shader
};

const std::vector<AdjustmentInfo>& registry();
bool isKnown(const QString& id);
QString displayName(const QString& id);
int shaderId(const QString& id);   // -1 when unknown

// Builds the combined per-channel 256-entry LUTs for a "curves" adjustment
// (master applied first, then R/G/B). Returns false (LUTs untouched) for any
// other adjustment type. Shared by the CPU apply() below and the GPU live path
// (LayerCompositor uploads these into the adjustment shader's LUT texture) so
// both produce identical pixels.
bool buildCurveLuts(const AdjustmentData& data,
                    std::array<unsigned char, 256>& rLut,
                    std::array<unsigned char, 256>& gLut,
                    std::array<unsigned char, 256>& bLut);

// Builds the per-channel 256-entry transfer LUTs for a "colorbalance"
// adjustment (R = Cyan-Red, G = Magenta-Green, B = Yellow-Blue) and reports the
// Preserve Luminosity flag. Returns false (LUTs/flag untouched) for any other
// adjustment type. Shared by the CPU apply() below and the GPU live path
// (LayerCompositor uploads these into the adjustment shader's LUT texture) so
// both produce identical pixels. The luminosity-preserving pass is applied on
// top of these LUTs by apply() and the shader, not baked into the LUTs.
bool buildColorBalanceLuts(const AdjustmentData& data,
                           std::array<unsigned char, 256>& rLut,
                           std::array<unsigned char, 256>& gLut,
                           std::array<unsigned char, 256>& bLut,
                           bool& preserveLuminosity);

// Applies `data` in place to a 4-byte-per-pixel image (ARGB32[_Premultiplied]
// or RGBA8888[_Premultiplied]); alpha is never touched. Grayscale is a linear
// combination of the color channels, so it is correct on both premultiplied
// and straight-alpha content.
//
// `opacity` is the adjustment layer's opacity. `mask` (optional, Grayscale8)
// modulates the effect per pixel — white applies, black hides, following
// mix(1, mask, maskDensity) like the GPU mask shader. `maskTopLeft` is the
// target-image pixel that mask pixel (0,0) maps to; pixels outside the mask
// default to white (fully adjusted), matching the layer-mask convention.
void apply(QImage& image, const AdjustmentData& data, float opacity,
           const QImage& mask = QImage(),
           const QPoint& maskTopLeft = QPoint(0, 0),
           float maskDensity = 1.0f);

} // namespace adjustments
