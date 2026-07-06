#pragma once

#include <QRectF>
#include <set>

class Document;

namespace BoxSelection {

std::set<int> findLayersInRect(const Document* doc, const QRectF& canvasRect);

} // namespace BoxSelection
