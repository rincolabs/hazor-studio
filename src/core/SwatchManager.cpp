#include "SwatchManager.hpp"

#include "AppPaths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <functional>

namespace {
constexpr int kFormatVersion = 1;

QString typeToString(SwatchNode::Type t)
{
    return t == SwatchNode::Type::Folder ? QStringLiteral("folder")
                                         : QStringLiteral("collection");
}
}

SwatchManager* SwatchManager::instance()
{
    static SwatchManager s_instance;
    return &s_instance;
}

SwatchManager::SwatchManager(QObject* parent)
    : QObject(parent)
{
    load();
}

QString SwatchManager::makeId(const QString& prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::Id128);
}

QString SwatchManager::displayName(const SwatchColor& c)
{
    if (!c.name.trimmed().isEmpty())
        return c.name;
    return c.color.name(QColor::HexRgb).toUpper();
}

// --- Lookup ----------------------------------------------------------------

SwatchNode* SwatchManager::nodeById(const std::vector<std::unique_ptr<SwatchNode>>& nodes,
                                    const QString& id) const
{
    for (const auto& n : nodes) {
        if (n->id == id)
            return n.get();
        if (n->isFolder()) {
            if (SwatchNode* found = nodeById(n->children, id))
                return found;
        }
    }
    return nullptr;
}

SwatchNode* SwatchManager::findNode(const QString& id) const
{
    if (id.isEmpty())
        return nullptr;
    return nodeById(m_roots, id);
}

SwatchNode* SwatchManager::findCollection(const QString& id) const
{
    SwatchNode* n = findNode(id);
    return (n && n->isCollection()) ? n : nullptr;
}

std::vector<std::unique_ptr<SwatchNode>>* SwatchManager::childListForParent(const QString& parentId)
{
    if (parentId.isEmpty())
        return &m_roots;
    SwatchNode* parent = findNode(parentId);
    if (parent && parent->isFolder())
        return &parent->children;
    return nullptr;  // collections can't hold children; invalid parent
}

void SwatchManager::collectCollections(const SwatchNode* node, const QString& prefix,
                                       QVector<CollectionRef>& out) const
{
    if (node->isCollection()) {
        out.push_back({ node->id, node->name,
                        prefix.isEmpty() ? node->name : prefix + QStringLiteral(" / ") + node->name });
        return;
    }
    const QString childPrefix = prefix.isEmpty() ? node->name
                                                 : prefix + QStringLiteral(" / ") + node->name;
    for (const auto& child : node->children)
        collectCollections(child.get(), childPrefix, out);
}

QVector<SwatchManager::CollectionRef> SwatchManager::allCollections() const
{
    QVector<CollectionRef> out;
    for (const auto& root : m_roots)
        collectCollections(root.get(), QString(), out);
    return out;
}

QString SwatchManager::firstCollectionId() const
{
    const auto cols = allCollections();
    return cols.isEmpty() ? QString() : cols.front().id;
}

// --- Structural mutations --------------------------------------------------

QString SwatchManager::createCollection(const QString& name, const QString& parentId)
{
    auto* list = childListForParent(parentId);
    if (!list)
        return QString();
    auto node = std::make_unique<SwatchNode>();
    node->id = makeId(QStringLiteral("col-"));
    node->name = name.trimmed().isEmpty() ? QStringLiteral("Untitled Palette") : name.trimmed();
    node->type = SwatchNode::Type::Collection;
    node->parent = parentId.isEmpty() ? nullptr : findNode(parentId);
    const QString id = node->id;
    list->push_back(std::move(node));
    touchStructure();
    return id;
}

QString SwatchManager::createFolder(const QString& name, const QString& parentId)
{
    auto* list = childListForParent(parentId);
    if (!list)
        return QString();
    auto node = std::make_unique<SwatchNode>();
    node->id = makeId(QStringLiteral("fld-"));
    node->name = name.trimmed().isEmpty() ? QStringLiteral("New Folder") : name.trimmed();
    node->type = SwatchNode::Type::Folder;
    node->parent = parentId.isEmpty() ? nullptr : findNode(parentId);
    const QString id = node->id;
    list->push_back(std::move(node));
    touchStructure();
    return id;
}

void SwatchManager::renameNode(const QString& id, const QString& name)
{
    SwatchNode* node = findNode(id);
    if (!node || name.trimmed().isEmpty() || node->name == name.trimmed())
        return;
    node->name = name.trimmed();
    touchStructure();
}

void SwatchManager::removeNode(const QString& id)
{
    SwatchNode* node = findNode(id);
    if (!node)
        return;
    auto& siblings = node->parent ? node->parent->children : m_roots;
    for (auto it = siblings.begin(); it != siblings.end(); ++it) {
        if (it->get() == node) {
            siblings.erase(it);
            touchStructure();
            return;
        }
    }
}

void SwatchManager::moveNode(const QString& id, const QString& newParentId, int index)
{
    SwatchNode* node = findNode(id);
    if (!node || id == newParentId)
        return;
    // Disallow moving a folder into its own descendant.
    for (SwatchNode* p = findNode(newParentId); p; p = p->parent) {
        if (p == node)
            return;
    }
    auto* destList = childListForParent(newParentId);
    if (!destList)
        return;

    auto& srcList = node->parent ? node->parent->children : m_roots;
    std::unique_ptr<SwatchNode> owned;
    for (auto it = srcList.begin(); it != srcList.end(); ++it) {
        if (it->get() == node) {
            owned = std::move(*it);
            srcList.erase(it);
            break;
        }
    }
    if (!owned)
        return;
    owned->parent = newParentId.isEmpty() ? nullptr : findNode(newParentId);
    if (index < 0 || index > static_cast<int>(destList->size()))
        destList->push_back(std::move(owned));
    else
        destList->insert(destList->begin() + index, std::move(owned));
    touchStructure();
}

// --- Color mutations -------------------------------------------------------

QString SwatchManager::addColor(const QString& collectionId, const QColor& color, const QString& name)
{
    SwatchNode* col = findCollection(collectionId);
    if (!col || !color.isValid())
        return QString();
    SwatchColor sc;
    sc.id = makeId(QStringLiteral("clr-"));
    sc.name = name.trimmed();
    sc.color = color;
    col->colors.push_back(sc);
    touchColors(collectionId);
    return sc.id;
}

void SwatchManager::removeColor(const QString& collectionId, const QString& colorId)
{
    SwatchNode* col = findCollection(collectionId);
    if (!col)
        return;
    for (int i = 0; i < col->colors.size(); ++i) {
        if (col->colors[i].id == colorId) {
            col->colors.remove(i);
            touchColors(collectionId);
            return;
        }
    }
}

void SwatchManager::setColor(const QString& collectionId, const QString& colorId,
                             const QColor& color, const QString& name)
{
    SwatchNode* col = findCollection(collectionId);
    if (!col || !color.isValid())
        return;
    for (SwatchColor& sc : col->colors) {
        if (sc.id == colorId) {
            sc.color = color;
            sc.name = name.trimmed();
            touchColors(collectionId);
            return;
        }
    }
}

void SwatchManager::reorderColors(const QString& collectionId, const QVector<QString>& orderedColorIds)
{
    SwatchNode* col = findCollection(collectionId);
    if (!col)
        return;
    QVector<SwatchColor> reordered;
    reordered.reserve(col->colors.size());
    for (const QString& cid : orderedColorIds) {
        for (const SwatchColor& sc : col->colors) {
            if (sc.id == cid) {
                reordered.push_back(sc);
                break;
            }
        }
    }
    // Preserve any colors not present in the supplied order (safety net).
    if (reordered.size() != col->colors.size()) {
        for (const SwatchColor& sc : col->colors) {
            bool present = false;
            for (const SwatchColor& r : reordered) {
                if (r.id == sc.id) { present = true; break; }
            }
            if (!present)
                reordered.push_back(sc);
        }
    }
    col->colors = reordered;
    touchColors(collectionId);
}

// --- Notifications ---------------------------------------------------------

void SwatchManager::touch()
{
    save();
    emit changed();
}

void SwatchManager::touchStructure()
{
    save();
    emit structureChanged();
    emit changed();
}

void SwatchManager::touchColors(const QString& collectionId)
{
    save();
    emit collectionColorsChanged(collectionId);
    emit changed();
}

// --- Defaults --------------------------------------------------------------

QVector<QColor> SwatchManager::defaultPaletteColors()
{
    return {
        QColor("#000000"), QColor("#202020"), QColor("#404040"), QColor("#808080"), QColor("#B0B0B0"), QColor("#E0E0E0"), QColor("#FFFFFF"),
        QColor("#4C2A2A"), QColor("#7A1E1E"), QColor("#B31212"), QColor("#E53935"), QColor("#FF6B6B"), QColor("#FF9AA2"),
        QColor("#4A2A14"), QColor("#7A3F14"), QColor("#B35A1E"), QColor("#E67E22"), QColor("#FFAA4C"), QColor("#FFD08A"),
        QColor("#4A4312"), QColor("#7A6A11"), QColor("#B39B1A"), QColor("#E6C229"), QColor("#FFD84D"), QColor("#FFE88C"),
        QColor("#1E4B1E"), QColor("#2E7D32"), QColor("#43A047"), QColor("#66BB6A"), QColor("#9CCC65"), QColor("#C5E1A5"),
        QColor("#0F4C4C"), QColor("#00838F"), QColor("#00ACC1"), QColor("#26C6DA"), QColor("#4DD0E1"), QColor("#80DEEA"),
        QColor("#132A4A"), QColor("#1565C0"), QColor("#1E88E5"), QColor("#42A5F5"), QColor("#64B5F6"), QColor("#90CAF9"),
        QColor("#2B1A4A"), QColor("#5E35B1"), QColor("#7E57C2"), QColor("#9575CD"), QColor("#B39DDB"), QColor("#D1C4E9"),
        QColor("#4A1E3A"), QColor("#AD1457"), QColor("#D81B60"), QColor("#EC407A"), QColor("#F06292"), QColor("#F8BBD0"),
        QColor("#3E2723"), QColor("#5D4037"), QColor("#795548"), QColor("#8D6E63"), QColor("#A1887F"), QColor("#BCAAA4")
    };
}

void SwatchManager::seedDefaults()
{
    m_roots.clear();

    // Preferred path: build the seed tree from the bundled preset file. The
    // "Default Colors" collection is populated at runtime with the historical
    // palette so it always mirrors the current default swatches.
    QFile presetFile(QStringLiteral(":/swatches/default-presets.json"));
    if (presetFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(presetFile.readAll());
        const QJsonArray roots = doc.object().value(QStringLiteral("roots")).toArray();
        for (const QJsonValue& v : roots)
            m_roots.push_back(nodeFromJson(v.toObject(), nullptr));
    }

    if (!m_roots.empty()) {
        // Override "Default Colors" with the current default-swatch palette.
        std::function<bool(SwatchNode*)> fill = [&](SwatchNode* node) -> bool {
            if (node->isCollection()) {
                if (node->name == QStringLiteral("Default Colors")) {
                    node->colors.clear();
                    for (const QColor& c : defaultPaletteColors()) {
                        SwatchColor sc;
                        sc.id = makeId(QStringLiteral("clr-"));
                        sc.color = c;
                        node->colors.push_back(sc);
                    }
                    return true;
                }
                return false;
            }
            for (const auto& child : node->children)
                if (fill(child.get()))
                    return true;
            return false;
        };
        for (const auto& root : m_roots)
            if (fill(root.get()))
                break;
        return;
    }

    // Fallback: bundled preset unavailable — seed a single collection with the
    // historical palette.
    auto col = std::make_unique<SwatchNode>();
    col->id = makeId(QStringLiteral("col-"));
    col->name = defaultCollectionName();
    col->type = SwatchNode::Type::Collection;
    for (const QColor& c : defaultPaletteColors()) {
        SwatchColor sc;
        sc.id = makeId(QStringLiteral("clr-"));
        sc.color = c;
        col->colors.push_back(sc);
    }
    m_roots.push_back(std::move(col));
}

// --- Persistence -----------------------------------------------------------

QJsonObject SwatchManager::nodeToJson(const SwatchNode* node) const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = node->id;
    obj[QStringLiteral("name")] = node->name;
    obj[QStringLiteral("type")] = typeToString(node->type);
    if (node->isCollection()) {
        QJsonArray colors;
        for (const SwatchColor& sc : node->colors) {
            QJsonObject c;
            c[QStringLiteral("id")] = sc.id;
            c[QStringLiteral("name")] = sc.name;
            c[QStringLiteral("color")] = sc.color.name(QColor::HexArgb);
            colors.push_back(c);
        }
        obj[QStringLiteral("colors")] = colors;
    } else {
        QJsonArray children;
        for (const auto& child : node->children)
            children.push_back(nodeToJson(child.get()));
        obj[QStringLiteral("children")] = children;
    }
    return obj;
}

std::unique_ptr<SwatchNode> SwatchManager::nodeFromJson(const QJsonObject& obj, SwatchNode* parent)
{
    auto node = std::make_unique<SwatchNode>();
    node->id = obj.value(QStringLiteral("id")).toString();
    if (node->id.isEmpty())
        node->id = makeId(QStringLiteral("node-"));
    node->name = obj.value(QStringLiteral("name")).toString();
    node->parent = parent;
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("folder")) {
        node->type = SwatchNode::Type::Folder;
        const QJsonArray children = obj.value(QStringLiteral("children")).toArray();
        for (const QJsonValue& v : children)
            node->children.push_back(nodeFromJson(v.toObject(), node.get()));
    } else {
        node->type = SwatchNode::Type::Collection;
        const QJsonArray colors = obj.value(QStringLiteral("colors")).toArray();
        for (const QJsonValue& v : colors) {
            const QJsonObject c = v.toObject();
            SwatchColor sc;
            sc.id = c.value(QStringLiteral("id")).toString();
            if (sc.id.isEmpty())
                sc.id = makeId(QStringLiteral("clr-"));
            sc.name = c.value(QStringLiteral("name")).toString();
            sc.color = QColor(c.value(QStringLiteral("color")).toString());
            if (sc.color.isValid())
                node->colors.push_back(sc);
        }
    }
    return node;
}

QString SwatchManager::swatchesFilePath()
{
    return AppPaths::dataFile(QStringLiteral("swatches.json"));
}

void SwatchManager::load()
{
    m_roots.clear();

    QFile file(swatchesFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            const QJsonArray roots = doc.object().value(QStringLiteral("roots")).toArray();
            for (const QJsonValue& v : roots)
                m_roots.push_back(nodeFromJson(v.toObject(), nullptr));
        }
    }

    if (m_roots.empty())
        seedDefaults();
}

void SwatchManager::save() const
{
    QJsonArray roots;
    for (const auto& root : m_roots)
        roots.push_back(nodeToJson(root.get()));
    QJsonObject doc;
    doc[QStringLiteral("version")] = kFormatVersion;
    doc[QStringLiteral("roots")] = roots;

    const QString path = swatchesFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
}
