#pragma once

#include <QWidget>
#include <functional>
#include "brush/BrushPreset.hpp"
#include "brush/BrushTypes.hpp"

class BrushPresetManager;
class BrushLibraryTreeModel;
class BrushPresetItemDelegate;
class BrushPreviewCache;
class QTreeView;
class QLineEdit;
class QTimer;
class QMenu;
class QModelIndex;

// How a BrushLibraryListView behaves. The Brush Settings panel owns full preset
// and folder management; the Brushes panel is a select-only quick picker.
enum class BrushPresetListMode {
    SettingsManager,    // save / save as / duplicate / delete / new / rename / DnD
    BrushPanelSelector  // list + select only, no administrative actions
};

// Reusable brush library list: a search field + a virtualized folder tree of
// presets (BrushLibraryTreeModel + BrushPresetItemDelegate), with its own preview
// cache and async warm. Used by both the Brushes panel and the Brush Settings
// panel so the folder listing lives in exactly one place. In SettingsManager mode
// it also hosts the full Preset Manager (context menu, drag&drop, and the actions
// the panel's ≡ menu calls).
class BrushLibraryListView : public QWidget {
    Q_OBJECT
public:
    explicit BrushLibraryListView(
        BrushPresetListMode mode = BrushPresetListMode::BrushPanelSelector,
        QWidget* parent = nullptr);
    ~BrushLibraryListView() override;

    void setPresetManager(BrushPresetManager* manager);
    void warmCacheAsync();
    void setCurrentPreset(const QString& name);   // select + scroll, no signal echo
    void revealPreset(const QString& name);

    BrushPresetListMode mode() const { return m_mode; }

    // SettingsManager only: supplies the live brush settings to save/save-as.
    void setCurrentSettingsProvider(std::function<BrushSettings()> provider);
    // Populate a menu with the Preset Manager actions for the panel's ≡ button.
    void populateManagerMenu(QMenu* menu);

    // Preset Manager actions (no-ops unless SettingsManager + a manager is set).
    void saveCurrentPreset();        // overwrite selected, else redirect to Save As
    void saveCurrentPresetAs();
    void duplicateSelectedPreset();
    void deleteSelected();           // selected preset or folder
    void createNewPreset();
    void createNewFolder();          // empty folder under the current selection
    void renameSelected();           // selected preset or folder
    void reloadPresets();            // re-read the library from disk (manager)
    void resetDefaultPresets();      // wipe library and rebuild defaults (confirms)

    // Snapshot of the current tree selection, for driving the Brush Settings top
    // bar's enabled/disabled states. The action methods above remain the single
    // implementation; this only describes what they would target.
    enum class SelectionKind { None, Preset, Folder };
    struct Selection {
        SelectionKind kind = SelectionKind::None;
        // True for a protected selection that cannot be renamed/deleted in place.
        // Matches the library's existing protection model: the two virtual root
        // folders are protected; every preset is editable (Duplicate always allowed).
        bool protectedItem = false;
    };
    Selection currentSelection() const;

protected:
    // The Brushes dock starts hidden/floating, so a startup setCurrentPreset()
    // selects logically but the tree never paints the highlight (rows aren't laid
    // out until shown). Re-apply the pending selection on first show so the visual
    // selection appears with real geometry.
    void showEvent(QShowEvent* event) override;

signals:
    void presetSelected(const BrushPreset& preset);
    void openInSettingsRequested();
    void importBrushesRequested();
    // Raised whenever the current tree row changes (preset, folder, or cleared) so
    // the host can re-evaluate which management actions apply.
    void selectionChanged();

private:
    void onCurrentRowChanged(const QModelIndex& current);
    void selectTreeIndex(const QModelIndex& index);
    void applyTreeExpansion();
    void showTreeContextMenu(const QPoint& pos);
    void showSourceInfo(const QModelIndex& index);
    void applyTheme();

    // Preset Manager internals.
    QModelIndex currentIndex() const;
    QString selectedPresetName() const;          // empty if a folder/nothing
    QString groupForSelection() const;           // folder to create/save into
    void deletePresetByName(const QString& name);
    void deleteFolder(const QModelIndex& folderIndex);
    void createFolderUnder(const QModelIndex& parentFolderIndex);
    void renamePreset(const QString& name);
    void renameFolder(const QModelIndex& folderIndex);
    bool runSaveAsDialog(QString* outName, QString* outGroup);
    // Reveal a preset by name and emit presetSelected so the host applies it.
    void selectPresetByName(const QString& name);

    BrushPresetListMode m_mode;
    BrushPresetManager* m_manager = nullptr;
    BrushLibraryTreeModel* m_treeModel = nullptr;
    BrushPresetItemDelegate* m_listDelegate = nullptr;
    BrushPreviewCache* m_cache = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeView* m_tree = nullptr;
    QTimer* m_searchDebounce = nullptr;
    qint64 m_lastFilterMs = 0;
    bool m_syncing = false;
    bool m_warming = false;
    // Last preset asked to be current. Re-applied on showEvent (and after a model
    // rebuild) so the highlight survives being selected while hidden.
    QString m_pendingPresetName;
    std::function<BrushSettings()> m_settingsProvider;
};
