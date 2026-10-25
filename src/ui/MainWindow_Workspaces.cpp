#include "MainWindow.hpp"

#include "ui/AppMessageDialog.hpp"
#include "ui/WorkspaceManager.hpp"

#include <QActionGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>

// Window → Workspaces: the whole user-facing side of workspace persistence.
// The storage, versioning and autosave live in WorkspaceManager; everything
// here is menu structure and the three dialogs (name, overwrite, delete).

namespace {

// Shown for the built-in arrangement, which is always available and can be
// neither overwritten nor deleted.
QString defaultWorkspaceLabel() { return MainWindow::tr("Default"); }

// Trimmed user input, or an empty string when the user cancelled or typed
// nothing usable.
QString askWorkspaceName(QWidget* parent, const QString& title,
                         const QString& label, const QString& initial)
{
    bool ok = false;
    const QString name = QInputDialog::getText(parent, title, label,
                                               QLineEdit::Normal, initial, &ok);
    if (!ok)
        return QString();
    return name.trimmed();
}

} // namespace

void MainWindow::rebuildWorkspacesMenu()
{
    if (!m_workspacesMenu || !m_workspace)
        return;

    // clear() deletes the actions but not the group that held them together,
    // and this runs on every open.
    qDeleteAll(m_workspacesMenu->findChildren<QActionGroup*>());
    m_workspacesMenu->clear();

    // One exclusive group so the list reads as "which workspace am I in?".
    auto* group = new QActionGroup(m_workspacesMenu);
    group->setExclusive(true);

    const QString active = m_workspace->activePresetName();

    auto* defaultAction = m_workspacesMenu->addAction(defaultWorkspaceLabel());
    defaultAction->setCheckable(true);
    defaultAction->setChecked(active.isEmpty());
    defaultAction->setActionGroup(group);
    connect(defaultAction, &QAction::triggered, this, &MainWindow::onResetWorkspace);

    const QStringList names = m_workspace->presetNames();
    for (const QString& name : names) {
        QAction* action = m_workspacesMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == active);
        action->setActionGroup(group);
        connect(action, &QAction::triggered, this,
                [this, name]() { onLoadWorkspace(name); });
    }

    m_workspacesMenu->addSeparator();

    // The preset-only commands stay visible but disabled on the default
    // workspace, so the menu always has the same shape.
    const bool hasActivePreset = !active.isEmpty();

    auto* saveAction = m_workspacesMenu->addAction(tr("&Save Workspace"));
    saveAction->setEnabled(hasActivePreset);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveWorkspace);

    auto* saveAsAction = m_workspacesMenu->addAction(tr("Save Workspace &As..."));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveWorkspaceAs);

    auto* renameAction = m_workspacesMenu->addAction(tr("&Rename Workspace..."));
    renameAction->setEnabled(hasActivePreset);
    connect(renameAction, &QAction::triggered, this, &MainWindow::onRenameWorkspace);

    auto* deleteAction = m_workspacesMenu->addAction(tr("&Delete Workspace"));
    deleteAction->setEnabled(hasActivePreset);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteWorkspace);

    m_workspacesMenu->addSeparator();

    auto* resetAction = m_workspacesMenu->addAction(tr("Reset &Workspace"));
    connect(resetAction, &QAction::triggered, this, &MainWindow::onResetWorkspace);
}

void MainWindow::onLoadWorkspace(const QString& name)
{
    if (!m_workspace)
        return;
    if (!m_workspace->loadPreset(name)) {
        AppMessageDialog::warning(this, tr("Load Workspace"),
            tr("The workspace \"%1\" could not be applied and was left unchanged.")
                .arg(name));
    }
}

void MainWindow::onSaveWorkspace()
{
    if (!m_workspace)
        return;
    const QString active = m_workspace->activePresetName();
    if (active.isEmpty())
        return; // the built-in layout is never overwritten
    m_workspace->savePreset(active);
}

void MainWindow::onSaveWorkspaceAs()
{
    if (!m_workspace)
        return;

    const QString name = askWorkspaceName(this, tr("Save Workspace As"),
                                          tr("Workspace name:"), QString());
    if (name.isEmpty())
        return;

    if (name.compare(defaultWorkspaceLabel(), Qt::CaseInsensitive) == 0) {
        AppMessageDialog::warning(this, tr("Save Workspace As"),
            tr("\"%1\" is the built-in workspace. Choose another name.")
                .arg(defaultWorkspaceLabel()));
        return;
    }

    if (m_workspace->hasPreset(name)) {
        if (!AppMessageDialog::confirm(this, tr("Save Workspace As"),
                tr("A workspace named \"%1\" already exists. Replace it?").arg(name)))
            return;
    }

    m_workspace->savePreset(name);
}

void MainWindow::onRenameWorkspace()
{
    if (!m_workspace)
        return;
    const QString active = m_workspace->activePresetName();
    if (active.isEmpty())
        return;

    const QString name = askWorkspaceName(this, tr("Rename Workspace"),
                                          tr("Workspace name:"), active);
    if (name.isEmpty() || name == active)
        return;

    if (name.compare(defaultWorkspaceLabel(), Qt::CaseInsensitive) == 0) {
        AppMessageDialog::warning(this, tr("Rename Workspace"),
            tr("\"%1\" is the built-in workspace. Choose another name.")
                .arg(defaultWorkspaceLabel()));
        return;
    }

    if (m_workspace->hasPreset(name)) {
        if (!AppMessageDialog::confirm(this, tr("Rename Workspace"),
                tr("A workspace named \"%1\" already exists. Replace it?").arg(name)))
            return;
    }

    m_workspace->renamePreset(active, name);
}

void MainWindow::onDeleteWorkspace()
{
    if (!m_workspace)
        return;
    const QString active = m_workspace->activePresetName();
    if (active.isEmpty())
        return;

    if (!AppMessageDialog::confirm(this, tr("Delete Workspace"),
            tr("Delete the workspace \"%1\"? The panels on screen stay as they are.")
                .arg(active)))
        return;

    m_workspace->deletePreset(active);
}

void MainWindow::onResetWorkspace()
{
    if (m_workspace)
        m_workspace->resetToDefault();
    else
        resetDockLayout();
}

// ── WorkspaceHost ────────────────────────────────────────────────────

QByteArray MainWindow::captureWorkspaceState() const
{
    // On the Home tab the editing docks are all hidden on purpose, so the
    // live state describes an empty window. The snapshot taken when Home
    // opened is the arrangement that actually belongs to the workspace.
    if (m_homeDockMode)
        return m_documentDockState;
    return saveState(WorkspaceManager::kStateVersion);
}

bool MainWindow::applyWorkspaceState(const QByteArray& state)
{
    if (state.isEmpty())
        return false;

    // Same trick as resetDockLayout(): leaving Home mode for the duration of
    // the restore is what lets Qt validate and apply the blob, and re-entering
    // it parks the result in the Home snapshot instead of painting panels over
    // the Home page.
    const bool homeActive = m_homeDockMode;
    if (homeActive)
        setHomeDockMode(false);

    const bool ok = restoreState(state, WorkspaceManager::kStateVersion);
    if (ok)
        scheduleRestoredDockChromeRefresh();

    if (homeActive)
        setHomeDockMode(true);

    return ok;
}

void MainWindow::applyDefaultWorkspaceState()
{
    resetDockLayout();
}

void MainWindow::showWithRestoredWorkspace()
{
    // The geometry itself was already applied during construction, before the
    // dock layout, because dock sizes are stored in pixels and only land back
    // where the user left them against the right window size. It carries the
    // maximized/full-screen flag with it, so a session that ended maximized
    // comes back maximized; only a first run (or a geometry Qt rejected)
    // falls back to the historical default.
    if (m_workspace && m_workspace->windowGeometryRestored())
        show();
    else
        showMaximized();

    // Autosave starts only now: everything before this point is construction,
    // and the geometry of a window still being built is not a workspace.
    if (m_workspace)
        m_workspace->beginTracking();
}
