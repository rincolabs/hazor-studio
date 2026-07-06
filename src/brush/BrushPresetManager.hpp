#pragma once

#include "BrushPreset.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

class BrushPresetManager : public QObject {
    Q_OBJECT

public:
    explicit BrushPresetManager(QObject* parent = nullptr);

    void loadPresets();
    void savePresets();

    // Re-read the library from disk and notify listeners (loadPresets() itself is
    // the silent startup loader). Used by the Brush Settings "Reload" action.
    void reloadFromDisk();

    // Discard the entire user library and rebuild the built-in default kit:
    // delete brush_presets.json, re-seed the defaults, persist, and notify
    // listeners. Irreversible — the "Reset Default Brushes" action.
    void resetToDefaults();

    void addPreset(const BrushPreset& preset);
    void removePreset(const QString& name);
    BrushPreset* findPreset(const QString& name);

    // Bulk insert used by brush import. Saves once and emits presetsChanged once
    // (far cheaper than addPreset per item). When persist is false the presets
    // stay in memory only (the "Current Session" import destination): available
    // until the app closes, never written to disk.
    void addPresets(const std::vector<BrushPreset>& presets, bool persist = true);

    bool contains(const QString& name) const;
    // A name not already in the library, appending " 2", " 3"… as needed.
    QString uniqueName(const QString& base) const;

    // ── Preset Manager operations (Brush Settings) ────────────────
    // These are the single source of truth behind the Brush Settings preset
    // manager: each one mutates m_presets, persists, and emits presetsChanged
    // (so every list view rebuilds). Preset names stay globally unique, which the
    // selection/find-by-name machinery relies on.

    // Overwrite an existing preset's settings, keeping its name and folder. No-op
    // (returns false) if the name is unknown.
    bool overwritePreset(const QString& name, const BrushSettings& settings);

    // Create/replace a preset 'name' carrying 'settings' in folder 'group'. If a
    // preset with that name exists it is replaced (the "substituir" path).
    bool saveAsNewPreset(const QString& name, const QString& group,
                         const BrushSettings& settings);

    // Duplicate 'name' into the same folder with a unique "(copy)"/"(copy N)"
    // name. Returns the new name (empty on failure).
    QString duplicatePreset(const QString& name);

    // Create a fresh preset with default BrushSettings inside 'group', named
    // "New Brush Preset"/"New Brush Preset 2"… Returns the new name.
    QString createDefaultPreset(const QString& group);

    // Rename a preset. Fails if newName is empty or already used by another
    // preset. Returns true on success.
    bool renamePreset(const QString& oldName, const QString& newName);

    // Move a single preset to another folder (group = full display path now).
    bool movePresetToGroup(const QString& name, const QString& newGroup);

    // ── Folders ───────────────────────────────────────────────────
    // Folders are persisted so empty ones survive. Each entry is a full display
    // path (e.g. "Default Brushes/My Brushes/Sketch"); ancestors are kept too.
    const QStringList& folders() const { return m_folders; }
    // Create a folder and all its ancestors (no-op for ones already present).
    void addFolder(const QString& fullPath);
    // Rename/move a folder subtree: rewrite folder entries under oldFull to newFull
    // and apply the matching preset group changes, in one persisted batch.
    void relocateFolderData(const QString& oldFull, const QString& newFull,
                            const std::vector<std::pair<QString, QString>>& presetGroupChanges);
    // Delete a folder subtree: drop folder entries under fullPath and the listed
    // presets, in one persisted batch.
    void removeFolderTree(const QString& fullPath, const QStringList& presetNames);

    const std::vector<BrushPreset>& presets() const { return m_presets; }
    QStringList presetNames() const;

signals:
    void presetsChanged();

private:
    QString configPath() const;
    // Build the built-in professional brush kit (categories, tips, textures,
    // dynamics) into m_presets/m_folders. Called by loadPresets() on first run
    // (when no user brush_presets.json exists), so deleting that file resets the
    // library to this kit. The definition lives in C++ as the single source of
    // truth; tips ship as PNGs in :/brushes/tips and textures in
    // :/brushes/textures, referenced by path/id (never duplicated per preset).
    void seedDefaultPresets();
    std::vector<BrushPreset> m_presets;
    QStringList m_folders;   // persisted folder paths (incl. ancestors)
};
