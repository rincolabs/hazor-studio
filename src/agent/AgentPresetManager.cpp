#include "AgentPresetManager.hpp"

#include "core/AppPaths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

AgentPresetManager::AgentPresetManager(QObject* parent)
    : QObject(parent)
{
    m_configDir = AppPaths::subDir(QStringLiteral("agents"));
}

void AgentPresetManager::loadAll()
{
    m_presets.clear();

    QDir dir(m_configDir);
    QStringList files = dir.entryList({"*.json"}, QDir::Files, QDir::Name);

    for (const auto& file : files) {
        QString path = dir.absoluteFilePath(file);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();

        if (!doc.isObject()) continue;

        AgentConfig cfg = AgentConfig::fromJson(doc.object());
        QString name = cfg.name.isEmpty()
            ? QFileInfo(file).completeBaseName()
            : cfg.name;
        cfg.name = name;
        m_presets.insert(name, cfg);
    }

    ensureDefaults();

    // Load active Assistant preset (active_agent.txt)
    QString activeFile = m_configDir + "/../active_agent.txt";
    QFile af(activeFile);
    if (af.open(QIODevice::ReadOnly)) {
        m_activeName = QString::fromUtf8(af.readAll()).trimmed();
        af.close();
    }
    if (!m_presets.contains(m_activeName) ||
        m_presets.value(m_activeName).kind != Assistant) {
        m_activeName = firstNameOfKind(Assistant);
        if (m_activeName.isEmpty())
            m_activeName = m_presets.firstKey();
    }

    // Load active Generative preset (active_generative.txt)
    QString genFile = m_configDir + "/../active_generative.txt";
    QFile gf(genFile);
    if (gf.open(QIODevice::ReadOnly)) {
        m_activeGenerative = QString::fromUtf8(gf.readAll()).trimmed();
        gf.close();
    }
    if (!m_presets.contains(m_activeGenerative) ||
        m_presets.value(m_activeGenerative).kind != Generative) {
        m_activeGenerative = firstNameOfKind(Generative);  // may be empty
    }

    emit presetsChanged();
}

QString AgentPresetManager::firstNameOfKind(AgentKind kind) const
{
    for (auto it = m_presets.constBegin(); it != m_presets.constEnd(); ++it)
        if (it.value().kind == kind)
            return it.key();
    return {};
}

void AgentPresetManager::save(const AgentConfig& preset)
{
    if (preset.name.isEmpty()) return;

    // Remove old entry if name changed
    QString oldKey;
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it) {
        if (it.key() != preset.name && it.value().name == preset.name) {
            oldKey = it.key();
            break;
        }
    }
    if (!oldKey.isEmpty() && oldKey != preset.name) {
        QFile::remove(filePath(oldKey));
        m_presets.remove(oldKey);
    }

    m_presets.insert(preset.name, preset);
    rename(preset.name, preset.name); // handles file write + old file cleanup

    emit presetsChanged();
}

void AgentPresetManager::remove(const QString& name)
{
    if (!m_presets.contains(name)) return;
    if (m_presets.size() <= 1) return;

    QFile::remove(filePath(name));
    m_presets.remove(name);

    if (m_activeName == name) {
        m_activeName = firstNameOfKind(Assistant);
        if (m_activeName.isEmpty())
            m_activeName = m_presets.firstKey();
        emit activePresetChanged(m_presets.value(m_activeName));
    }
    if (m_activeGenerative == name) {
        m_activeGenerative = firstNameOfKind(Generative);
        emit activeGenerativeChanged(m_presets.value(m_activeGenerative));
    }

    emit presetsChanged();
}

QString AgentPresetManager::duplicate(const QString& name)
{
    if (!m_presets.contains(name)) return {};

    AgentConfig copy = m_presets.value(name);
    QString base = copy.name;
    QString newName = base + " (copy)";
    int n = 2;
    while (m_presets.contains(newName))
        newName = QString("%1 (copy %2)").arg(base).arg(n++);

    copy.name = newName;
    m_presets.insert(newName, copy);

    QJsonDocument doc(copy.toJson());
    QFile f(filePath(newName));
    if (f.open(QIODevice::WriteOnly))
        f.write(doc.toJson(QJsonDocument::Indented));

    emit presetsChanged();
    return newName;
}

void AgentPresetManager::rename(const QString& oldName, const QString& newName)
{
    if (oldName == newName) {
        // Just save the file under the existing name
        if (!m_presets.contains(newName)) return;
        QJsonDocument doc(m_presets[newName].toJson());
        QFile f(filePath(newName));
        if (f.open(QIODevice::WriteOnly))
            f.write(doc.toJson(QJsonDocument::Indented));
        return;
    }

    if (!m_presets.contains(oldName)) return;

    AgentConfig cfg = m_presets.take(oldName);
    cfg.name = newName;
    m_presets.insert(newName, cfg);

    QFile::remove(filePath(oldName));

    QJsonDocument doc(cfg.toJson());
    QFile f(filePath(newName));
    if (f.open(QIODevice::WriteOnly))
        f.write(doc.toJson(QJsonDocument::Indented));

    if (m_activeName == oldName) {
        m_activeName = newName;
        emit activePresetChanged(cfg);
    }

    emit presetsChanged();
}

QStringList AgentPresetManager::presetNames() const
{
    return m_presets.keys();
}

QStringList AgentPresetManager::presetNames(AgentKind kind) const
{
    QStringList names;
    for (auto it = m_presets.constBegin(); it != m_presets.constEnd(); ++it)
        if (it.value().kind == kind)
            names.append(it.key());
    return names;
}

AgentConfig AgentPresetManager::preset(const QString& name) const
{
    return m_presets.value(name);
}

bool AgentPresetManager::exists(const QString& name) const
{
    return m_presets.contains(name);
}

void AgentPresetManager::setActivePreset(const QString& name)
{
    if (!m_presets.contains(name)) {
        return;
    }

    // Route by kind so a Generative preset never becomes the chat agent.
    if (m_presets.value(name).kind == Generative) {
        setActiveGenerative(name);
        return;
    }

    if (m_activeName == name) {
        return;
    }

    m_activeName = name;
    emit activePresetChanged(m_presets.value(name));

    QString activeFile = m_configDir + "/../active_agent.txt";
    QFile f(activeFile);
    if (f.open(QIODevice::WriteOnly))
        f.write(name.toUtf8());
}

void AgentPresetManager::setActiveGenerative(const QString& name)
{
    if (!m_presets.contains(name)) return;
    if (m_presets.value(name).kind != Generative) return;
    if (m_activeGenerative == name) return;

    m_activeGenerative = name;
    emit activeGenerativeChanged(m_presets.value(name));

    QString genFile = m_configDir + "/../active_generative.txt";
    QFile f(genFile);
    if (f.open(QIODevice::WriteOnly))
        f.write(name.toUtf8());
}

QString AgentPresetManager::sanitizedName(const QString& name) const
{
    QString safe = name;
    safe.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "_");
    if (safe.isEmpty()) safe = "agent";
    return safe;
}

QString AgentPresetManager::filePath(const QString& name) const
{
    return m_configDir + "/" + sanitizedName(name) + ".json";
}

void AgentPresetManager::ensureDefaults()
{
    if (!m_presets.isEmpty()) return;

    AgentConfig def;
    def.name = "Default Agent";
    def.description = "Default Ollama-based agent";

    QJsonDocument doc(def.toJson());
    QFile f(filePath(def.name));
    if (f.open(QIODevice::WriteOnly))
        f.write(doc.toJson(QJsonDocument::Indented));

    m_presets.insert(def.name, def);
}
