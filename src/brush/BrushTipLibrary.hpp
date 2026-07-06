#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>

// One reusable brush tip (the bitmap stamp a brush paints with). Tips are global
// editor resources (like brush textures or swatches), not owned by any single
// preset — a preset only stores the id. Pixels are kept as ARGB32 so a tip can
// carry both coverage (alpha) and its own colours (colour-stamp tips).
struct BrushTip {
    QString id;        // unique key used by presets
    QString name;      // display name
    QString path;      // source file when loaded from disk (may be empty)
    QImage image;      // ARGB32 pixels
    quint64 contentKey = 0;   // hash of the pixels, for content-based dedup

    QSize size() const { return image.size(); }
    bool isNull() const { return image.isNull(); }
};

// Global, in-session library of brush tips. Mirrors BrushTextureLibrary: tips are
// populated from a resources folder on disk plus tips ingested from imported
// brushes/bundles and presets for the current session, so a tip stays a reusable
// resource available to every brush rather than being trapped inside one preset.
class BrushTipLibrary : public QObject {
    Q_OBJECT
public:
    static BrushTipLibrary* instance();

    const QVector<BrushTip>& tips() const { return m_tips; }
    const BrushTip* find(const QString& id) const;

    // Register a tip under a display name; returns its id. If a tip with the same
    // pixel content already exists it is kept (no duplicate). The image is stored
    // as ARGB32. Emits tipsChanged() when something new is added.
    QString add(const QString& name, const QImage& image, const QString& path = {});

    // Load an image file and register it. Returns the id, or empty on failure.
    QString importFromFile(const QString& path);

    void remove(const QString& id);

    // Scan the on-disk resource folders (idempotent; safe to call repeatedly).
    void reload();

    // Scan once, the first time it is needed (mirrors BrushTextureLibrary), so a
    // preset/apply path can rely on the bundled tips being present even before any
    // Brush Settings panel has opened. No-op after the first scan.
    void ensureLoaded();

    // Default folder bundled with the app (src/resources/brushes/tips), embedded
    // in the Qt resource system so every default tip ships inside the binary.
    static QString defaultTipDir();

signals:
    void tipsChanged();

private:
    explicit BrushTipLibrary(QObject* parent = nullptr);
    static QString makeId(const QString& name);
    static quint64 contentHash(const QImage& argb32);
    void scanFolder(const QString& dir);

    QVector<BrushTip> m_tips;
    bool m_scanned = false;
};
