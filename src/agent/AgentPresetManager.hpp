#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include "AgentConfig.hpp"

class AgentPresetManager : public QObject {
    Q_OBJECT

public:
    explicit AgentPresetManager(QObject* parent = nullptr);

    void loadAll();
    void save(const AgentConfig& preset);
    void remove(const QString& name);
    QString duplicate(const QString& name);
    void rename(const QString& oldName, const QString& newName);

    QStringList presetNames() const;
    QStringList presetNames(AgentKind kind) const;
    AgentConfig preset(const QString& name) const;
    bool exists(const QString& name) const;

    QString activePresetName() const { return m_activeName; }
    AgentConfig activePreset() const { return m_presets.value(m_activeName); }
    void setActivePreset(const QString& name);

    // Kind-aware active presets — Assistant and Generative track independently.
    // The Assistant active preset is the legacy m_activeName (active_agent.txt).
    QString activeAssistantName() const { return m_activeName; }
    QString activeGenerativeName() const { return m_activeGenerative; }
    AgentConfig activeAssistant() const { return m_presets.value(m_activeName); }
    AgentConfig activeGenerative() const { return m_presets.value(m_activeGenerative); }
    void setActiveGenerative(const QString& name);

    QString configDir() const { return m_configDir; }

signals:
    void presetsChanged();
    void activePresetChanged(const AgentConfig& config);
    void activeGenerativeChanged(const AgentConfig& config);

private:
    QString sanitizedName(const QString& name) const;
    QString filePath(const QString& name) const;
    void ensureDefaults();
    QString firstNameOfKind(AgentKind kind) const;

    QMap<QString, AgentConfig> m_presets;
    QString m_activeName;          // active Assistant preset (active_agent.txt)
    QString m_activeGenerative;    // active Generative preset (active_generative.txt)
    QString m_configDir;
};
