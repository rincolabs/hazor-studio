#include "RecentFilesManager.hpp"

#include <QFileInfo>
#include <QSettings>
#include <QVariantMap>

namespace {
constexpr char kSettingsKey[] = "file/recentFiles";
constexpr char kOpenedAtSettingsKey[] = "file/recentFilesOpenedAt";
}

RecentFilesManager* RecentFilesManager::instance()
{
    static RecentFilesManager inst;
    return &inst;
}

RecentFilesManager::RecentFilesManager(QObject* parent)
    : QObject(parent)
{
    load();
}

void RecentFilesManager::addFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute.isEmpty())
        return;
    m_files.removeAll(absolute);
    m_files.prepend(absolute);
    m_lastOpened.insert(absolute, QDateTime::currentDateTime());
    while (m_files.size() > kMaxEntries)
        m_lastOpened.remove(m_files.takeLast());
    save();
    emit changed();
}

void RecentFilesManager::removeFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (m_files.removeAll(absolute) == 0)
        return;
    m_lastOpened.remove(absolute);
    save();
    emit changed();
}

void RecentFilesManager::clear()
{
    if (m_files.isEmpty())
        return;
    m_files.clear();
    m_lastOpened.clear();
    save();
    emit changed();
}

void RecentFilesManager::load()
{
    QSettings settings;
    m_files = settings.value(kSettingsKey).toStringList();
    const QVariantMap openedAt = settings.value(kOpenedAtSettingsKey).toMap();
    while (m_files.size() > kMaxEntries)
        m_files.removeLast();

    for (const QString& path : m_files) {
        const QDateTime stored = QDateTime::fromString(
            openedAt.value(path).toString(), Qt::ISODateWithMs);
        m_lastOpened.insert(path, stored.isValid()
            ? stored
            : QFileInfo(path).lastModified());
    }
}

void RecentFilesManager::save() const
{
    QSettings settings;
    settings.setValue(kSettingsKey, m_files);
    QVariantMap openedAt;
    for (const QString& path : m_files) {
        const QDateTime timestamp = m_lastOpened.value(path);
        if (timestamp.isValid())
            openedAt.insert(path, timestamp.toString(Qt::ISODateWithMs));
    }
    settings.setValue(kOpenedAtSettingsKey, openedAt);
}

QList<RecentFileEntry> RecentFilesManager::entries() const
{
    QList<RecentFileEntry> result;
    result.reserve(m_files.size());
    for (const QString& path : m_files)
        result.append({path, m_lastOpened.value(path)});
    return result;
}
