#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class QJsonObject;

// A single named color inside a collection.
struct SwatchColor {
    QString id;
    QString name;   // optional human label; empty → derived from hex
    QColor  color;
};

// A node in the swatch tree. A node is either a Folder (holds child nodes,
// supports unlimited nesting) or a Collection (holds an ordered list of
// colors). The model intentionally keeps both payloads on one struct so the
// tree is uniform and easy to (de)serialize.
class SwatchNode {
public:
    enum class Type { Folder, Collection };

    QString id;
    QString name;
    Type    type = Type::Collection;

    // Collection payload
    QVector<SwatchColor> colors;

    // Folder payload
    std::vector<std::unique_ptr<SwatchNode>> children;

    SwatchNode* parent = nullptr;

    bool isFolder() const { return type == Type::Folder; }
    bool isCollection() const { return type == Type::Collection; }
};

// Centralized, application-wide store of reusable color palettes.
//
// Single source of truth for every color-palette consumer (Swatches panel,
// ColorPaletteBar, and future Assets/Styles/Themes). Access via the process
// singleton `SwatchManager::instance()`. All mutations emit `changed()` (broad)
// plus a granular signal, and auto-persist through QSettings so the structure
// survives restarts.
class SwatchManager : public QObject {
    Q_OBJECT

public:
    static SwatchManager* instance();

    // --- Tree access -------------------------------------------------------
    const std::vector<std::unique_ptr<SwatchNode>>& roots() const { return m_roots; }
    SwatchNode* findNode(const QString& id) const;
    SwatchNode* findCollection(const QString& id) const; // nullptr unless a Collection

    // Flattened list of every collection with a "Folder/Sub" display path.
    struct CollectionRef { QString id; QString name; QString path; };
    QVector<CollectionRef> allCollections() const;

    QString firstCollectionId() const;

    // --- Structural mutations ---------------------------------------------
    // parentId empty → create at root. Returns the new node id.
    QString createCollection(const QString& name, const QString& parentId = QString());
    QString createFolder(const QString& name, const QString& parentId = QString());
    void renameNode(const QString& id, const QString& name);
    void removeNode(const QString& id);  // removes node and all descendants
    void moveNode(const QString& id, const QString& newParentId, int index = -1);

    // --- Color mutations ---------------------------------------------------
    QString addColor(const QString& collectionId, const QColor& color,
                     const QString& name = QString());
    void removeColor(const QString& collectionId, const QString& colorId);
    void setColor(const QString& collectionId, const QString& colorId,
                  const QColor& color, const QString& name = QString());
    void reorderColors(const QString& collectionId, const QVector<QString>& orderedColorIds);

    // --- Persistence -------------------------------------------------------
    void load();
    void save() const;

    // Seed palette: the colors historically shown by ColorPaletteBar. Used to
    // build the "Default Swatches" collection on first run.
    static QVector<QColor> defaultPaletteColors();
    static QString defaultCollectionName() { return QStringLiteral("Default Swatches"); }

    static QString displayName(const SwatchColor& c);

signals:
    void changed();                                   // structure and/or colors
    void structureChanged();                           // folders/collections added/removed/moved/renamed
    void collectionColorsChanged(const QString& collectionId);

private:
    explicit SwatchManager(QObject* parent = nullptr);

    SwatchNode* nodeById(const std::vector<std::unique_ptr<SwatchNode>>& nodes,
                         const QString& id) const;
    std::vector<std::unique_ptr<SwatchNode>>* childListForParent(const QString& parentId);
    void collectCollections(const SwatchNode* node, const QString& prefix,
                            QVector<CollectionRef>& out) const;

    static QString makeId(const QString& prefix);
    void seedDefaults();

    QJsonObject nodeToJson(const SwatchNode* node) const;
    std::unique_ptr<SwatchNode> nodeFromJson(const QJsonObject& obj, SwatchNode* parent);

    // Path to the standalone swatches file (kept separate from QSettings).
    static QString swatchesFilePath();

    void touch();              // emit changed() + persist
    void touchStructure();     // emit structureChanged() + changed() + persist
    void touchColors(const QString& collectionId);

    std::vector<std::unique_ptr<SwatchNode>> m_roots;
};
