// PSD adjustment/fill layer blocks -> Hazor's adjustment registry (curves,
// colorbalance, huesaturation, solidcolor — the only four Hazor has).
// Binary layouts follow the documented PSD/PSB adjustment-layer block
// format.

#include "PsdImportService_Internal.hpp"

#include "core/ColorBalanceData.hpp"
#include "core/CurvesData.hpp"
#include "core/HueSaturationData.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SolidColorData.hpp"
#include "psd/Descriptor.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QPoint>

#include <algorithm>
#include <array>
#include <cmath>

namespace psdimport {

namespace {

uint16_t be16(const std::vector<uint8_t>& d, size_t at)
{
    return at + 2 <= d.size() ? static_cast<uint16_t>((d[at] << 8) | d[at + 1]) : 0;
}
int16_t bei16(const std::vector<uint8_t>& d, size_t at) { return static_cast<int16_t>(be16(d, at)); }

curves::PointList identity() { return curves::identityPoints(); }

// `curv` (Curves block): pencil flag (1), version (2, must be 1 or
// 4), channel bitmask (4), then per set bit (0..31): either a 256-byte
// freehand map or a u16 point count + (output,input) u16 pairs.
bool parseCurves(const std::vector<uint8_t>& d, curves::CurvesData& out, bool& pencilResampled)
{
    pencilResampled = false;
    if (d.size() < 7)
        return false;
    const bool pencil = d[0] != 0;
    pencilResampled = pencil;
    const uint16_t version = be16(d, 1);
    if (version != 1 && version != 4)
        return false;
    const uint32_t bits = (uint32_t(d[3]) << 24) | (uint32_t(d[4]) << 16) | (uint32_t(d[5]) << 8) | uint32_t(d[6]);
    size_t at = 7;
    std::array<curves::PointList, 5> curvesOut = {identity(), identity(), identity(), identity(), identity()};
    bool any = false;
    for (int bit = 0; bit < 32; ++bit) {
        if ((bits & (1u << bit)) == 0)
            continue;
        curves::PointList pts;
        if (pencil) {
            if (at + 256 > d.size())
                break;
            if (bit < 5) {
                // Resample the 256-entry freehand map to a manageable set of
                // control points (exact per-pixel reproduction would need a
                // map-based curve model Hazor does not have).
                for (int x = 0; x <= 255; x += 8)
                    pts.push_back(QPoint(x, d[at + static_cast<size_t>(x)]));
                if (pts.empty() || pts.back().x() != 255)
                    pts.push_back(QPoint(255, d[at + 255]));
            }
            at += 256;
        } else {
            if (at + 2 > d.size())
                break;
            const uint16_t n = be16(d, at);
            at += 2;
            for (uint16_t i = 0; i < n; ++i) {
                if (at + 4 > d.size())
                    break;
                const uint16_t out8 = be16(d, at);
                const uint16_t in8 = be16(d, at + 2);
                at += 4;
                if (bit < 5)
                    pts.push_back(QPoint(std::clamp<int>(in8, 0, 255), std::clamp<int>(out8, 0, 255)));
            }
        }
        if (bit < 5 && pts.size() >= 2) {
            curvesOut[static_cast<size_t>(bit)] = curves::sanitize(pts);
            any = true;
        }
    }
    if (!any)
        return false;
    out.channels[static_cast<size_t>(curves::Channel::RGB)] = curvesOut[0];
    out.channels[static_cast<size_t>(curves::Channel::Red)] = curvesOut[1];
    out.channels[static_cast<size_t>(curves::Channel::Green)] = curvesOut[2];
    out.channels[static_cast<size_t>(curves::Channel::Blue)] = curvesOut[3];
    out.preset = QStringLiteral("Custom");
    return true;
}

// `blnc` (Color Balance block): shadows/midtones/highlights, each
// cyan-red/magenta-green/yellow-blue as an i16 (-100..100), then preserve
// luminosity (1 byte).
bool parseColorBalance(const std::vector<uint8_t>& d, colorbalance::ColorBalanceData& out)
{
    if (d.size() < 19)
        return false;
    for (int tone = 0; tone < 3; ++tone) {
        for (int axis = 0; axis < 3; ++axis) {
            const int v = std::clamp<int>(bei16(d, static_cast<size_t>(tone * 3 + axis) * 2), -100, 100);
            out.setValue(static_cast<colorbalance::Tone>(tone), static_cast<colorbalance::Axis>(axis), v);
        }
    }
    out.preserveLuminosity = d[18] != 0;
    return true;
}

// `hue2` (Hue/Saturation block): version(2) colorize-flag(1 byte at
// offset 2), master H/S/L at offset 4 (colorize) or 10 (not), then six
// 14-byte per-band records from offset 16 (4 i16 hue bounds + h/s/l i16),
// all of which Hazor models.
bool parseHueSaturation(const std::vector<uint8_t>& d, huesaturation::HueSaturationData& out)
{
    if (d.size() < 12)
        return false;
    out.colorize = d[2] != 0;
    const size_t base = out.colorize ? 4 : 10;
    if (base + 6 > d.size())
        return false;
    out.ranges[0] = huesaturation::HslValues{bei16(d, base), bei16(d, base + 2), bei16(d, base + 4)};
    static constexpr huesaturation::Range kBandOrder[6] = {
        huesaturation::Range::Reds, huesaturation::Range::Yellows, huesaturation::Range::Greens,
        huesaturation::Range::Cyans, huesaturation::Range::Blues, huesaturation::Range::Magentas,
    };
    for (int i = 0; i < 6; ++i) {
        const size_t at = 16 + static_cast<size_t>(i) * 14;
        if (at + 14 > d.size())
            break;
        out.ranges[static_cast<size_t>(kBandOrder[i])] =
            huesaturation::HslValues{bei16(d, at + 8), bei16(d, at + 10), bei16(d, at + 12)};

        // The four 2-byte bounds in front of those sliders are the band's hue
        // geometry (outer start, inner start, inner end, outer end; degrees
        // 0..360, wrapping for Reds) — which is exactly what Hazor's editable
        // HueRange models, so there is no reason to leave the band at its
        // default. They are unwrapped into the monotone frame HueRange::
        // weight() works in, then shifted so the band's centre lands in
        // [0, 360); a file that never refined its bands then reproduces
        // defaultBandRanges() to the degree.
        double bounds[4];
        for (int k = 0; k < 4; ++k)
            bounds[k] = bei16(d, at + static_cast<size_t>(k) * 2);
        for (int k = 1; k < 4; ++k) {
            while (bounds[k] < bounds[k - 1])
                bounds[k] += 360.0;
        }
        if (bounds[3] - bounds[0] <= 0.0)
            continue; // degenerate/absent geometry: keep Hazor's default band
        const double center = 0.5 * (bounds[1] + bounds[2]);
        const double shift = 360.0 * std::floor(center / 360.0);
        huesaturation::HueRange& band = out.band(kBandOrder[i]);
        band.enabled = true;
        band.centerHue = static_cast<float>(center - shift);
        band.outerStart = static_cast<float>(bounds[0] - shift);
        band.innerStart = static_cast<float>(bounds[1] - shift);
        band.innerEnd = static_cast<float>(bounds[2] - shift);
        band.outerEnd = static_cast<float>(bounds[3] - shift);
        band.supportsWrap = band.outerStart < 0.0f || band.outerEnd > 360.0f;
    }
    out.presetName = QStringLiteral("Custom");
    return true;
}

} // namespace

bool tryBuildAdjustment(const psd::LayerRecord& rec, Context& ctx, const QString& layerName, LayerTreeNode& node)
{
    if (const auto* b = rec.block(psdKey("curv"))) {
        curves::CurvesData cd;
        bool pencilResampled = false;
        if (parseCurves(b->data, cd, pencilResampled)) {
            node.adjustment = std::make_shared<AdjustmentData>();
            node.adjustment->type = QStringLiteral("curves");
            node.adjustment->params = cd.toParams();
            if (pencilResampled) {
                ctx.warn(QCoreApplication::translate("PsdImportService",
                    "Layer \"%1\": its Curves adjustment was drawn freehand (pencil mode), which "
                    "stores a value per input level; Hazor curves are control points, so it was "
                    "resampled and is only an approximation.").arg(layerName));
            }
        } else {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Curves settings could not be read; the layer was not imported.").arg(layerName));
            return false;
        }
    } else if (const auto* b = rec.block(psdKey("blnc"))) {
        colorbalance::ColorBalanceData cb;
        if (parseColorBalance(b->data, cb)) {
            node.adjustment = std::make_shared<AdjustmentData>();
            node.adjustment->type = QStringLiteral("colorbalance");
            node.adjustment->params = cb.toParams();
        } else {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Color Balance settings could not be read; the layer was not imported.").arg(layerName));
            return false;
        }
    } else if (const auto* b = rec.block(psdKey("hue2"))) {
        huesaturation::HueSaturationData hs;
        if (parseHueSaturation(b->data, hs)) {
            node.adjustment = std::make_shared<AdjustmentData>();
            node.adjustment->type = QStringLiteral("huesaturation");
            node.adjustment->params = hs.toParams();
        } else {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Hue/Saturation settings could not be read; the layer was not imported.").arg(layerName));
            return false;
        }
    } else if (const auto* b = rec.block(psdKey("SoCo"))) {
        const auto parsed = psd::VersionedDescriptor::parsePrefix(b->data);
        const psd::Value* clr = parsed.hasValue() ? parsed.value().first.descriptor.get("Clr ") : nullptr;
        if (clr && clr->kind() == psd::Value::Kind::DescriptorKind) {
            node.adjustment = std::make_shared<AdjustmentData>();
            node.adjustment->type = QStringLiteral("solidcolor");
            node.adjustment->params = solidcolor::SolidColorData::paramsFromColor(colorFromDescriptor(clr->asDescriptor()));
        } else {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Solid Color fill could not be read; the layer was not imported.").arg(layerName));
            return false;
        }
    } else {
        static const std::pair<const char*, const char*> kUnsupported[] = {
            {"levl", "Levels"}, {"hue ", "Hue/Saturation"}, {"brit", "Brightness/Contrast"},
            {"nvrt", "Invert"}, {"thrs", "Threshold"}, {"post", "Posterize"}, {"expA", "Exposure"},
            {"vibA", "Vibrance"}, {"mixr", "Channel Mixer"}, {"grdm", "Gradient Map"},
            {"phfl", "Photo Filter"}, {"selc", "Selective Color"}, {"blwh", "Black & White"},
            {"clrL", "Color Lookup"},
        };
        for (const auto& [key, label] : kUnsupported) {
            if (rec.block(psdKey(key))) {
                ctx.warn(QCoreApplication::translate("PsdImportService",
                    "Layer \"%1\": the %2 adjustment has no equivalent in Hazor and was not imported.")
                    .arg(layerName, QString::fromUtf8(label)));
                return false;
            }
        }
        return false;
    }

    node.layer = std::make_shared<Layer>();
    node.layer->name = layerName;
    node.layer->owner = &node;
    node.layer->cpuImage = QImage(ctx.docSize.isEmpty() ? QSize(1, 1) : ctx.docSize, QImage::Format_RGBA8888);
    node.layer->cpuImage.fill(Qt::transparent);
    return true;
}

void reclipAdjustmentToHost(LayerTreeNode& adjustmentNode, const LayerTreeNode& host, const QSize& docSize)
{
    if (!adjustmentNode.layer)
        return;
    const QRect hostRectPx = currentImageRectPx(host, docSize);
    const QSize size = hostRectPx.size().isEmpty() ? QSize(1, 1) : hostRectPx.size();

    QImage newCpu(size, QImage::Format_RGBA8888);
    newCpu.fill(Qt::transparent);

    Layer& layer = *adjustmentNode.layer;
    if (!layer.maskImage.isNull()) {
        // The mask was built document-sized (Normal Mode); re-crop it to the
        // host's own rect, in the host's local pixel space.
        QImage newMask(size, QImage::Format_Grayscale8);
        newMask.fill(255);
        const QRect src(hostRectPx.topLeft(), size);
        const QRect clipped = src.intersected(layer.maskImage.rect());
        if (!clipped.isEmpty()) {
            for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                const uchar* s = layer.maskImage.constScanLine(y);
                uchar* d = newMask.scanLine(y - src.top());
                for (int x = clipped.left(); x <= clipped.right(); ++x)
                    d[x - src.left()] = s[x];
            }
        }
        layer.maskImage = newMask;
        layer.maskOrigin = QPoint(0, 0);
    }
    layer.cpuImage = newCpu;
}

} // namespace psdimport
