#include "smartobject/DocumentContainer.hpp"

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <deque>

namespace {

// Walks `nodes` and reports every Smart Object instance it finds. Used for both
// the reverse index and the dependency graph, so there is exactly one place
// that knows "an instance is a Layer node whose Layer carries smartObjectData".
template <typename Fn>
void forEachInstance(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes, Fn&& fn)
{
    for (const auto& node : nodes) {
        if (!node) continue;
        if (node->type == LayerTreeNode::Type::Layer && node->layer
            && node->layer->smartObjectData) {
            fn(node.get(), *node->layer->smartObjectData);
        }
        forEachInstance(node->children, fn);
    }
}

} // namespace

SmartObjectDocId DocumentContainer::generateDocId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

DocumentContainer::DocumentContainer(std::shared_ptr<Document> root)
    : DocumentContainer(std::move(root), generateDocId())
{}

DocumentContainer::DocumentContainer(std::shared_ptr<Document> root, SmartObjectDocId rootId)
    : m_rootId(rootId.isEmpty() ? generateDocId() : std::move(rootId))
{
    if (root)
        m_documents.insert(m_rootId, std::move(root));
}

std::shared_ptr<Document> DocumentContainer::document(const SmartObjectDocId& id) const
{
    return m_documents.value(id);
}

bool DocumentContainer::contains(const SmartObjectDocId& id) const
{
    return m_documents.contains(id);
}

std::vector<SmartObjectDocId> DocumentContainer::documentIds() const
{
    std::vector<SmartObjectDocId> out;
    out.reserve(m_documents.size());
    for (auto it = m_documents.constBegin(); it != m_documents.constEnd(); ++it)
        out.push_back(it.key());
    // QHash iteration order is unspecified; a stable order keeps save output and
    // any diff of it reproducible.
    std::sort(out.begin(), out.end());
    return out;
}

SmartObjectDocId DocumentContainer::addDocument(std::shared_ptr<Document> doc)
{
    if (!doc)
        return {};
    SmartObjectDocId id = generateDocId();
    while (m_documents.contains(id))
        id = generateDocId();
    m_documents.insert(id, std::move(doc));
    return id;
}

bool DocumentContainer::addDocumentWithId(const SmartObjectDocId& id,
                                          std::shared_ptr<Document> doc)
{
    if (id.isEmpty() || !doc || m_documents.contains(id))
        return false;
    m_documents.insert(id, std::move(doc));
    return true;
}

std::shared_ptr<Document> DocumentContainer::replaceDocument(const SmartObjectDocId& id,
                                                             std::shared_ptr<Document> doc)
{
    if (id.isEmpty() || !doc)
        return nullptr;
    auto it = m_documents.find(id);
    if (it == m_documents.end())
        return nullptr;
    std::shared_ptr<Document> previous = it.value();
    it.value() = std::move(doc);
    return previous;
}

std::vector<SmartObjectDocId> DocumentContainer::directReferences(const Document& doc) const
{
    std::vector<SmartObjectDocId> out;
    forEachInstance(doc.roots, [&](LayerTreeNode*, const SmartObjectData& data) {
        if (data.mode == SmartObjectStorageMode::Embedded && !data.docId.isEmpty())
            out.push_back(data.docId);
    });
    return out;
}

std::vector<LayerTreeNode*> DocumentContainer::instancesOf(const SmartObjectDocId& id) const
{
    std::vector<LayerTreeNode*> out;
    if (id.isEmpty())
        return out;
    for (auto it = m_documents.constBegin(); it != m_documents.constEnd(); ++it) {
        if (!it.value()) continue;
        forEachInstance(it.value()->roots, [&](LayerTreeNode* node, const SmartObjectData& data) {
            if (data.mode == SmartObjectStorageMode::Embedded && data.docId == id)
                out.push_back(node);
        });
    }
    return out;
}

bool DocumentContainer::canReference(const SmartObjectDocId& from,
                                     const SmartObjectDocId& to) const
{
    if (from.isEmpty() || to.isEmpty())
        return false;
    // A document displaying itself is the degenerate cycle.
    if (from == to)
        return false;
    if (!m_documents.contains(from) || !m_documents.contains(to))
        return false;

    // Legal only when `to` cannot already reach `from`: otherwise from → to
    // closes a loop and the render cascade would never terminate.
    QSet<SmartObjectDocId> visited;
    std::deque<SmartObjectDocId> queue{to};
    visited.insert(to);
    while (!queue.empty()) {
        const SmartObjectDocId current = queue.front();
        queue.pop_front();
        if (current == from)
            return false;
        const auto doc = m_documents.value(current);
        if (!doc) continue;
        for (const SmartObjectDocId& next : directReferences(*doc)) {
            if (!visited.contains(next)) {
                visited.insert(next);
                queue.push_back(next);
            }
        }
    }
    return true;
}

std::vector<SmartObjectDocId> DocumentContainer::reachableFromRoot() const
{
    std::vector<SmartObjectDocId> out;
    if (!m_documents.contains(m_rootId))
        return out;

    QSet<SmartObjectDocId> visited;
    visited.insert(m_rootId);
    std::deque<SmartObjectDocId> queue{m_rootId};
    out.push_back(m_rootId);
    while (!queue.empty()) {
        const SmartObjectDocId current = queue.front();
        queue.pop_front();
        const auto doc = m_documents.value(current);
        if (!doc) continue;
        for (const SmartObjectDocId& next : directReferences(*doc)) {
            if (visited.contains(next) || !m_documents.contains(next))
                continue;
            visited.insert(next);
            out.push_back(next);
            queue.push_back(next);
        }
    }
    return out;
}

void DocumentContainer::beginEditSession(const SmartObjectDocId& id,
                                         std::shared_ptr<Document> working)
{
    if (id.isEmpty() || !working)
        return;
    m_editSessions.insert(id, std::move(working));
}

std::shared_ptr<Document> DocumentContainer::editSession(const SmartObjectDocId& id) const
{
    return m_editSessions.value(id);
}

void DocumentContainer::endEditSession(const SmartObjectDocId& id)
{
    m_editSessions.remove(id);
}

std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>>
DocumentContainer::editSessions() const
{
    std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>> out;
    out.reserve(m_editSessions.size());
    for (auto it = m_editSessions.constBegin(); it != m_editSessions.constEnd(); ++it)
        out.emplace_back(it.key(), it.value());
    return out;
}
