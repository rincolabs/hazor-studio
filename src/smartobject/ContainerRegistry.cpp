#include "smartobject/ContainerRegistry.hpp"

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "io/ProjectFileService.hpp"
#include "smartobject/DocumentContainer.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <deque>

namespace {

// A document is identified across files by (canonical container path, docId).
struct DocRef {
    QString path;
    SmartObjectDocId docId;
    bool operator==(const DocRef& other) const
    {
        return path == other.path && docId == other.docId;
    }
};

size_t qHash(const DocRef& ref, size_t seed = 0)
{
    return qHash(ref.path, seed) ^ qHash(ref.docId, seed);
}

} // namespace

ContainerRegistry& ContainerRegistry::instance()
{
    static ContainerRegistry registry;
    return registry;
}

QString ContainerRegistry::canonicalKey(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    // canonicalFilePath() is empty for a file that does not exist yet (a Save As
    // target). The absolute path is the best stable key available then, and the
    // entry gets re-keyed once the file exists.
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

std::shared_ptr<DocumentContainer> ContainerRegistry::find(const QString& path) const
{
    const QString key = canonicalKey(path);
    if (key.isEmpty())
        return nullptr;
    return m_containers.value(key);
}

std::shared_ptr<DocumentContainer> ContainerRegistry::open(const QString& path, QString* error)
{
    const QString key = canonicalKey(path);
    if (key.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid project path.");
        return nullptr;
    }
    if (auto existing = m_containers.value(key))
        return existing;

    ContainerLoadResult loaded = ProjectFileService::loadContainer(path);
    if (!loaded.ok || !loaded.container) {
        if (error) {
            *error = loaded.error.isEmpty()
                ? QStringLiteral("Invalid project file.") : loaded.error;
        }
        return nullptr;
    }
    loaded.container->setPath(key);
    m_containers.insert(key, loaded.container);
    return loaded.container;
}

void ContainerRegistry::registerContainer(const std::shared_ptr<DocumentContainer>& container,
                                          const QString& path)
{
    if (!container)
        return;
    const QString key = canonicalKey(path);
    if (key.isEmpty())
        return;

    // Save As: drop the old key so one container is never reachable under two
    // paths (which would reintroduce the two-Documents-per-file bug from the
    // other direction).
    const QString previous = container->path();
    if (!previous.isEmpty() && previous != key
        && m_containers.value(previous) == container) {
        m_containers.remove(previous);
    }
    container->setPath(key);
    m_containers.insert(key, container);
}

void ContainerRegistry::forget(const QString& path)
{
    const QString key = canonicalKey(path);
    if (!key.isEmpty())
        m_containers.remove(key);
}

bool ContainerRegistry::wouldCreateCycle(const QString& fromPath,
                                         const SmartObjectDocId& fromDocId,
                                         const QString& toPath,
                                         const SmartObjectDocId& toDocId) const
{
    const DocRef from{canonicalKey(fromPath), fromDocId};
    DocRef to{canonicalKey(toPath), toDocId};
    if (from == to)
        return true;

    // An empty target docId means "the root of that file".
    if (to.docId.isEmpty()) {
        if (auto container = m_containers.value(to.path))
            to.docId = container->rootId();
    }
    if (from == to)
        return true;

    // Forward reachability from the prospective target: if it can already reach
    // the referencing document, the new edge closes a loop.
    QSet<DocRef> visited;
    std::deque<DocRef> queue;
    visited.insert(to);
    queue.push_back(to);

    while (!queue.empty()) {
        const DocRef current = queue.front();
        queue.pop_front();
        if (current == from)
            return true;

        const auto container = m_containers.value(current.path);
        if (!container)
            continue;   // not open: we cannot see through it, so the branch ends
        const auto doc = container->document(current.docId);
        if (!doc)
            continue;

        for (LayerTreeNode* node : doc->flatten()) {
            if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
                continue;
            const auto payload = node->layer->smartObjectData;
            if (!payload)
                continue;

            DocRef next;
            if (payload->mode == SmartObjectStorageMode::Embedded) {
                next = DocRef{current.path, payload->docId};
            } else {
                if (payload->linkedPath.isEmpty())
                    continue;
                // Linked paths are stored relative to their own container.
                const QDir base(QFileInfo(current.path).absolutePath());
                next.path = canonicalKey(base.absoluteFilePath(payload->linkedPath));
                next.docId = payload->docId;
                if (next.docId.isEmpty()) {
                    if (auto target = m_containers.value(next.path))
                        next.docId = target->rootId();
                }
            }
            if (next.docId.isEmpty() || visited.contains(next))
                continue;
            visited.insert(next);
            queue.push_back(next);
        }
    }
    return false;
}
