#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

#include "brush/BrushPreset.hpp"

class BrushPresetManager;

enum class BrushLibraryNodeType {
    Folder,
    BrushPreset
};

class BrushLibraryTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        NodeTypeRole,
        PresetNameRole,
        FolderIdRole,
        BrushCountRole,
        SourceSubtitleRole
    };

    explicit BrushLibraryTreeModel(QObject* parent = nullptr);
    ~BrushLibraryTreeModel() override;

    void setPresetManager(BrushPresetManager* manager);
    BrushPresetManager* presetManager() const { return m_manager; }

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    const BrushPreset* presetForIndex(const QModelIndex& index) const;
    bool isFolder(const QModelIndex& index) const;
    bool hasFilter() const { return !m_filter.isEmpty(); }

    QModelIndex indexForPresetName(const QString& name) const;
    QVector<QModelIndex> folderIndexes() const;

    void setFilter(const QString& needle);
    QString filter() const { return m_filter; }

    bool folderExpanded(const QModelIndex& index) const;
    void setFolderExpanded(const QModelIndex& index, bool expanded);

    // ── Preset Manager helpers (Brush Settings) ───────────────────
    // Turns drag&drop on/off and grants the drag/drop item flags. The Brush Panel
    // (selector mode) leaves this off so the tree is select-only.
    void setEditingEnabled(bool on);
    bool editingEnabled() const { return m_editingEnabled; }

    // Display path (folder names from the root) for a folder index.
    QStringList folderDisplayPath(const QModelIndex& index) const;
    // Top-level virtual root ("Default Brushes"/"Imported Brushes"/…) the index
    // lives under.
    QString topRootName(const QModelIndex& index) const;
    // A folder the user may rename/move/delete: a non-root folder under
    // "Default Brushes" (imported/source-derived folders are protected).
    bool isManagedFolder(const QModelIndex& index) const;
    // A preset that may be moved/reorganised (hand-made, under Default Brushes).
    bool isManagedPreset(const QModelIndex& index) const;
    // Folder/preset names directly and recursively under a folder index.
    QStringList presetNamesUnder(const QModelIndex& folderIndex) const;
    // The folder a preset/folder index belongs to (its parent folder display
    // path); for the root or an invalid index this is ["Default Brushes"].
    QStringList targetFolderForDrop(const QModelIndex& index) const;
    // Folder index addressed by a display path, or invalid.
    QModelIndex indexForFolderPath(const QStringList& displayPath) const;
    // Every user-managed folder display path (incl. the Default Brushes root),
    // for the Save As destination picker.
    QVector<QStringList> managedFolderDisplayPaths() const;

    // Convert a folder display path to the value stored in BrushPreset::group
    // (the full display path, root included — authoritative once organised).
    static QString groupFromDisplayPath(const QStringList& displayPath);
    // Convert a free-typed folder path (e.g. "My Brushes/Sketch") into a stored
    // group, placing rootless paths under Default Brushes.
    static QString groupFromUserPath(const QString& text);
    // The display path of the default user root, for new folders in empty space.
    static QStringList defaultRootPath();

    // Group a preset created/saved "into" this index's folder should carry. Returns
    // false when the index resolves outside the user's Default Brushes tree
    // (imported/protected) so the caller can fall back to the root group "".
    bool saveGroupForIndex(const QModelIndex& index, QString* outGroup) const;

    // Rename/move a folder: recompute the group of every descendant preset for the
    // folder's new display path and apply via the manager. Returns false if the
    // move is illegal (into itself/descendant, name clash, protected target).
    bool relocateFolder(const QModelIndex& folderIndex,
                        const QStringList& newFolderDisplayPath);

    // QAbstractItemModel drag&drop.
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;

signals:
    // Emitted right after a drag&drop move (the model has already rebuilt) so the
    // view can re-select the relocated item, as the spec requires.
    void presetDropped(const QString& presetName);
    void folderDropped(const QStringList& newFolderDisplayPath);

private slots:
    void rebuild();

private:
    struct Node {
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        QString id;
        QString name;
        BrushLibraryNodeType type = BrushLibraryNodeType::Folder;
        int presetIndex = -1;
        int brushCount = 0;
        bool builtIn = false;
        bool imported = false;
    };

    Node* nodeForIndex(const QModelIndex& index) const;
    QModelIndex indexForNode(const Node* node) const;
    int rowOfNode(const Node* node) const;

    Node* ensureFolder(Node* parent, const QStringList& path, int depth,
                       bool imported, bool builtIn);
    Node* ensureExplicitFolder(Node* parent, const QStringList& path, int depth);
    void insertPreset(int presetIndex);
    bool presetMatchesFilter(const BrushPreset& preset, const QStringList& folderPath) const;
    QStringList folderPathForPreset(const BrushPreset& preset) const;
    QString sourceTypeId(const BrushPreset& preset) const;
    QString sourceSubtitle(const BrushPreset& preset) const;
    QString folderIdFor(const QStringList& path) const;
    bool storedExpandedState(const QString& folderId) const;
    void collectFolderIndexes(const Node* parent, QVector<QModelIndex>& out) const;

    QStringList displayPathForNode(const Node* node) const;
    QString topRootForNode(const Node* node) const;
    void collectPresetNames(const Node* node, QStringList& out) const;
    void collectManagedFolderPaths(const Node* node, QVector<QStringList>& out) const;
    const Node* folderNodeForPath(const Node* parent, const QStringList& path, int depth) const;

    BrushPresetManager* m_manager = nullptr;
    QString m_filter;
    std::unique_ptr<Node> m_root;
    QHash<QString, bool> m_expandedFolders;
    bool m_editingEnabled = false;
};
