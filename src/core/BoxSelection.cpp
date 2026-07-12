#include "BoxSelection.hpp"
#include "Document.hpp"
#include "LayerTreeNode.hpp"
#include "transform/TransformController.hpp"

#include <QPolygonF>

namespace BoxSelection {

std::set<int> findLayersInRect(const Document* doc, const QRectF& canvasRect)
{
    std::set<int> candidates;

    if (!doc) return candidates;

    auto flat = doc->flatten();
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        auto* node = flat[i];
        if (!node->isVisible()) continue;
        if (node->type != LayerTreeNode::Type::Layer) continue;
        if (!node->layer) continue;
        if (node->isPositionLocked()) continue;

        QPolygonF corners = TransformController::cornersFromNode(node);
        QRectF layerBounds = corners.boundingRect();

        if (canvasRect.intersects(layerBounds)) {
            candidates.insert(i);
        }
    }

    return candidates;
}

} // namespace BoxSelection
