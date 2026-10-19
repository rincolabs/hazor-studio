// Shared, format-agnostic helpers for the PsdImportService_*.cpp sources:
// node-placement transform, blend mode mapping, descriptor colour decoding,
// lock flags, layer mask conversion and raw channel -> RGBA8 decoding.

#include "PsdImportService_Internal.hpp"

#include "core/LayerTreeNode.hpp"
#include "psd/Pixels.hpp"

#include <QCoreApplication>
#include <QImage>

#include <algorithm>

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

void applyLayerMask(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                    Layer& layer, const QRect& ownerRectPx)
{
    const psd::LayerMask* m = rec.layerMask();
    if (!m)
        return;

    const bool useReal = rec.channel(psd::kChannelRealUserMask) != nullptr && m->real.has_value();
    if (!useReal && rec.channel(psd::kChannelUserMask) == nullptr)
        return;

    const int16_t channelId = useReal ? psd::kChannelRealUserMask : psd::kChannelUserMask;
    const psd::Rect maskRect = useReal ? m->real->rect : m->rect;
    const uint8_t defaultColor = useReal ? m->real->background : m->defaultColor;
    const uint8_t flags = useReal ? m->real->flags : m->flags;

    const QSize ownerSize = ownerRectPx.size().isEmpty() ? QSize(1, 1) : ownerRectPx.size();
    QImage maskImg(ownerSize, QImage::Format_Grayscale8);
    maskImg.fill(defaultColor);

    if (!maskRect.isEmpty()) {
        const auto decoded = rec.decodeChannel(channelId, ctx.file.header.depth, ctx.file.header.version);
        const auto wh = maskRect.size();
        if (decoded.hasValue() && wh.hasValue()) {
            const auto bytes8 = psd::planeToU8(decoded.value(), ctx.file.header.depth, wh.value().first, wh.value().second);
            if (bytes8.hasValue()) {
                const int mw = static_cast<int>(wh.value().first);
                const int mh = static_cast<int>(wh.value().second);
                const int offX = maskRect.left - ownerRectPx.left();
                const int offY = maskRect.top - ownerRectPx.top();
                const bool invert = (flags & psd::LayerMask::kFlagInvert) != 0;
                const auto& src = bytes8.value();
                for (int y = 0; y < mh; ++y) {
                    const int dy = y + offY;
                    if (dy < 0 || dy >= maskImg.height())
                        continue;
                    uchar* dst = maskImg.scanLine(dy);
                    for (int x = 0; x < mw; ++x) {
                        const int dx = x + offX;
                        if (dx < 0 || dx >= maskImg.width())
                            continue;
                        uint8_t v = src[static_cast<size_t>(y) * mw + x];
                        if (invert) v = static_cast<uint8_t>(255 - v);
                        dst[dx] = v;
                    }
                }
            } else {
                ctx.warn(QCoreApplication::translate("PsdImportService",
                    "Layer \"%1\": its mask could not be decoded and was treated as empty.").arg(layerName));
            }
        } else if (!decoded.hasValue()) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its mask could not be decoded and was treated as empty.").arg(layerName));
        }
    }

    layer.maskImage = maskImg;
    layer.maskOrigin = QPoint(0, 0);
    layer.maskVisible = !m->disabled();
    layer.maskDensity = m->parameters && m->parameters->userDensity
        ? static_cast<float>(*m->parameters->userDensity) / 255.0f : 1.0f;
    layer.maskFeather = m->parameters && m->parameters->userFeather
        ? static_cast<float>(*m->parameters->userFeather) : 0.0f;
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
