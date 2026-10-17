#include "RecentFilesManager.hpp"

#include <QFileInfo>
#include <QSettings>

namespace {
constexpr char kSettingsKey[] = "file/recentFiles";
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
    while (m_files.size() > kMaxEntries)
        m_files.removeLast();
    save();
    emit changed();
}

void RecentFilesManager::removeFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (m_files.removeAll(absolute) == 0)
        return;
    save();
    emit changed();
}

void RecentFilesManager::clear()
{
    if (m_files.isEmpty())
        return;
    m_files.clear();
    save();
    emit changed();
}

void RecentFilesManager::load()
{
    QSettings settings;
    m_files = settings.value(kSettingsKey).toStringList();
    while (m_files.size() > kMaxEntries)
        m_files.removeLast();
}

void RecentFilesManager::save() const
{
    QSettings settings;
    settings.setValue(kSettingsKey, m_files);
}
