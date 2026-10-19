// Vector mask (`vmsk`/`vsms`) + fill (`SoCo`) + stroke (`vstk`) -> Hazor's
// ShapeData, rasterized through the same ShapeLayerUpdater path a
// hand-drawn shape layer uses.
//
// Path-record layout implemented from the documented PSD/PSB path-record
// format ("Path resource format"): 26-byte records, each a 2-byte selector
// followed by 24 bytes of data.

#include "PsdImportService_Internal.hpp"

#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "engine/ShapeRenderer.hpp"
#include "shape/ShapeLayerUpdater.hpp"

#include <QCoreApplication>

#include <cstring>
#include <span>

namespace psdimport {

namespace {

struct Knot {
    QPointF pre, anchor, post;
};
struct Subpath {
    bool closed = true;
    std::vector<Knot> knots;
};

double fixed8_24(int32_t v) { return static_cast<double>(v) / 16777216.0; }

// A record's point `i` (0 = preceding control, 1 = anchor, 2 = leaving
// control), as fractions of the document's (width, height) mapped straight
// to canvas NDC.
QPointF recordPoint(const uint8_t* data24, int i)
{
    auto be32 = [&](size_t at) -> int32_t {
        return static_cast<int32_t>((uint32_t(data24[at]) << 24) | (uint32_t(data24[at + 1]) << 16)
                                     | (uint32_t(data24[at + 2]) << 8) | uint32_t(data24[at + 3]));
    };
    const double fy = fixed8_24(be32(static_cast<size_t>(i) * 8));
    const double fx = fixed8_24(be32(static_cast<size_t>(i) * 8 + 4));
    return QPointF(fx * 2.0 - 1.0, 1.0 - fy * 2.0);
}

std::vector<Subpath> parsePathRecords(std::span<const uint8_t> data)
{
    std::vector<Subpath> out;
    size_t remaining = 0;
    for (size_t off = 0; off + 26 <= data.size(); off += 26) {
        const uint16_t selector = static_cast<uint16_t>((data[off] << 8) | data[off + 1]);
        const uint8_t* body = data.data() + off + 2; // 24 bytes
        const bool isLength = selector == 0 || selector == 3;
        const bool isKnot = selector == 1 || selector == 2 || selector == 4 || selector == 5;
        if (isLength) {
            remaining = static_cast<uint16_t>((body[0] << 8) | body[1]);
            out.push_back(Subpath{selector == 0, {}});
        } else if (isKnot) {
            const bool closed = selector <= 2;
            if (remaining == 0 && (out.empty() || out.back().closed != closed))
                out.push_back(Subpath{closed, {}});
            if (remaining > 0)
                --remaining;
            if (!out.empty()) {
                Knot k;
                k.pre = recordPoint(body, 0);
                k.anchor = recordPoint(body, 1);
                k.post = recordPoint(body, 2);
                out.back().knots.push_back(k);
            }
        }
    }
    return out;
}

VectorPath pathFromSubpaths(const std::vector<Subpath>& subs)
{
    VectorPath vp;
    vp.closed = !subs.empty();
    for (const auto& s : subs) {
        if (s.knots.size() < 1)
            continue;
        PathCommand move;
        move.type = PathCommandType::MoveTo;
        move.p1 = s.knots.front().anchor;
        vp.commands.push_back(move);
        for (size_t i = 1; i < s.knots.size(); ++i) {
            PathCommand c;
            c.type = PathCommandType::CubicTo;
            c.p1 = s.knots[i - 1].post;
            c.p2 = s.knots[i].pre;
            c.p3 = s.knots[i].anchor;
            vp.commands.push_back(c);
        }
        if (s.closed && s.knots.size() >= 2) {
            PathCommand c;
            c.type = PathCommandType::CubicTo;
            c.p1 = s.knots.back().post;
            c.p2 = s.knots.front().pre;
            c.p3 = s.knots.front().anchor;
            vp.commands.push_back(c);
        }
        if (s.closed) {
            PathCommand close;
            close.type = PathCommandType::ClosePath;
            vp.commands.push_back(close);
        } else {
            vp.closed = false;
        }
    }
    return vp;
}

double unitToPx(const psd::Value* v, double dpi, double fallback)
{
    if (!v) return fallback;
    if (v->kind() == psd::Value::Kind::UnitFloat) {
        const auto& u = v->asUnitFloat();
        if (u.unit == std::array<uint8_t, 4>{'#', 'P', 'n', 't'})
            return u.value * dpi / 72.0;
        if (u.unit == std::array<uint8_t, 4>{'#', 'M', 'l', 'm'})
            return u.value * dpi / 25.4;
        return u.value;
    }
    if (v->kind() == psd::Value::Kind::Double) return v->asDouble();
    if (v->kind() == psd::Value::Kind::Integer) return static_cast<double>(v->asInteger());
    return fallback;
}

} // namespace

bool tryBuildShape(const psd::LayerRecord& rec, Context& ctx, const QString& layerName, LayerTreeNode& node, Document& scratchDoc)
{
    const psd::TaggedBlock* vecBlock = rec.block(psdKey("vsms"));
    if (!vecBlock) vecBlock = rec.block(psdKey("vmsk"));
    if (!vecBlock || vecBlock->data.size() < 8)
        return false;

    const uint32_t flags = (uint32_t(vecBlock->data[4]) << 24) | (uint32_t(vecBlock->data[5]) << 16)
        | (uint32_t(vecBlock->data[6]) << 8) | uint32_t(vecBlock->data[7]);
    const std::span<const uint8_t> body(vecBlock->data.data() + 8, vecBlock->data.size() - 8);
    const auto subpaths = parsePathRecords(body);
    VectorPath path = pathFromSubpaths(subpaths);
    if (path.commands.empty())
        return false;
    if (flags & 1) {
        // Inverted vector mask (reveals everything except the path): Hazor's
        // shape fill has no "inverted" concept, so this would render as the
        // opposite of the intended result.
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its shape path is inverted, which Hazor does not support; it was "
            "imported without the inversion.").arg(layerName));
    }

    // Multiple path components render with Hazor's single, uniform winding
    // rule; the format's per-component subtract/intersect/exclude operations
    // are not reproduced.
    const bool multipleComponents = subpaths.size() > 1;

    ShapeData shape;
    shape.path = path;
    shape.transform.localToCanvas = QTransform(); // path coordinates are already canvas NDC
    shape.style.antiAlias = true;

    const auto* soco = rec.block(psdKey("SoCo"));
    const auto* vstk = rec.block(psdKey("vstk"));

    bool fillEnabled = soco != nullptr;
    shape.style.fillColor = QColor(200, 200, 200, 255);
    if (soco) {
        const auto parsed = psd::VersionedDescriptor::parsePrefix(soco->data);
        if (parsed.hasValue()) {
            if (const psd::Value* clr = parsed.value().first.descriptor.get("Clr ");
                clr && clr->kind() == psd::Value::Kind::DescriptorKind) {
                shape.style.fillColor = colorFromDescriptor(clr->asDescriptor());
            }
        }
    }

    shape.style.strokeEnabled = false;
    if (vstk) {
        const auto parsed = psd::VersionedDescriptor::parsePrefix(vstk->data);
        if (parsed.hasValue()) {
            const psd::Descriptor& d = parsed.value().first.descriptor;
            if (const psd::Value* fe = d.get("fillEnabled"); fe && fe->kind() == psd::Value::Kind::Boolean)
                fillEnabled = fe->asBoolean();
            const psd::Value* se = d.get("strokeEnabled");
            if (!se || se->kind() != psd::Value::Kind::Boolean || se->asBoolean()) {
                const psd::Value* content = d.get("strokeStyleContent");
                QColor strokeColor(0, 0, 0, 255);
                bool solidStroke = true;
                if (content && content->kind() == psd::Value::Kind::DescriptorKind) {
                    const psd::Descriptor& cd = content->asDescriptor();
                    if (const psd::Value* clr = cd.get("Clr "); clr && clr->kind() == psd::Value::Kind::DescriptorKind) {
                        strokeColor = colorFromDescriptor(clr->asDescriptor());
                    } else {
                        solidStroke = false; // gradient/pattern stroke content
                    }
                }
                if (!solidStroke) {
                    ctx.warn(QCoreApplication::translate("PsdImportService",
                        "Layer \"%1\": its shape stroke uses a gradient or pattern, which Hazor's "
                        "shape stroke does not support; a solid color was used instead.").arg(layerName));
                }
                const double widthPx = unitToPx(d.get("strokeStyleLineWidth"), ctx.dpi, 1.0);
                shape.style.strokeEnabled = true;
                shape.style.strokeColor = strokeColor;
                shape.style.strokeWidth = ctx.docSize.width() > 0 ? widthPx * 2.0 / ctx.docSize.width() : 0.002;
            }
        }
    }
    shape.style.fillEnabled = fillEnabled;

    node.layer->shapeData = std::make_shared<ShapeData>(shape);
    if (!ShapeLayerUpdater::rebuildShapeRaster(scratchDoc, node)) {
        node.layer->shapeData.reset();
        return false;
    }

    if (multipleComponents) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its shape combines multiple path components (subtract/intersect/exclude); "
            "Hazor may not render it exactly as the original file intended.").arg(layerName));
    }
    return true;
}

} // namespace psdimport
