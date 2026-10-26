#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QDockWidget;
class QMainWindow;
class QTimer;

// A named panel arrangement. `state` is a QMainWindow::saveState() blob taken
// with WorkspaceManager::kStateVersion; it carries dock positions and sizes,
// visibility, tab groups with their order and current tab, splitter positions
// and the geometry of floating panels. Open documents are deliberately not
// part of it — a workspace is the shape of the interface, not the session.
struct WorkspacePreset {
    QString name;
    QByteArray state;
};

// What WorkspaceManager needs from the window it manages. It exists because
// the live QMainWindow state is not always the state that belongs to the
// workspace: on the Home tab every editing dock is hidden on purpose and the
// real arrangement lives in an in-memory snapshot, so both capture and apply
// have to go through the window rather than around it.
class WorkspaceHost {
public:
    virtual ~WorkspaceHost() = default;

    // The dock layout that belongs to the workspace right now.
    virtual QByteArray captureWorkspaceState() const = 0;
    // Returns false when `state` is not a layout this build can restore; the
    // caller then leaves whatever is on screen alone.
    virtual bool applyWorkspaceState(const QByteArray& state) = 0;
    // The built-in arrangement. It is the first-run layout and the fallback
    // for anything stored that no longer restores.
    virtual void applyDefaultWorkspaceState() = 0;
};

// Owns everything about workspace layouts: capturing them from the window,
// versioning and storing them in QSettings, restoring them at startup and
// applying them on demand.
//
// Two kinds of stored layout, deliberately separate:
//   * the *working* layout — rewritten automatically whenever the user moves
//     a panel or the window, and restored on the next launch;
//   * named *presets* — only ever written by an explicit save, so rearranging
//     panels after loading one never edits it behind the user's back.
class WorkspaceManager : public QObject {
    Q_OBJECT

public:
    // Handed to QMainWindow::saveState()/restoreState(). Bump it whenever the
    // set of dock object names changes in a way that makes older blobs
    // meaningless: Qt then rejects them itself and we fall back to the default
    // layout instead of restoring something half-wrong.
    static constexpr int kStateVersion = 1;

    // Schema version of the QSettings keys below. Bumping it discards every
    // stored workspace once, which is the escape hatch for a change Qt's own
    // state version cannot describe (e.g. a different set of settings keys).
    static constexpr int kSettingsVersion = 1;

    WorkspaceManager(QMainWindow* window, WorkspaceHost* host, QObject* parent = nullptr);

    // ── Working layout ───────────────────────────────────────────────
    // Applies what the previous session left behind: first the window
    // geometry, then the dock layout. The order matters — dock sizes are
    // stored in pixels, so they only land back where the user left them if
    // the window is already the size it will be shown at. Returns false when
    // the dock layout could not be restored (first run, or a blob Qt
    // rejected), in which case the caller's default layout stands untouched.
    bool restoreWorkingLayout();

    // Whether restoreWorkingLayout() found and applied a stored geometry. The
    // caller uses it to choose between show() and the first-run maximize.
    bool windowGeometryRestored() const { return m_geometryRestored; }

    // Starts honouring scheduleSave(). Call it once the window is on screen,
    // so the half-built geometry of a window still under construction is never
    // what gets persisted.
    void beginTracking();

    // Debounced, drag-aware persist of the working layout. Safe to call from
    // every signal that might mean "the layout changed".
    void scheduleSave();

    // Immediate persist, for shutdown.
    void saveNow();

    // Connect the dock signals that mean the arrangement changed.
    void watchDocks(const QList<QDockWidget*>& docks);

    // Qt creates the tab bar of a tabified dock group on the fly and destroys
    // it when the group breaks up, so there is nothing to connect once at
    // startup. Call this after every layout change: it is what makes a tab
    // dragged to another slot *inside its own group* — which emits no
    // QDockWidget signal at all — reach the autosave.
    void watchDockTabBars();

    // ── Presets ──────────────────────────────────────────────────────
    QStringList presetNames() const;
    bool hasPreset(const QString& name) const;

    // Empty while the working layout belongs to no preset (first run, after a
    // reset, or after the active preset was deleted).
    QString activePresetName() const { return m_activePresetName; }

    bool loadPreset(const QString& name);
    // Creates `name` or overwrites it with the current layout, and makes it
    // the active preset. Asking the user about an overwrite is the caller's
    // job — see MainWindow_Workspaces.cpp.
    void savePreset(const QString& name);
    bool renamePreset(const QString& from, const QString& to);
    bool deletePreset(const QString& name);

    // Back to the built-in arrangement; no preset is active afterwards.
    void resetToDefault();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void load();
    void writeWorkingLayout() const;
    void writePresets() const;
    int indexOfPreset(const QString& name) const;

    QMainWindow* m_window = nullptr;
    WorkspaceHost* m_host = nullptr;

    QVector<WorkspacePreset> m_presets;
    QString m_activePresetName;

    QByteArray m_workingState;
    QByteArray m_workingGeometry;

    QTimer* m_saveTimer = nullptr;
    bool m_geometryRestored = false;
    bool m_tracking = false;
    bool m_applying = false; // suppresses the saves our own restore would cause
};
