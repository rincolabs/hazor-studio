#pragma once

#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QStringList>

struct RecentFileEntry {
    QString path;
    QDateTime lastOpened;
};

// Single source of truth for File > Open Recent, backed by QSettings so the
// history survives restarts. Every open path (File > Open, OS file-open
// events, RAW open, project Save As) funnels a successful open through
// addFile() here instead of keeping its own list.
class RecentFilesManager : public QObject {
    Q_OBJECT

public:
    static RecentFilesManager* instance();

    static constexpr int kMaxEntries = 10;

    // Most-recent-first, absolute paths.
    QStringList files() const { return m_files; }

    // The same recent-file ordering with the last successful opening time.
    // Existing installations only had the path list, so entries restored from
    // that older setting use the file modification time until they are opened
    // again and acquire an explicit timestamp.
    QList<RecentFileEntry> entries() const;

    // Moves `path` to the front (inserting it if new), trims to kMaxEntries
    // and persists. Call only after a file has *successfully* opened.
    void addFile(const QString& path);
    void removeFile(const QString& path);
    void clear();

signals:
    void changed();

private:
    explicit RecentFilesManager(QObject* parent = nullptr);

    void load();
    void save() const;

    QStringList m_files;
    QHash<QString, QDateTime> m_lastOpened;
};
