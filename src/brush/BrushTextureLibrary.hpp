#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>

// One reusable brush texture/pattern. Textures are global editor resources (like
// brush tips or swatches), not owned by any single preset — a preset only stores
// the id. The pixels are kept as Grayscale8 (what the dab engine samples).
struct BrushTexture {
    QString id;        // unique key used by presets
    QString name;      // display name
    QString path;      // source file when loaded from disk (may be empty)
    QImage image;      // Grayscale8 pixels
    quint64 contentKey = 0;   // hash of the pixels, for content-based dedup

    QSize size() const { return image.size(); }
    bool isNull() const { return image.isNull(); }
};

// Global, in-session library of brush textures. Phase 1: populated from a
// resources folder on disk plus textures ingested from imported brushes/bundles
// for the current session (no persistent ResourceManager yet — that is Phase 2).
class BrushTextureLibrary : public QObject {
    Q_OBJECT
public:
    static BrushTextureLibrary* instance();

    const QVector<BrushTexture>& textures() const { return m_textures; }
    const BrushTexture* find(const QString& id) const;

    // Register a texture under a display name; returns its id. If a texture with
    // the same id already exists it is kept (no duplicate). The image is converted
    // to Grayscale8. Emits texturesChanged() when something new is added.
    QString add(const QString& name, const QImage& image, const QString& path = {});

    // Load an image file and register it. Returns the id, or empty on failure.
    QString importFromFile(const QString& path);

    void remove(const QString& id);

    // Scan the on-disk resource folders (idempotent; safe to call repeatedly).
    void reload();

    // Scan once, the first time it is needed. Lets preset loading/apply resolve a
    // textureId to pixels even before any texture panel has opened (which is what
    // triggers reload()). Cheap to call repeatedly: a no-op after the first scan.
    void ensureLoaded();

    // Default folder bundled with the app (src/resources/brushes/textures).
    static QString defaultTextureDir();

signals:
    void texturesChanged();

private:
    explicit BrushTextureLibrary(QObject* parent = nullptr);
    static QString makeId(const QString& name);
    static quint64 contentHash(const QImage& grayscale8);
    void scanFolder(const QString& dir);

    QVector<BrushTexture> m_textures;
    bool m_scanned = false;
};
