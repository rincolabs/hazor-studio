#pragma once

#include "ai/models/AiModelTypes.hpp"

#include <QList>
#include <QString>
#include <QStringList>

// Parsed view of the versioned model manifest (resources/models or a remote
// override). Invalid entries are skipped and reported per-model so a single bad
// entry never breaks the Settings dialog.
class AiModelManifest {
public:
    static constexpr int kSupportedVersion = 1;

    // Loads the bundled manifest from the Qt resource system.
    static AiModelManifest loadBundled();
    // Parses a manifest from raw JSON bytes.
    static AiModelManifest fromJson(const QByteArray& json);

    bool isValid() const { return m_valid; }
    int version() const { return m_version; }
    const QList<AiManifestEntry>& models() const { return m_models; }
    const QStringList& warnings() const { return m_warnings; } // per-entry problems
    QString error() const { return m_error; }                  // global parse error

    const AiManifestEntry* model(const QString& id) const;
    QList<AiManifestEntry> modelsOfFamily(const QString& family) const;

private:
    bool m_valid = false;
    int m_version = 0;
    QList<AiManifestEntry> m_models;
    QStringList m_warnings;
    QString m_error;
};
