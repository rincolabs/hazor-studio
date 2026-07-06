#include "BrushLibraryTreeModel.hpp"

#include "brush/BrushPresetManager.hpp"

#include <QDataStream>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonObject>
#include <QMimeData>
#include <QSettings>
#include <functional>
#include <utility>

namespace {
const QString kMimeType = QStringLiteral("application/x-brush-library-item");

QStringList splitGroupPath(const QString& group)
{
    QStringList parts = group.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (QString& part : parts)
        part = part.trimmed();
    parts.removeAll(QString());
    return parts;
}

QString completeBaseNameFromStoredFile(const QString& fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName();
    return base.isEmpty() ? fileName : base;
}

QString normalizedIdPart(QString part)
{
    part = part.trimmed().toLower();
    part.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return part;
}
} // namespace

BrushLibraryTreeModel::BrushLibraryTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>())
{
}

BrushLibraryTreeModel::~BrushLibraryTreeModel() = default;

void BrushLibraryTreeModel::setPresetManager(BrushPresetManager* manager)
{
    if (m_manager == manager)
        return;
    if (m_manager)
        m_manager->disconnect(this);
    m_manager = manager;
    if (m_manager) {
        connect(m_manager, &BrushPresetManager::presetsChanged,
                this, &BrushLibraryTreeModel::rebuild);
    }
    rebuild();
}

QModelIndex BrushLibraryTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
        return {};
    Node* parentNode = nodeForIndex(parent);
    if (!parentNode || row >= static_cast<int>(parentNode->children.size()))
        return {};
    return createIndex(row, column, parentNode->children[static_cast<size_t>(row)].get());
}

QModelIndex BrushLibraryTreeModel::parent(const QModelIndex& child) const
{
    Node* node = nodeForIndex(child);
    if (!node || !node->parent || node->parent == m_root.get())
        return {};
    return indexForNode(node->parent);
}

int BrushLibraryTreeModel::rowCount(const QModelIndex& parent) const
{
    const Node* node = nodeForIndex(parent);
    return node ? static_cast<int>(node->children.size()) : 0;
}

int BrushLibraryTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant BrushLibraryTreeModel::data(const QModelIndex& index, int role) const
{
    const Node* node = nodeForIndex(index);
    if (!node)
        return {};

    if (role == Qt::DisplayRole || role == NameRole)
        return node->name;
    if (role == NodeTypeRole)
        return static_cast<int>(node->type);
    if (role == FolderIdRole && node->type == BrushLibraryNodeType::Folder)
        return node->id;
    if (role == BrushCountRole && node->type == BrushLibraryNodeType::Folder)
        return node->brushCount;

    if (node->type != BrushLibraryNodeType::BrushPreset)
        return {};

    const BrushPreset* preset = presetForIndex(index);
    if (!preset)
        return {};

    if (role == PresetNameRole)
        return preset->name;
    if (role == SourceSubtitleRole)
        return sourceSubtitle(*preset);
    if (role == Qt::ToolTipRole) {
        QStringList lines;
        lines << preset->name;
        const QString type = sourceTypeId(*preset);
        if (!type.isEmpty()) {
            const QJsonObject src = preset->sourceInfo;
            const QString sourceApp = src["sourceApp"].toString();
            const QString fileName = src["fileName"].toString();
            const QString compatibility = src["compatibility"].toString();
            const QString importedAt = src["importedAt"].toString();
            lines << tr("Source: %1").arg(sourceApp.isEmpty() ? type : sourceApp);
            if (!fileName.isEmpty())
                lines << tr("File: %1").arg(fileName);
            if (!compatibility.isEmpty())
                lines << tr("Compatibility: %1").arg(compatibility);
            if (!importedAt.isEmpty())
                lines << tr("Imported: %1").arg(importedAt);
        }
        return lines.join(QLatin1Char('\n'));
    }

    return {};
}

const BrushPreset* BrushLibraryTreeModel::presetForIndex(const QModelIndex& index) const
{
    const Node* node = nodeForIndex(index);
    if (!node || node->type != BrushLibraryNodeType::BrushPreset || !m_manager)
        return nullptr;
    const auto& presets = m_manager->presets();
    if (node->presetIndex < 0 || node->presetIndex >= static_cast<int>(presets.size()))
        return nullptr;
    return &presets[static_cast<size_t>(node->presetIndex)];
}

bool BrushLibraryTreeModel::isFolder(const QModelIndex& index) const
{
    const Node* node = nodeForIndex(index);
    return node && node->type == BrushLibraryNodeType::Folder;
}

QModelIndex BrushLibraryTreeModel::indexForPresetName(const QString& name) const
{
    if (!m_root)
        return {};

    std::vector<const Node*> stack;
    stack.push_back(m_root.get());
    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();
        if (node->type == BrushLibraryNodeType::BrushPreset) {
            const QModelIndex idx = indexForNode(node);
            const BrushPreset* preset = presetForIndex(idx);
            if (preset && preset->name == name)
                return idx;
        }
        for (const auto& child : node->children)
            stack.push_back(child.get());
    }
    return {};
}

QVector<QModelIndex> BrushLibraryTreeModel::folderIndexes() const
{
    QVector<QModelIndex> out;
    collectFolderIndexes(m_root.get(), out);
    return out;
}

void BrushLibraryTreeModel::setFilter(const QString& needle)
{
    const QString trimmed = needle.trimmed();
    if (trimmed == m_filter)
        return;
    m_filter = trimmed;
    rebuild();
}

bool BrushLibraryTreeModel::folderExpanded(const QModelIndex& index) const
{
    const Node* node = nodeForIndex(index);
    if (!node || node->type != BrushLibraryNodeType::Folder)
        return false;
    return storedExpandedState(node->id);
}

void BrushLibraryTreeModel::setFolderExpanded(const QModelIndex& index, bool expanded)
{
    const Node* node = nodeForIndex(index);
    if (!node || node->type != BrushLibraryNodeType::Folder || node->id.isEmpty())
        return;
    m_expandedFolders.insert(node->id, expanded);
    QSettings settings;
    settings.setValue(QStringLiteral("brushLibrary/expandedFolders/%1").arg(node->id), expanded);
}

void BrushLibraryTreeModel::rebuild()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    if (m_manager) {
        // Explicit (possibly empty) folders first, so they survive even with no
        // presets inside. Skipped while filtering — a search shows only matches.
        if (m_filter.isEmpty()) {
            for (const QString& f : m_manager->folders())
                ensureExplicitFolder(m_root.get(), splitGroupPath(f), 0);
        }
        const auto& presets = m_manager->presets();
        for (int i = 0; i < static_cast<int>(presets.size()); ++i)
            insertPreset(i);
    }
    endResetModel();
}

BrushLibraryTreeModel::Node* BrushLibraryTreeModel::ensureExplicitFolder(
    Node* parent, const QStringList& path, int depth)
{
    if (!parent || depth >= path.size())
        return parent;

    const QString name = path[depth];
    for (const auto& child : parent->children) {
        if (child->type == BrushLibraryNodeType::Folder && child->name == name)
            return ensureExplicitFolder(child.get(), path, depth + 1);
    }

    auto node = std::make_unique<Node>();
    node->parent = parent;
    node->name = name;
    node->id = folderIdFor(path.mid(0, depth + 1));
    node->type = BrushLibraryNodeType::Folder;
    node->brushCount = 0;   // empty until presets land here
    Node* raw = node.get();
    parent->children.push_back(std::move(node));
    return ensureExplicitFolder(raw, path, depth + 1);
}

BrushLibraryTreeModel::Node* BrushLibraryTreeModel::nodeForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return m_root.get();
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex BrushLibraryTreeModel::indexForNode(const Node* node) const
{
    if (!node || node == m_root.get() || !node->parent)
        return {};
    const int row = rowOfNode(node);
    if (row < 0)
        return {};
    return createIndex(row, 0, const_cast<Node*>(node));
}

int BrushLibraryTreeModel::rowOfNode(const Node* node) const
{
    if (!node || !node->parent)
        return -1;
    const auto& siblings = node->parent->children;
    for (int i = 0; i < static_cast<int>(siblings.size()); ++i) {
        if (siblings[static_cast<size_t>(i)].get() == node)
            return i;
    }
    return -1;
}

BrushLibraryTreeModel::Node* BrushLibraryTreeModel::ensureFolder(
    Node* parent, const QStringList& path, int depth, bool imported, bool builtIn)
{
    if (!parent || depth >= path.size())
        return parent;

    const QString name = path[depth];
    for (const auto& child : parent->children) {
        if (child->type == BrushLibraryNodeType::Folder && child->name == name) {
            child->imported = child->imported || imported;
            child->builtIn = child->builtIn || builtIn;
            ++child->brushCount;
            return ensureFolder(child.get(), path, depth + 1, imported, builtIn);
        }
    }

    auto node = std::make_unique<Node>();
    node->parent = parent;
    node->name = name;
    node->id = folderIdFor(path.mid(0, depth + 1));
    node->type = BrushLibraryNodeType::Folder;
    node->brushCount = 1;
    node->imported = imported;
    node->builtIn = builtIn;

    Node* raw = node.get();
    parent->children.push_back(std::move(node));
    return ensureFolder(raw, path, depth + 1, imported, builtIn);
}

void BrushLibraryTreeModel::insertPreset(int presetIndex)
{
    if (!m_manager)
        return;
    const auto& presets = m_manager->presets();
    if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size()))
        return;

    const BrushPreset& preset = presets[static_cast<size_t>(presetIndex)];
    const QStringList path = folderPathForPreset(preset);
    if (!presetMatchesFilter(preset, path))
        return;

    const bool imported = !sourceTypeId(preset).isEmpty();
    Node* folder = ensureFolder(m_root.get(), path, 0, imported, !imported);
    if (!folder)
        folder = m_root.get();

    auto leaf = std::make_unique<Node>();
    leaf->parent = folder;
    leaf->name = preset.name;
    leaf->type = BrushLibraryNodeType::BrushPreset;
    leaf->presetIndex = presetIndex;
    leaf->imported = imported;
    leaf->builtIn = !imported;
    folder->children.push_back(std::move(leaf));
}

bool BrushLibraryTreeModel::presetMatchesFilter(const BrushPreset& preset,
                                                const QStringList& folderPath) const
{
    if (m_filter.isEmpty())
        return true;

    const QString needle = m_filter.toLower();
    if (preset.name.toLower().contains(needle))
        return true;
    if (preset.group.toLower().contains(needle))
        return true;

    for (const QString& folder : folderPath) {
        if (folder.toLower().contains(needle))
            return true;
    }

    const QJsonObject src = preset.sourceInfo;
    const QStringList sourceFields = {
        sourceTypeId(preset),
        src["type"].toString(),
        src["sourceApp"].toString(),
        src["fileName"].toString(),
        src["bundleName"].toString(),
        src["displayGroup"].toString(),
        src["compatibility"].toString()
    };
    for (const QString& field : sourceFields) {
        if (field.toLower().contains(needle))
            return true;
    }

    return false;
}

QStringList BrushLibraryTreeModel::folderPathForPreset(const BrushPreset& preset) const
{
    const QString type = sourceTypeId(preset);
    QStringList groupParts = splitGroupPath(preset.group);

    // The stored group is the authoritative full display path once a preset has
    // ever been organised. A path that already starts with a known root is taken
    // verbatim; a legacy/relative one is placed under its natural root.
    if (!groupParts.isEmpty()) {
        const QString first = groupParts.first();
        if (first == tr("Default Brushes") || first == tr("Imported Brushes"))
            return groupParts;
        QStringList path;
        path << (type.isEmpty() ? tr("Default Brushes") : tr("Imported Brushes"));
        path << groupParts;
        return path;
    }

    // Empty group → derive the initial placement.
    if (type.isEmpty())
        return { tr("Default Brushes") };

    const QJsonObject src = preset.sourceInfo;
    const QString fileBase = completeBaseNameFromStoredFile(src["fileName"].toString());
    QStringList path;
    path << tr("Imported Brushes");
    if (type == QLatin1String("krita-kpp")) {
        path << tr("Krita") << src["displayGroup"].toString(tr("Individual Presets"));
    } else if (type == QLatin1String("krita-bundle")) {
        path << tr("Krita");
        const QString bundle = src["bundleName"].toString(fileBase);
        if (!bundle.isEmpty())
            path << bundle;
    } else if (type == QLatin1String("image-brush-tip")) {
        path << tr("Images");
        if (!fileBase.isEmpty())
            path << fileBase;
    } else {
        path << tr("Other");
    }
    return path;
}

QString BrushLibraryTreeModel::sourceTypeId(const BrushPreset& preset) const
{
    const QString raw = preset.sourceInfo["type"].toString().trimmed().toLower();
    if (raw.isEmpty())
        return {};
    if (raw == QLatin1String("krita-kpp") || raw.contains(QLatin1String("krita preset"))
        || raw.contains(QLatin1String("kpp")))
        return QStringLiteral("krita-kpp");
    if (raw == QLatin1String("krita-bundle") || raw.contains(QLatin1String("krita bundle"))
        || raw.contains(QLatin1String("bundle")))
        return QStringLiteral("krita-bundle");
    if (raw == QLatin1String("image-brush-tip") || raw.contains(QLatin1String("image")))
        return QStringLiteral("image-brush-tip");
    return raw;
}

QString BrushLibraryTreeModel::sourceSubtitle(const BrushPreset& preset) const
{
    const QString type = sourceTypeId(preset);
    if (type.isEmpty())
        return {};
    const QJsonObject src = preset.sourceInfo;
    QString label = src["sourceApp"].toString();
    if (label.isEmpty()) {
        if (type == QLatin1String("krita-kpp") || type == QLatin1String("krita-bundle"))
            label = tr("Krita");
        else if (type == QLatin1String("image-brush-tip"))
            label = tr("Image");
    }
    const QString compatibility = src["compatibility"].toString();
    if (!label.isEmpty() && !compatibility.isEmpty())
        return tr("%1 · %2").arg(label, compatibility);
    return label;
}

QString BrushLibraryTreeModel::folderIdFor(const QStringList& path) const
{
    QStringList idParts;
    for (const QString& part : path)
        idParts << normalizedIdPart(part);
    return idParts.join(QLatin1Char('/'));
}

bool BrushLibraryTreeModel::storedExpandedState(const QString& folderId) const
{
    if (folderId.isEmpty())
        return true;
    if (m_expandedFolders.contains(folderId))
        return m_expandedFolders.value(folderId);
    QSettings settings;
    return settings.value(QStringLiteral("brushLibrary/expandedFolders/%1").arg(folderId), true).toBool();
}

void BrushLibraryTreeModel::collectFolderIndexes(const Node* parent, QVector<QModelIndex>& out) const
{
    if (!parent)
        return;
    for (const auto& child : parent->children) {
        if (child->type == BrushLibraryNodeType::Folder) {
            const QModelIndex idx = indexForNode(child.get());
            if (idx.isValid())
                out.append(idx);
            collectFolderIndexes(child.get(), out);
        }
    }
}

// ── Preset Manager helpers ────────────────────────────────────

namespace {
bool listStartsWith(const QStringList& a, const QStringList& b)
{
    if (a.size() < b.size())
        return false;
    for (int i = 0; i < b.size(); ++i)
        if (a[i] != b[i])
            return false;
    return true;
}
} // namespace

void BrushLibraryTreeModel::setEditingEnabled(bool on)
{
    m_editingEnabled = on;
}

QStringList BrushLibraryTreeModel::displayPathForNode(const Node* node) const
{
    QStringList path;
    const Node* n = node;
    while (n && n != m_root.get()) {
        path.prepend(n->name);
        n = n->parent;
    }
    return path;
}

QString BrushLibraryTreeModel::topRootForNode(const Node* node) const
{
    const Node* n = node;
    while (n && n->parent && n->parent != m_root.get())
        n = n->parent;
    return (n && n != m_root.get()) ? n->name : QString();
}

QStringList BrushLibraryTreeModel::folderDisplayPath(const QModelIndex& index) const
{
    const Node* node = nodeForIndex(index);
    if (!node || node->type != BrushLibraryNodeType::Folder)
        return {};
    return displayPathForNode(node);
}

QString BrushLibraryTreeModel::topRootName(const QModelIndex& index) const
{
    return topRootForNode(nodeForIndex(index));
}

bool BrushLibraryTreeModel::isManagedFolder(const QModelIndex& index) const
{
    // Any folder is user-managed except the two top-level virtual roots, which are
    // protected (they are the natural homes for empty-group / freshly imported
    // presets, so renaming/deleting them would be meaningless).
    const Node* node = nodeForIndex(index);
    if (!node || node == m_root.get() || node->type != BrushLibraryNodeType::Folder)
        return false;
    return node->parent != m_root.get();
}

bool BrushLibraryTreeModel::isManagedPreset(const QModelIndex& index) const
{
    const Node* node = nodeForIndex(index);
    return node && node->type == BrushLibraryNodeType::BrushPreset;
}

void BrushLibraryTreeModel::collectPresetNames(const Node* node, QStringList& out) const
{
    if (!node)
        return;
    if (node->type == BrushLibraryNodeType::BrushPreset) {
        out << node->name;
        return;
    }
    for (const auto& child : node->children)
        collectPresetNames(child.get(), out);
}

QStringList BrushLibraryTreeModel::presetNamesUnder(const QModelIndex& folderIndex) const
{
    QStringList out;
    collectPresetNames(nodeForIndex(folderIndex), out);
    return out;
}

QStringList BrushLibraryTreeModel::targetFolderForDrop(const QModelIndex& index) const
{
    const Node* node = nodeForIndex(index);
    if (!node || node == m_root.get())
        return { tr("Default Brushes") };
    if (node->type == BrushLibraryNodeType::Folder)
        return displayPathForNode(node);
    return displayPathForNode(node->parent);   // preset → its folder
}

const BrushLibraryTreeModel::Node* BrushLibraryTreeModel::folderNodeForPath(
    const Node* parent, const QStringList& path, int depth) const
{
    if (!parent || depth >= path.size())
        return parent;
    for (const auto& child : parent->children) {
        if (child->type == BrushLibraryNodeType::Folder && child->name == path[depth])
            return folderNodeForPath(child.get(), path, depth + 1);
    }
    return nullptr;
}

QModelIndex BrushLibraryTreeModel::indexForFolderPath(const QStringList& displayPath) const
{
    if (displayPath.isEmpty())
        return {};
    const Node* node = folderNodeForPath(m_root.get(), displayPath, 0);
    if (!node || node == m_root.get())
        return {};
    return indexForNode(node);
}

void BrushLibraryTreeModel::collectManagedFolderPaths(const Node* node,
                                                      QVector<QStringList>& out) const
{
    if (!node)
        return;
    for (const auto& child : node->children) {
        if (child->type != BrushLibraryNodeType::Folder)
            continue;
        out.append(displayPathForNode(child.get()));   // every folder, both roots
        collectManagedFolderPaths(child.get(), out);
    }
}

QVector<QStringList> BrushLibraryTreeModel::managedFolderDisplayPaths() const
{
    QVector<QStringList> out;
    collectManagedFolderPaths(m_root.get(), out);
    return out;
}

QString BrushLibraryTreeModel::groupFromDisplayPath(const QStringList& displayPath)
{
    // The group is the full display path (root included) — authoritative once set.
    return displayPath.join(QLatin1Char('/'));
}

QStringList BrushLibraryTreeModel::defaultRootPath()
{
    return { tr("Default Brushes") };
}

QString BrushLibraryTreeModel::groupFromUserPath(const QString& text)
{
    QStringList parts = text.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (QString& p : parts)
        p = p.trimmed();
    parts.removeAll(QString());
    if (parts.isEmpty())
        return {};
    // A path without a known root lands under Default Brushes.
    const QString first = parts.first();
    if (first != tr("Default Brushes") && first != tr("Imported Brushes"))
        parts.prepend(tr("Default Brushes"));
    return parts.join(QLatin1Char('/'));
}

bool BrushLibraryTreeModel::saveGroupForIndex(const QModelIndex& index, QString* outGroup) const
{
    const Node* node = nodeForIndex(index);
    if (!node || node == m_root.get()) {        // empty area / root → Default root
        if (outGroup)
            *outGroup = tr("Default Brushes");
        return true;
    }
    const QStringList folderPath = (node->type == BrushLibraryNodeType::Folder)
        ? displayPathForNode(node)
        : displayPathForNode(node->parent);
    if (outGroup)
        *outGroup = folderPath.join(QLatin1Char('/'));
    return true;
}

bool BrushLibraryTreeModel::relocateFolder(const QModelIndex& folderIndex,
                                           const QStringList& newFolderDisplayPath)
{
    const Node* folder = nodeForIndex(folderIndex);
    if (!folder || folder->type != BrushLibraryNodeType::Folder || !m_manager)
        return false;
    if (newFolderDisplayPath.isEmpty())
        return false;
    // Destination must sit under a real root (never at the absolute top).
    if (newFolderDisplayPath.size() < 2)
        return false;

    const QStringList oldPath = displayPathForNode(folder);
    if (oldPath.isEmpty() || oldPath.size() < 2)   // never relocate a virtual root
        return false;
    // Refuse moving a folder into itself or one of its own descendants.
    if (listStartsWith(newFolderDisplayPath, oldPath))
        return false;
    // Refuse if the destination already has a folder with that name.
    const QModelIndex existing = indexForFolderPath(newFolderDisplayPath);
    if (existing.isValid() && existing != folderIndex)
        return false;

    // For every descendant preset, swap the old folder prefix for the new one and
    // recompute its stored group (full display path).
    std::vector<std::pair<QString, QString>> changes;
    std::function<void(const Node*)> walk = [&](const Node* n) {
        if (!n)
            return;
        if (n->type == BrushLibraryNodeType::BrushPreset) {
            const QStringList presetFolder = displayPathForNode(n->parent);
            if (!listStartsWith(presetFolder, oldPath))
                return;
            QStringList newFolder = newFolderDisplayPath;
            newFolder += presetFolder.mid(oldPath.size());
            changes.emplace_back(n->name, groupFromDisplayPath(newFolder));
            return;
        }
        for (const auto& child : n->children)
            walk(child.get());
    };
    walk(folder);

    // Move folder entries + preset groups together in one persisted batch (so empty
    // subfolders relocate too).
    m_manager->relocateFolderData(oldPath.join(QLatin1Char('/')),
                                  newFolderDisplayPath.join(QLatin1Char('/')),
                                  changes);
    return true;
}

// ── Drag & drop ───────────────────────────────────────────────

Qt::ItemFlags BrushLibraryTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return m_editingEnabled ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (!m_editingEnabled)
        return f;

    const Node* node = nodeForIndex(index);
    if (!node)
        return f;
    if (node->type == BrushLibraryNodeType::BrushPreset) {
        f |= Qt::ItemIsDragEnabled;       // any preset can be moved
    } else {                              // folder
        f |= Qt::ItemIsDropEnabled;       // any folder (incl. both roots) accepts drops
        if (node->parent != m_root.get()) // not a virtual root → it can be moved
            f |= Qt::ItemIsDragEnabled;
    }
    return f;
}

Qt::DropActions BrushLibraryTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList BrushLibraryTreeModel::mimeTypes() const
{
    return { kMimeType };
}

QMimeData* BrushLibraryTreeModel::mimeData(const QModelIndexList& indexes) const
{
    for (const QModelIndex& index : indexes) {
        if (!index.isValid())
            continue;
        const Node* node = nodeForIndex(index);
        if (!node)
            continue;
        QByteArray buf;
        QDataStream st(&buf, QIODevice::WriteOnly);
        if (node->type == BrushLibraryNodeType::BrushPreset)
            st << int(0) << node->name;
        else
            st << int(1) << displayPathForNode(node);
        auto* md = new QMimeData;
        md->setData(kMimeType, buf);
        return md;
    }
    return nullptr;
}

bool BrushLibraryTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction,
                                            int, int, const QModelIndex& parent) const
{
    if (!m_editingEnabled || !data || !data->hasFormat(kMimeType))
        return false;

    QByteArray buf = data->data(kMimeType);
    QDataStream st(&buf, QIODevice::ReadOnly);
    int kind = -1;
    st >> kind;

    const QStringList target = targetFolderForDrop(parent);
    if (target.isEmpty())               // must resolve to a real folder under a root
        return false;

    if (kind == 0)          // preset → into any folder
        return true;

    if (kind == 1) {        // folder
        QStringList src;
        st >> src;
        if (src.size() < 2)             // never the virtual root
            return false;
        QStringList newPath = target;
        newPath << src.last();
        if (listStartsWith(newPath, src))           // into itself / a descendant
            return false;
        if (target == src.mid(0, src.size() - 1))   // already in this folder
            return false;
        if (indexForFolderPath(newPath).isValid())  // name clash at destination
            return false;
        return true;
    }
    return false;
}

bool BrushLibraryTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                         int row, int column, const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;

    QByteArray buf = data->data(kMimeType);
    QDataStream st(&buf, QIODevice::ReadOnly);
    int kind = -1;
    st >> kind;
    const QStringList target = targetFolderForDrop(parent);

    if (kind == 0) {
        QString name;
        st >> name;
        if (m_manager && m_manager->movePresetToGroup(name, groupFromDisplayPath(target)))
            emit presetDropped(name);
    } else if (kind == 1) {
        QStringList src;
        st >> src;
        QStringList newPath = target;
        newPath << src.last();
        if (relocateFolder(indexForFolderPath(src), newPath))
            emit folderDropped(newPath);
    }
    // The mutation goes through the manager, which emits presetsChanged → a full
    // rebuild. Return false so the view never tries to remove the source rows
    // itself (we are not in InternalMove and own the move end-to-end).
    return false;
}
