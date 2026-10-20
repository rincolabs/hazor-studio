// Shared, format-agnostic helpers for the PsdImportService_*.cpp sources:
// node-placement transform, blend mode mapping, descriptor colour decoding,
// lock flags, layer mask conversion and raw channel -> RGBA8 decoding.

#include "PsdImportService_Internal.hpp"

#include "core/LayerTreeNode.hpp"
#include "psd/Pixels.hpp"

#include <QCoreApplication>
#include <QImage>

#include <algorithm>
#include <cmath>

namespace psdimport {

QTransform ndcTransformForPixelRect(const QRectF& rectPx, const QSize& docSize)
{
    const double docW = std::max(1, docSize.width());
    const double docH = std::max(1, docSize.height());
    const double scaleX = rectPx.width() / docW;
    const double scaleY = rectPx.height() / docH;
    const double tx = -1.0 + 2.0 * rectPx.left() / docW + scaleX;
    const double ty = 1.0 - 2.0 * rectPx.top() / docH - scaleY;
    QTransform t;
    t.setMatrix(scaleX, 0.0, 0.0, 0.0, scaleY, 0.0, tx, ty, 1.0);
    return t;
}

QTransform ndcTransformForImageMap(const QTransform& imagePxToDocPx, const QSize& imageSizePx,
                                   const QSize& docSize)
{
    const double w = std::max(1, imageSizePx.width());
    const double h = std::max(1, imageSizePx.height());
    const double docW = std::max(1, docSize.width());
    const double docH = std::max(1, docSize.height());

    // Image NDC ([-1,1], Y up) -> image pixels (Y down), then document pixels
    // -> document NDC: the two halves of the convention
    // ndcTransformForPixelRect() documents above, with the caller's own
    // pixel-space placement in between.
    QTransform ndcToImgPx;
    ndcToImgPx.setMatrix(w / 2.0, 0.0, 0.0,
                         0.0, -h / 2.0, 0.0,
                         w / 2.0, h / 2.0, 1.0);
    QTransform docPxToNdc;
    docPxToNdc.setMatrix(2.0 / docW, 0.0, 0.0,
                         0.0, -2.0 / docH, 0.0,
                         -1.0, 1.0, 1.0);
    return ndcToImgPx * imagePxToDocPx * docPxToNdc;
}

QRect currentImageRectPx(const LayerTreeNode& node, const QSize& docSize)
{
    const double docW = std::max(1, docSize.width());
    const double docH = std::max(1, docSize.height());
    const QTransform& t = node.transform;
    const double left = (t.m31() - t.m11() + 1.0) * docW / 2.0;
    const double top = (1.0 - t.m22() - t.m32()) * docH / 2.0;
    const double width = t.m11() * docW;
    const double height = t.m22() * docH;
    return QRect(static_cast<int>(std::lround(left)), static_cast<int>(std::lround(top)),
                 static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
}

BlendMode mapBlendMode(const psd::BlendMode& mode, Context& ctx, const QString& layerName)
{
    switch (mode.kind()) {
    case psd::BlendMode::Kind::Normal: return BlendMode::Normal;
    case psd::BlendMode::Kind::Darken: return BlendMode::Darken;
    case psd::BlendMode::Kind::Multiply: return BlendMode::Multiply;
    case psd::BlendMode::Kind::ColorBurn: return BlendMode::ColorBurn;
    case psd::BlendMode::Kind::Lighten: return BlendMode::Lighten;
    case psd::BlendMode::Kind::Screen: return BlendMode::Screen;
    case psd::BlendMode::Kind::ColorDodge: return BlendMode::ColorDodge;
    case psd::BlendMode::Kind::Overlay: return BlendMode::Overlay;
    case psd::BlendMode::Kind::SoftLight: return BlendMode::SoftLight;
    case psd::BlendMode::Kind::HardLight: return BlendMode::HardLight;
    case psd::BlendMode::Kind::Difference: return BlendMode::Difference;
    case psd::BlendMode::Kind::Exclusion: return BlendMode::Exclusion;
    case psd::BlendMode::Kind::Hue: return BlendMode::Hue;
    case psd::BlendMode::Kind::Saturation: return BlendMode::Saturation;
    case psd::BlendMode::Kind::Color: return BlendMode::Color;
    case psd::BlendMode::Kind::Luminosity: return BlendMode::Luminosity;
    case psd::BlendMode::Kind::LinearBurn: return BlendMode::LinearBurn;
    case psd::BlendMode::Kind::LinearDodge: return BlendMode::LinearDodge;
    case psd::BlendMode::Kind::VividLight: return BlendMode::VividLight;
    case psd::BlendMode::Kind::LinearLight: return BlendMode::LinearLight;
    case psd::BlendMode::Kind::PinLight: return BlendMode::PinLight;
    case psd::BlendMode::Kind::HardMix: return BlendMode::HardMix;
    case psd::BlendMode::Kind::DarkerColor: return BlendMode::DarkerColor;
    case psd::BlendMode::Kind::LighterColor: return BlendMode::LighterColor;
    case psd::BlendMode::Kind::Subtract: return BlendMode::Subtract;
    case psd::BlendMode::Kind::Divide: return BlendMode::Divide;
    case psd::BlendMode::Kind::Dissolve: return BlendMode::Dissolve;
    default:
        break;
    }
    QString name;
    for (uint8_t c : mode.key())
        name.append(QChar(static_cast<ushort>(c)));
    ctx.warn(QCoreApplication::translate("PsdImportService",
        "Layer \"%1\": blend mode \"%2\" has no equivalent in Hazor; Normal was used instead.")
        .arg(layerName, name.trimmed()));
    return BlendMode::Normal;
}

void reportUnsupportedLayerProperties(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                                      bool clippingHonoured, bool fillOpacityHonoured)
{
    if (rec.fillOpacity() != 255 && !fillOpacityHonoured) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its Fill Opacity of %2%% was not applied — a group "
            "has no Fill of its own, only the layers inside it do.")
            .arg(layerName).arg(static_cast<int>(std::lround(rec.fillOpacity() * 100.0 / 255.0))));
    }

    // "Blend If": every layer carries the block, almost always at its neutral
    // 0/0/255/255 settings, so only a real restriction is worth a line.
    const auto ranges = rec.blendingRanges.ranges();
    const bool restricted = std::any_of(ranges.begin(), ranges.end(), [](const psd::BlendRange& r) {
        return r.source != std::array<uint8_t, 4>{0, 0, 255, 255}
            || r.dest != std::array<uint8_t, 4>{0, 0, 255, 255};
    });
    if (restricted) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its \"Blend If\" channel ranges have no equivalent in Hazor and were "
            "not applied, so the layer covers more than it did in the original file.").arg(layerName));
    }

    if (const auto* knko = rec.block(psdKey("knko")); knko && !knko->data.empty() && knko->data[0] != 0) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its Knockout setting has no equivalent in Hazor and was not applied.").arg(layerName));
    }

    if (rec.clipping != 0 && !clippingHonoured) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": it is clipped to the layer below it. Hazor can clip a layer or "
            "an adjustment — including to a group — but not a group as a whole, so this one "
            "was imported unclipped and covers more than it did in the original "
            "file.").arg(layerName));
    }
}

QColor colorFromDescriptor(const psd::Descriptor& d)
{
    auto num = [&](const char* key) -> std::optional<double> {
        const psd::Value* v = d.get(key);
        if (!v) return std::nullopt;
        switch (v->kind()) {
        case psd::Value::Kind::Double: return v->asDouble();
        case psd::Value::Kind::Integer: return static_cast<double>(v->asInteger());
        case psd::Value::Kind::UnitFloat: return v->asUnitFloat().value;
        default: return std::nullopt;
        }
    };
    auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };

    if (d.classId.is("RGBC")) {
        if (const auto r = num("redFloat"))
            return QColor::fromRgbF(clamp01(*r), clamp01(num("greenFloat").value_or(0.0)),
                                     clamp01(num("blueFloat").value_or(0.0)));
        return QColor::fromRgbF(clamp01(num("Rd  ").value_or(0.0) / 255.0),
                                 clamp01(num("Grn ").value_or(0.0) / 255.0),
                                 clamp01(num("Bl  ").value_or(0.0) / 255.0));
    }
    if (d.classId.is("CMYC")) {
        const double c = num("Cyn ").value_or(0.0) / 100.0;
        const double m = num("Mgnt").value_or(0.0) / 100.0;
        const double y = num("Ylw ").value_or(0.0) / 100.0;
        const double k = num("Blck").value_or(0.0) / 100.0;
        return QColor::fromRgbF(clamp01((1.0 - c) * (1.0 - k)), clamp01((1.0 - m) * (1.0 - k)),
                                 clamp01((1.0 - y) * (1.0 - k)));
    }
    if (d.classId.is("Grsc")) {
        const double g = clamp01(1.0 - num("Gry ").value_or(0.0) / 100.0);
        return QColor::fromRgbF(g, g, g);
    }
    if (const auto r = num("redFloat"))
        return QColor::fromRgbF(clamp01(*r), clamp01(num("greenFloat").value_or(0.0)),
                                 clamp01(num("blueFloat").value_or(0.0)));
    if (const auto r = num("Rd  "))
        return QColor::fromRgbF(clamp01(*r / 255.0), clamp01(num("Grn ").value_or(0.0) / 255.0),
                                 clamp01(num("Bl  ").value_or(0.0) / 255.0));
    return QColor(Qt::black);
}

int lockFlagsFromRecord(const psd::LayerRecord& rec)
{
    int flags = LockNone;
    if (rec.flags.transparencyProtected())
        flags |= LockTransparent;
    if (const auto* b = rec.block(psdKey("lspf"))) {
        if (const auto parsed = b->parsed(); parsed && parsed->hasValue()
            && parsed->value().kind() == psd::TaggedBlock::BlockData::Kind::Protection) {
            const uint32_t v = parsed->value().asProtection();
            if (v & 1) flags |= LockTransparent;
            if (v & 2) flags |= LockImage;
            if (v & 4) flags |= LockPosition;
        }
    }
    return flags;
}

namespace {

// The record's user mask decoded into its own document-pixel rect — the half
// of the conversion that does not depend on WHERE the mask has to land
// (applyLayerMask blits it, applyLayerMaskThroughTransform resamples it).
struct DecodedMask {
    const psd::LayerMask* mask = nullptr;
    QImage image;             // Grayscale8, `rectPx`-sized; null when that rect is empty
    QRect rectPx;             // document pixels
    uint8_t defaultColor = 0; // the value every pixel outside `rectPx` has
};

std::optional<DecodedMask> decodeUserMask(const psd::LayerRecord& rec, Context& ctx, const QString& layerName)
{
    const psd::LayerMask* m = rec.layerMask();
    if (!m)
        return std::nullopt;

    const bool useReal = rec.channel(psd::kChannelRealUserMask) != nullptr && m->real.has_value();
    if (!useReal && rec.channel(psd::kChannelUserMask) == nullptr)
        return std::nullopt;

    const int16_t channelId = useReal ? psd::kChannelRealUserMask : psd::kChannelUserMask;
    const psd::Rect maskRect = useReal ? m->real->rect : m->rect;
    const uint8_t flags = useReal ? m->real->flags : m->flags;

    DecodedMask out;
    out.mask = m;
    out.defaultColor = useReal ? m->real->background : m->defaultColor;
    out.rectPx = QRect(static_cast<int>(maskRect.left), static_cast<int>(maskRect.top),
                       static_cast<int>(maskRect.width()), static_cast<int>(maskRect.height()));
    if (maskRect.isEmpty())
        return out;

    const auto decoded = rec.decodeChannel(channelId, ctx.file.header.depth, ctx.file.header.version);
    const auto wh = maskRect.size();
    if (!decoded.hasValue()) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its mask could not be decoded and was treated as empty.").arg(layerName));
        return out;
    }
    if (!wh.hasValue())
        return out;

    const auto bytes8 = psd::planeToU8(decoded.value(), ctx.file.header.depth, wh.value().first, wh.value().second);
    if (!bytes8.hasValue()) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its mask could not be decoded and was treated as empty.").arg(layerName));
        return out;
    }

    const int mw = static_cast<int>(wh.value().first);
    const int mh = static_cast<int>(wh.value().second);
    const bool invert = (flags & psd::LayerMask::kFlagInvert) != 0;
    const auto& src = bytes8.value();
    QImage plane(mw, mh, QImage::Format_Grayscale8);
    for (int y = 0; y < mh; ++y) {
        uchar* dst = plane.scanLine(y);
        for (int x = 0; x < mw; ++x) {
            const uint8_t v = src[static_cast<size_t>(y) * mw + x];
            dst[x] = invert ? static_cast<uint8_t>(255 - v) : v;
        }
    }
    out.image = std::move(plane);
    return out;
}

void assignMask(Layer& layer, const DecodedMask& decoded, QImage maskImage)
{
    const psd::LayerMask& m = *decoded.mask;
    layer.maskImage = std::move(maskImage);
    layer.maskOrigin = QPoint(0, 0);
    layer.maskVisible = !m.disabled();
    layer.maskDensity = m.parameters && m.parameters->userDensity
        ? static_cast<float>(*m.parameters->userDensity) / 255.0f : 1.0f;
    layer.maskFeather = m.parameters && m.parameters->userFeather
        ? static_cast<float>(*m.parameters->userFeather) : 0.0f;
}

} // namespace

void applyLayerMask(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                    Layer& layer, const QRect& ownerRectPx)
{
    const auto decoded = decodeUserMask(rec, ctx, layerName);
    if (!decoded)
        return;

    const QSize ownerSize = ownerRectPx.size().isEmpty() ? QSize(1, 1) : ownerRectPx.size();
    QImage maskImg(ownerSize, QImage::Format_Grayscale8);
    maskImg.fill(decoded->defaultColor);

    if (!decoded->image.isNull()) {
        const QImage& plane = decoded->image;
        const int offX = decoded->rectPx.left() - ownerRectPx.left();
        const int offY = decoded->rectPx.top() - ownerRectPx.top();
        for (int y = 0; y < plane.height(); ++y) {
            const int dy = y + offY;
            if (dy < 0 || dy >= maskImg.height())
                continue;
            uchar* dst = maskImg.scanLine(dy);
            const uchar* src = plane.constScanLine(y);
            for (int x = 0; x < plane.width(); ++x) {
                const int dx = x + offX;
                if (dx < 0 || dx >= maskImg.width())
                    continue;
                dst[dx] = src[x];
            }
        }
    }

    assignMask(layer, *decoded, std::move(maskImg));
}

void applyLayerMaskThroughTransform(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                                    Layer& layer, const QSize& imageSizePx, const QTransform& imageToDocPx)
{
    const auto decoded = decodeUserMask(rec, ctx, layerName);
    if (!decoded)
        return;

    const QSize size = imageSizePx.isEmpty() ? QSize(1, 1) : imageSizePx;
    QImage maskImg(size, QImage::Format_Grayscale8);
    maskImg.fill(decoded->defaultColor);

    if (!decoded->image.isNull()) {
        // Walked per destination pixel and sampled bilinearly rather than
        // handed to QPainter: both ends are Grayscale8, which a painter path
        // would have to round-trip through ARGB32 to stay within the formats
        // Qt guarantees as paint devices.
        const QImage& plane = decoded->image;
        const double planeLeft = decoded->rectPx.left();
        const double planeTop = decoded->rectPx.top();
        const double fallback = decoded->defaultColor;
        const auto sample = [&](int x, int y) -> double {
            if (x < 0 || y < 0 || x >= plane.width() || y >= plane.height())
                return fallback; // outside the stored rect the mask IS defaultColor
            return plane.constScanLine(y)[x];
        };
        for (int y = 0; y < size.height(); ++y) {
            uchar* dst = maskImg.scanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                // Pixel centres both ways, so the resample lands on the same
                // grid convention ndcTransformForPixelQuad() places the image on.
                const QPointF docPt = imageToDocPx.map(QPointF(x + 0.5, y + 0.5));
                const double px = docPt.x() - planeLeft - 0.5;
                const double py = docPt.y() - planeTop - 0.5;
                const int x0 = static_cast<int>(std::floor(px));
                const int y0 = static_cast<int>(std::floor(py));
                const double fx = px - x0;
                const double fy = py - y0;
                const double upper = sample(x0, y0) * (1.0 - fx) + sample(x0 + 1, y0) * fx;
                const double lower = sample(x0, y0 + 1) * (1.0 - fx) + sample(x0 + 1, y0 + 1) * fx;
                const long value = std::lround(upper * (1.0 - fy) + lower * fy);
                dst[x] = static_cast<uchar>(std::clamp(value, 0L, 255L));
            }
        }
    }

    assignMask(layer, *decoded, std::move(maskImg));
}

QImage decodeLayerRgba(const psd::LayerRecord& rec, const psd::PsdFile& file, Context& ctx, const QString& layerName)
{
    if (rec.rect.isEmpty())
        return QImage();
    const auto index = [&]() -> std::optional<size_t> {
        const auto& layers = file.layers();
        for (size_t i = 0; i < layers.size(); ++i) {
            if (&layers[i] == &rec)
                return i;
        }
        return std::nullopt;
    }();
    if (!index)
        return QImage();
    const auto view = file.layer(*index);
    if (!view)
        return QImage();
    const auto result = view->rgba8();
    if (!result.hasValue()) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its pixels could not be decoded (%2); the layer was imported empty.")
            .arg(layerName, QString::fromStdString(result.error().message())));
        return QImage();
    }
    const psd::RgbaImage& img = result.value();
    if (img.width == 0 || img.height == 0)
        return QImage();
    QImage out(reinterpret_cast<const uchar*>(img.data.data()),
                static_cast<int>(img.width), static_cast<int>(img.height),
                static_cast<int>(img.width) * 4, QImage::Format_RGBA8888);
    return out.copy();
}

} // namespace psdimport

// ── Document channels (Alpha + Spot) ───────────────────────────
//
// The document's extra channels live in the MERGED Image Data Section, after
// the colour planes and after the merged transparency plane when there is one.
// Their metadata comes from the image resources, and the three that matter
// have to be read together because they are three parallel arrays indexed by
// the same channel position:
//
//   1006  alpha channel names, as Pascal strings
//   1045  the same names as Unicode, which wins when present
//   1053  alpha identifiers, preserved so a re-import re-associates them
//   1077  DisplayInfo: colour space + colour + opacity + mode per channel
//   1007  the obsolete DisplayInfo layout, read only when 1077 is absent
//
// Two things this must NOT do. It must not treat a LAYER's channels as
// document channels: a layer record's negative channel ids (-1 transparency,
// -2/-3 user and vector mask) are per-layer data and have nothing to do with
// this list. And it must not let a mismatch between the metadata arrays and
// the plane count shift the association — every lookup is bounds-checked per
// channel index, so a file with, say, fewer names than planes still imports
// every plane with the right colour and mode.

namespace psdimport {

namespace {

// One DisplayInfo record: what distinguishes a spot separation from a stored
// selection, and what carries a spot's ink colour.
struct ChannelDisplayInfo {
    uint16_t colorSpace = 0;
    std::array<uint16_t, 4> components{};
    uint16_t opacity = 100;
    uint8_t mode = 1;
};

// DisplayInfo mode values. 2 is a spot separation; 0 and 1 are stored
// selections differing in which side the overlay marks.
constexpr uint8_t kModeAlphaSelected = 0;
constexpr uint8_t kModeAlphaProtected = 1;
constexpr uint8_t kModeSpot = 2;

uint16_t readU16(std::span<const uint8_t> d, size_t at)
{
    if (at + 2 > d.size())
        return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(d[at]) << 8) | d[at + 1]);
}

uint32_t readU32(std::span<const uint8_t> d, size_t at)
{
    if (at + 4 > d.size())
        return 0;
    return (static_cast<uint32_t>(d[at]) << 24) | (static_cast<uint32_t>(d[at + 1]) << 16)
         | (static_cast<uint32_t>(d[at + 2]) << 8) | static_cast<uint32_t>(d[at + 3]);
}

std::vector<ChannelDisplayInfo> parseDisplayInfo(std::span<const uint8_t> d, bool modern)
{
    std::vector<ChannelDisplayInfo> out;
    // The modern record (1077) is prefixed by a 4-byte version and each entry
    // is 13 bytes; the obsolete one (1007) has no version and pads each entry
    // to 14 bytes.
    size_t at = modern ? 4 : 0;
    const size_t stride = modern ? 13 : 14;
    while (at + stride <= d.size()) {
        ChannelDisplayInfo info;
        info.colorSpace = readU16(d, at);
        for (int c = 0; c < 4; ++c)
            info.components[static_cast<size_t>(c)] = readU16(d, at + 2 + c * 2);
        info.opacity = readU16(d, at + 10);
        info.mode = d[at + 12];
        out.push_back(info);
        at += stride;
    }
    return out;
}

// Pascal strings, back to back (resource 1006).
std::vector<QString> parsePascalNames(std::span<const uint8_t> d)
{
    std::vector<QString> out;
    size_t at = 0;
    while (at < d.size()) {
        const size_t len = d[at];
        if (at + 1 + len > d.size())
            break;
        out.push_back(QString::fromLatin1(
            reinterpret_cast<const char*>(d.data() + at + 1), static_cast<int>(len)));
        at += 1 + len;
    }
    return out;
}

// Unicode strings, each a 4-byte character count followed by UTF-16BE
// (resource 1045). These win over 1006 so a non-Latin channel name survives.
std::vector<QString> parseUnicodeNames(std::span<const uint8_t> d)
{
    std::vector<QString> out;
    size_t at = 0;
    while (at + 4 <= d.size()) {
        const uint32_t count = readU32(d, at);
        at += 4;
        if (count == 0 || static_cast<uint64_t>(count) * 2 > d.size() - at) {
            if (count == 0)
                out.push_back(QString());
            break;
        }
        QString name;
        name.reserve(static_cast<int>(count));
        for (uint32_t i = 0; i < count; ++i) {
            const ushort unit = readU16(d, at + static_cast<size_t>(i) * 2);
            if (unit == 0)
                continue;  // trailing NUL the writer includes in the count
            name.append(QChar(unit));
        }
        at += static_cast<size_t>(count) * 2;
        out.push_back(name);
    }
    return out;
}

std::vector<int> parseAlphaIdentifiers(std::span<const uint8_t> d)
{
    std::vector<int> out;
    for (size_t at = 0; at + 4 <= d.size(); at += 4)
        out.push_back(static_cast<int>(readU32(d, at)));
    return out;
}

// 16-bit DisplayInfo components → a QColor. Only RGB (colour space 0) maps
// directly; anything else is approximated from the first three components and
// reported, rather than silently importing a wrong ink.
QColor displayInfoColor(const ChannelDisplayInfo& info, bool* exact)
{
    const auto to8 = [](uint16_t v) { return static_cast<int>(v >> 8); };
    if (exact)
        *exact = info.colorSpace == 0;
    return QColor(to8(info.components[0]), to8(info.components[1]),
                  to8(info.components[2]));
}

} // namespace

std::vector<DocumentChannel> importDocumentChannels(const psd::PsdFile& file,
                                                     const QSize& canvasSize,
                                                     QStringList& warnings)
{
    std::vector<DocumentChannel> channels;
    const psd::Header& header = file.header;
    const int colorChannels = header.colorMode.colorChannels().value_or(0);
    if (colorChannels <= 0 || canvasSize.isEmpty())
        return channels;

    // Plane order in the merged image: colour channels, the merged
    // transparency channel when the file has one, then the extra channels.
    const int extraStart = colorChannels + (file.mergedHasAlpha() ? 1 : 0);
    const int extraCount = static_cast<int>(header.channels) - extraStart;
    if (extraCount <= 0)
        return channels;

    const auto merged = file.decodeMerged();
    if (!merged.hasValue()) {
        warnings.append(QCoreApplication::translate("PsdImportService",
            "The document's alpha and spot channels could not be read."));
        return channels;
    }
    const std::vector<uint8_t>& planes = merged.value();

    const size_t rowBytes = header.rowBytes();
    const size_t planeBytes = rowBytes * header.height;
    if (planeBytes == 0
        || planes.size() < static_cast<size_t>(extraStart + extraCount) * planeBytes) {
        warnings.append(QCoreApplication::translate("PsdImportService",
            "The document's alpha and spot channel data was incomplete and was "
            "not imported."));
        return channels;
    }

    std::vector<QString> names;
    if (const auto* unicodeRes = file.resource(1045))
        names = parseUnicodeNames(unicodeRes->data);
    if (names.empty()) {
        if (const auto* pascalRes = file.resource(1006))
            names = parsePascalNames(pascalRes->data);
    }

    std::vector<int> identifiers;
    if (const auto* idRes = file.resource(1053))
        identifiers = parseAlphaIdentifiers(idRes->data);

    std::vector<ChannelDisplayInfo> display;
    if (const auto* modern = file.resource(1077))
        display = parseDisplayInfo(modern->data, /*modern=*/true);
    else if (const auto* legacy = file.resource(1007))
        display = parseDisplayInfo(legacy->data, /*modern=*/false);

    bool reportedColorSpace = false;
    const int width = canvasSize.width();
    const int height = canvasSize.height();
    const bool sixteenBit = header.depth == 16;

    for (int i = 0; i < extraCount; ++i) {
        const size_t planeOffset = static_cast<size_t>(extraStart + i) * planeBytes;

        QImage buffer(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y) {
            const uint8_t* src = planes.data() + planeOffset
                               + static_cast<size_t>(y) * rowBytes;
            uint8_t* dst = buffer.scanLine(y);
            for (int x = 0; x < width; ++x) {
                // 16-bit planes are big-endian; the high byte is the 8-bit
                // value, which is the precision the channel model carries.
                dst[x] = sixteenBit ? src[static_cast<size_t>(x) * 2] : src[x];
            }
        }

        DocumentChannel channel;
        channel.data = std::move(buffer);
        channel.visible = false;
        if (i < static_cast<int>(names.size()) && !names[static_cast<size_t>(i)].isEmpty())
            channel.name = names[static_cast<size_t>(i)];
        if (i < static_cast<int>(identifiers.size()))
            channel.externalId = identifiers[static_cast<size_t>(i)];

        if (i < static_cast<int>(display.size())) {
            const ChannelDisplayInfo& info = display[static_cast<size_t>(i)];
            bool exact = true;
            const QColor color = displayInfoColor(info, &exact);
            if (!exact && !reportedColorSpace) {
                warnings.append(QCoreApplication::translate("PsdImportService",
                    "A channel's colour was defined in a colour space Hazor Studio "
                    "does not support; it was approximated as RGB."));
                reportedColorSpace = true;
            }
            const float opacity = std::clamp(info.opacity / 100.0f, 0.0f, 1.0f);
            if (info.mode == kModeSpot) {
                channel.kind = ChannelKind::Spot;
                channel.inkColor = color;
                // The format stores a spot's solidity in the DisplayInfo
                // opacity field; it is a preview property, never the ink
                // coverage, which stays in the plane above.
                channel.solidity = opacity;
            } else {
                channel.kind = ChannelKind::Alpha;
                channel.overlayColor = color;
                channel.overlayOpacity = opacity;
                channel.representation = info.mode == kModeAlphaSelected
                    ? ChannelMaskRepresentation::SelectedAreas
                    : ChannelMaskRepresentation::MaskedAreas;
                Q_UNUSED(kModeAlphaProtected);
            }
        }
        if (channel.name.isEmpty()) {
            channel.name = channel.isSpot()
                ? QCoreApplication::translate("PsdImportService", "Spot Color %1").arg(i + 1)
                : QCoreApplication::translate("PsdImportService", "Alpha %1").arg(i + 1);
        }
        channels.push_back(std::move(channel));
    }

    return channels;
}

} // namespace psdimport
