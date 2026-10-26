#include "WorkspaceManager.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

namespace {

// All workspace keys live under one group so a corrupt or outdated schema can
// be dropped wholesale without touching the rest of the preferences.
constexpr char kGroup[]            = "workspace";
constexpr char kVersionKey[]       = "workspace/settingsVersion";
constexpr char kWorkingStateKey[]  = "workspace/workingState";
constexpr char kWorkingGeomKey[]   = "workspace/workingGeometry";
constexpr char kActivePresetKey[]  = "workspace/activePreset";
constexpr char kPresetsArrayKey[]  = "workspace/presets";
constexpr char kPresetNameKey[]    = "name";
constexpr char kPresetStateKey[]   = "state";

// Long enough that a drag or a window resize collapses into a single write,
// short enough that a crash right after a rearrangement still loses nothing
// the user would notice.
constexpr int kSaveDebounceMs = 750;

} // namespace

WorkspaceManager::WorkspaceManager(QMainWindow* window, WorkspaceHost* host, QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_host(host)
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, &WorkspaceManager::saveNow);

    load();

    // Move, resize and maximize/restore are part of the workspace too, and
    // none of them is reported by a dock signal.
    if (m_window)
        m_window->installEventFilter(this);
}

// ── Loading / storing ────────────────────────────────────────────────

void WorkspaceManager::load()
{
    QSettings settings;

    const int version = settings.value(QLatin1String(kVersionKey), 0).toInt();
    if (version != kSettingsVersion) {
        // Either a first run (0) or a schema this build cannot read. Dropping
        // the group is what keeps an unreadable workspace from being carried
        // around forever; startup then falls back to the default layout.
        if (version != 0)
            settings.remove(QLatin1String(kGroup));
        return;
    }

    m_workingState = settings.value(QLatin1String(kWorkingStateKey)).toByteArray();
    m_workingGeometry = settings.value(QLatin1String(kWorkingGeomKey)).toByteArray();
    m_activePresetName = settings.value(QLatin1String(kActivePresetKey)).toString();

    const int count = settings.beginReadArray(QLatin1String(kPresetsArrayKey));
    m_presets.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        WorkspacePreset preset;
        preset.name = settings.value(QLatin1String(kPresetNameKey)).toString();
        preset.state = settings.value(QLatin1String(kPresetStateKey)).toByteArray();
        // A nameless or empty entry cannot be loaded or shown; skipping it
        // beats letting it break the menu.
        if (preset.name.isEmpty() || preset.state.isEmpty())
            continue;
        if (indexOfPreset(preset.name) < 0)
            m_presets.append(preset);
    }
    settings.endArray();

    if (!m_activePresetName.isEmpty() && indexOfPreset(m_activePresetName) < 0)
        m_activePresetName.clear();
}

void WorkspaceManager::writeWorkingLayout() const
{
    QSettings settings;
    settings.setValue(QLatin1String(kVersionKey), kSettingsVersion);
    settings.setValue(QLatin1String(kWorkingStateKey), m_workingState);
    settings.setValue(QLatin1String(kWorkingGeomKey), m_workingGeometry);
    settings.setValue(QLatin1String(kActivePresetKey), m_activePresetName);
}

void WorkspaceManager::writePresets() const
{
    QSettings settings;
    settings.setValue(QLatin1String(kVersionKey), kSettingsVersion);
    settings.setValue(QLatin1String(kActivePresetKey), m_activePresetName);

    // beginWriteArray() only rewrites the indices it is given, so a shrinking
    // list would leave the deleted preset's keys behind in the file. Clearing
    // the array first is what keeps the stored set honest.
    settings.remove(QLatin1String(kPresetsArrayKey));
    settings.beginWriteArray(QLatin1String(kPresetsArrayKey), m_presets.size());
    for (int i = 0; i < m_presets.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QLatin1String(kPresetNameKey), m_presets[i].name);
        settings.setValue(QLatin1String(kPresetStateKey), m_presets[i].state);
    }
    settings.endArray();
}

// ── Working layout ───────────────────────────────────────────────────

bool WorkspaceManager::restoreWorkingLayout()
{
    if (!m_host || !m_window)
        return false;

    if (!m_workingGeometry.isEmpty())
        m_geometryRestored = m_window->restoreGeometry(m_workingGeometry);

    if (m_workingState.isEmpty())
        return false;

    m_applying = true;
    const bool ok = m_host->applyWorkspaceState(m_workingState);
    m_applying = false;

    if (!ok) {
        // A layout this build can no longer make sense of must not stop the
        // app from starting: drop it and keep the default already in place.
        m_workingState.clear();
        m_activePresetName.clear();
    }
    return ok;
}

void WorkspaceManager::beginTracking()
{
    m_tracking = true;
}

void WorkspaceManager::scheduleSave()
{
    if (!m_tracking || m_applying)
        return;
    m_saveTimer->start();
}

void WorkspaceManager::saveNow()
{
    if (!m_tracking || m_applying || !m_window || !m_host)
        return;

    // Qt's dock layout is still in flux while a button is held (the user is
    // mid-drag); capturing now would store an intermediate arrangement.
    if (QApplication::mouseButtons() != Qt::NoButton) {
        m_saveTimer->start();
        return;
    }
    m_saveTimer->stop();

    const QByteArray state = m_host->captureWorkspaceState();
    const QByteArray geometry = m_window->saveGeometry();
    if (state == m_workingState && geometry == m_workingGeometry)
        return; // nothing moved since the last write

    if (!state.isEmpty())
        m_workingState = state;
    if (!geometry.isEmpty())
        m_workingGeometry = geometry;
    writeWorkingLayout();
}

// ── Change tracking ──────────────────────────────────────────────────

bool WorkspaceManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::WindowStateChange:
            scheduleSave();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void WorkspaceManager::watchDocks(const QList<QDockWidget*>& docks)
{
    for (QDockWidget* dock : docks) {
        if (!dock)
            continue;
        connect(dock, &QDockWidget::dockLocationChanged, this,
                [this](Qt::DockWidgetArea) { scheduleSave(); });
        connect(dock, &QDockWidget::topLevelChanged, this,
                [this](bool) { scheduleSave(); });
        connect(dock, &QDockWidget::visibilityChanged, this,
                [this](bool) { scheduleSave(); });
    }
}

void WorkspaceManager::watchDockTabBars()
{
    if (!m_window)
        return;

    for (QTabBar* bar : m_window->findChildren<QTabBar*>()) {
        // The document tab bar belongs to a QTabWidget; the tab bar of a
        // tabified dock group is parented straight onto the main window or
        // onto a floating group window.
        if (qobject_cast<QTabWidget*>(bar->parentWidget()))
            continue;
        connect(bar, &QTabBar::tabMoved, this, &WorkspaceManager::scheduleSave,
                Qt::UniqueConnection);
        connect(bar, &QTabBar::currentChanged, this, &WorkspaceManager::scheduleSave,
                Qt::UniqueConnection);
    }
}

// ── Presets ──────────────────────────────────────────────────────────

int WorkspaceManager::indexOfPreset(const QString& name) const
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

QStringList WorkspaceManager::presetNames() const
{
    QStringList names;
    names.reserve(m_presets.size());
    for (const WorkspacePreset& preset : m_presets)
        names << preset.name;
    return names;
}

bool WorkspaceManager::hasPreset(const QString& name) const
{
    return indexOfPreset(name) >= 0;
}

bool WorkspaceManager::loadPreset(const QString& name)
{
    const int index = indexOfPreset(name);
    if (index < 0 || !m_host)
        return false;

    m_applying = true;
    const bool ok = m_host->applyWorkspaceState(m_presets[index].state);
    m_applying = false;
    if (!ok)
        return false;

    // The preset is now the working layout, but it stays a *copy*: later
    // rearrangements are autosaved into the working state and never touch
    // m_presets. Only savePreset() writes a preset.
    m_activePresetName = m_presets[index].name;
    m_workingState = m_presets[index].state;
    writeWorkingLayout();
    return true;
}

void WorkspaceManager::savePreset(const QString& name)
{
    if (!m_host || name.isEmpty())
        return;

    const QByteArray state = m_host->captureWorkspaceState();
    if (state.isEmpty())
        return;

    const int index = indexOfPreset(name);
    if (index >= 0) {
        m_presets[index].name = name; // adopt the casing the user just typed
        m_presets[index].state = state;
    } else {
        m_presets.append({name, state});
    }

    m_activePresetName = name;
    m_workingState = state;
    writePresets();
    writeWorkingLayout();
}

bool WorkspaceManager::renamePreset(const QString& from, const QString& to)
{
    const int index = indexOfPreset(from);
    if (index < 0 || to.isEmpty())
        return false;
    if (from == to)
        return true;

    const int clash = indexOfPreset(to);
    if (clash >= 0 && clash != index) {
        m_presets[index].name = to;
        m_presets.removeAt(clash);
    } else {
        m_presets[index].name = to;
    }

    if (m_activePresetName.compare(from, Qt::CaseInsensitive) == 0)
        m_activePresetName = to;
    writePresets();
    return true;
}

bool WorkspaceManager::deletePreset(const QString& name)
{
    const int index = indexOfPreset(name);
    if (index < 0)
        return false;

    m_presets.removeAt(index);
    // Deleting a preset drops the name, not the arrangement on screen: the
    // user keeps working in exactly the layout they had.
    if (m_activePresetName.compare(name, Qt::CaseInsensitive) == 0)
        m_activePresetName.clear();
    writePresets();
    return true;
}

void WorkspaceManager::resetToDefault()
{
    if (!m_host)
        return;

    m_applying = true;
    m_host->applyDefaultWorkspaceState();
    m_applying = false;

    m_activePresetName.clear();
    m_workingState = m_host->captureWorkspaceState();
    writeWorkingLayout();
}
