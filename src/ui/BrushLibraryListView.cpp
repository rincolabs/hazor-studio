#include "BrushLibraryListView.hpp"
#include "BrushLibraryTreeModel.hpp"
#include "BrushPresetItemDelegate.hpp"
#include "brush/BrushPreviewCache.hpp"
#include "brush/BrushPreviewRenderer.hpp"
#include "brush/BrushPresetManager.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/ScrollBarStyle.hpp"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTimer>
#include <QDateTime>
#include <vector>
#include <utility>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTreeView>
#include <QLineEdit>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QItemSelectionModel>
#include <QShowEvent>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QAbstractItemView>

static QString libraryListStyle(const Theme* t)
{
    ScrollBarQssOptions sb;
    sb.minLength = 30;
    sb.borderRadius = 4;
    sb.handleMargin = 2;
    sb.horizontal = false;
    return QStringLiteral(
        "QTreeView { background: %1; border: none; outline: none; }"
        "QTreeView::branch { background: transparent; }")
        .arg(t->colorSurface.name())
        + scrollBarQss(t->colorBackgroundTertiary, t->colorSurfaceHover, sb);
}

BrushLibraryListView::BrushLibraryListView(BrushPresetListMode mode, QWidget* parent)
    : QWidget(parent), m_mode(mode)
{
    const Theme* t = ThemeManager::instance()->current();
    const bool managing = (m_mode == BrushPresetListMode::SettingsManager);

    m_cache = new BrushPreviewCache();
    m_treeModel = new BrushLibraryTreeModel(this);
    m_treeModel->setEditingEnabled(managing);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(t->spaceSM);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search brushes…"));
    m_search->setClearButtonEnabled(true);
    root->addWidget(m_search);

    // Throttle + debounce so live typing refilters at most every kThrottleMs and
    // flushes once typing pauses (same feel as the Brushes panel).
    constexpr int kThrottleMs = 200;
    constexpr int kDebounceMs = 160;
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kDebounceMs);
    auto applyFilter = [this]() {
        m_searchDebounce->stop();
        m_lastFilterMs = QDateTime::currentMSecsSinceEpoch();
        if (m_treeModel) {
            m_treeModel->setFilter(m_search->text());
            applyTreeExpansion();
        }
    };
    connect(m_searchDebounce, &QTimer::timeout, this, applyFilter);
    connect(m_search, &QLineEdit::textChanged, this, [this, applyFilter]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastFilterMs >= kThrottleMs)
            applyFilter();
        else
            m_searchDebounce->start();
    });

    m_listDelegate = new BrushPresetItemDelegate(m_cache, this);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_treeModel);
    m_tree->setItemDelegate(m_listDelegate);
    m_tree->setUniformRowHeights(false);
    m_tree->setMouseTracking(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(5);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(libraryListStyle(t));
    if (managing) {
        // Organisational drag&drop. DragDrop (not InternalMove): the model owns the
        // move end-to-end via the preset manager and rebuilds, so the view must not
        // remove source rows itself.
        m_tree->setDragEnabled(true);
        m_tree->setAcceptDrops(true);
        m_tree->setDropIndicatorShown(true);
        m_tree->setDragDropMode(QAbstractItemView::DragDrop);
        m_tree->setDefaultDropAction(Qt::MoveAction);
    }
    root->addWidget(m_tree, 1);

    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& cur, const QModelIndex&) {
                onCurrentRowChanged(cur);
                // Fire for every row change (preset/folder/cleared), even while
                // m_syncing — the top bar's enabled states must track silent
                // selections too. presetSelected (canvas apply) stays gated above.
                emit selectionChanged();
            });
    connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex& idx) {
        if (m_treeModel && !m_treeModel->hasFilter())
            m_treeModel->setFolderExpanded(idx, true);
    });
    connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex& idx) {
        if (m_treeModel && !m_treeModel->hasFilter())
            m_treeModel->setFolderExpanded(idx, false);
    });
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, &BrushLibraryListView::showTreeContextMenu);
    connect(m_treeModel, &QAbstractItemModel::modelReset,
            this, &BrushLibraryListView::applyTreeExpansion);

    if (managing) {
        // Keep the relocated item selected after a drag&drop move (the model has
        // already rebuilt by the time these fire).
        connect(m_treeModel, &BrushLibraryTreeModel::presetDropped, this,
                [this](const QString& name) { revealPreset(name); });
        connect(m_treeModel, &BrushLibraryTreeModel::folderDropped, this,
                [this](const QStringList& path) {
                    const QModelIndex idx = m_treeModel->indexForFolderPath(path);
                    if (idx.isValid())
                        selectTreeIndex(idx);
                });
    }

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &BrushLibraryListView::applyTheme);
}

BrushLibraryListView::~BrushLibraryListView()
{
    // The delegate (a QObject child) only touches the cache while painting, which
    // has stopped by the time the cache is freed here.
    delete m_cache;
}

void BrushLibraryListView::setPresetManager(BrushPresetManager* manager)
{
    m_manager = manager;
    m_treeModel->setPresetManager(manager);
    QTimer::singleShot(0, this, [this]() { warmCacheAsync(); });
}

void BrushLibraryListView::applyTheme()
{
    const Theme* t = ThemeManager::instance()->current();
    if (m_tree)
        m_tree->setStyleSheet(libraryListStyle(t));
}

namespace {
struct WarmEntry { QString key; QImage image; };
} // namespace

void BrushLibraryListView::warmCacheAsync()
{
    if (m_warming || !m_manager || !m_cache)
        return;
    std::vector<BrushPreset> presets = m_manager->presets();
    if (presets.empty())
        return;

    const qreal dpr = devicePixelRatioF();
    const QColor ink = ThemeManager::instance()->current()->colorTextPrimary;
    const QSize tipSize = m_listDelegate->tipThumbSize();
    const QSize strokeSize = m_listDelegate->strokeRefSize();

    m_warming = true;

    auto* watcher = new QFutureWatcher<QVector<WarmEntry>>(this);
    connect(watcher, &QFutureWatcher<QVector<WarmEntry>>::finished, this, [this, watcher]() {
        const QVector<WarmEntry> results = watcher->result();
        for (const WarmEntry& e : results)
            m_cache->put(e.key, QPixmap::fromImage(e.image));
        if (m_tree) m_tree->viewport()->update();
        m_warming = false;
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(
        [presets, dpr, ink, tipSize, strokeSize]() {
            QVector<WarmEntry> out;
            out.reserve(static_cast<int>(presets.size()) * 2);
            for (const BrushPreset& p : presets) {
                out.push_back({ BrushPreviewCache::keyFor(p, 't', tipSize, dpr, ink),
                                BrushPreviewCache::tipThumbnail(p, tipSize, dpr, ink) });
                if (!strokeSize.isEmpty())
                    out.push_back({ BrushPreviewCache::keyFor(p, 's', strokeSize, dpr, ink),
                                    BrushPreviewRenderer::renderStroke(p.settings, strokeSize, dpr, ink) });
            }
            return out;
        }));
}

void BrushLibraryListView::onCurrentRowChanged(const QModelIndex& current)
{
    if (m_syncing || !current.isValid())
        return;
    const BrushPreset* p = m_treeModel->presetForIndex(current);
    if (!p)
        return;
    emit presetSelected(*p);
}

void BrushLibraryListView::selectTreeIndex(const QModelIndex& index)
{
    if (!index.isValid() || !m_tree)
        return;
    QModelIndex parent = index.parent();
    while (parent.isValid()) {
        m_tree->expand(parent);
        parent = parent.parent();
    }
    m_syncing = true;
    m_tree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    m_tree->scrollTo(index, QAbstractItemView::EnsureVisible);
    m_syncing = false;
}

void BrushLibraryListView::setCurrentPreset(const QString& name)
{
    revealPreset(name);
}

void BrushLibraryListView::revealPreset(const QString& name)
{
    // Remember it so showEvent / a model rebuild can re-assert the highlight if this
    // ran while the view was hidden (rows aren't laid out, so nothing paints).
    m_pendingPresetName = name;
    const QModelIndex idx = m_treeModel ? m_treeModel->indexForPresetName(name) : QModelIndex();
    selectTreeIndex(idx);
}

void BrushLibraryListView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Re-apply the pending selection now the tree has real geometry, so the visual
    // highlight appears (selecting while hidden sets currentIndex but doesn't paint).
    if (!m_pendingPresetName.isEmpty() && m_tree && m_treeModel) {
        const QModelIndex idx = m_treeModel->indexForPresetName(m_pendingPresetName);
        if (idx.isValid())
            selectTreeIndex(idx);
    }
}

void BrushLibraryListView::applyTreeExpansion()
{
    if (!m_tree || !m_treeModel)
        return;
    const QVector<QModelIndex> folders = m_treeModel->folderIndexes();
    for (const QModelIndex& idx : folders) {
        if (m_treeModel->hasFilter())
            m_tree->expand(idx);
        else
            m_tree->setExpanded(idx, m_treeModel->folderExpanded(idx));
    }
}

void BrushLibraryListView::showTreeContextMenu(const QPoint& pos)
{
    if (!m_tree || !m_treeModel)
        return;
    const QModelIndex idx = m_tree->indexAt(pos);

    // ── Brush Settings: full Preset Manager context menu ──
    if (m_mode == BrushPresetListMode::SettingsManager) {
        // Make the clicked row current so the actions target it; selecting is silent
        // (no re-apply to the canvas) — only the explicit actions mutate anything.
        if (idx.isValid())
            selectTreeIndex(idx);

        QMenu menu(this);
        if (idx.isValid() && m_treeModel->isFolder(idx)) {
            const bool managed = m_treeModel->isManagedFolder(idx);
            QAction* newP = menu.addAction(tr("New Preset"));
            QAction* newF = menu.addAction(tr("New Folder..."));
            QAction* rename = managed ? menu.addAction(tr("Rename Folder...")) : nullptr;
            QAction* del = managed ? menu.addAction(tr("Delete Folder")) : nullptr;
            menu.addSeparator();
            QAction* expandAll = menu.addAction(tr("Expand All"));
            QAction* collapseAll = menu.addAction(tr("Collapse All"));
            menu.addSeparator();
            QAction* importAct = menu.addAction(tr("Import Brushes..."));
            QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
            if (chosen == newP)              createNewPreset();
            else if (chosen == newF)         createFolderUnder(idx);
            else if (rename && chosen == rename) renameFolder(idx);
            else if (del && chosen == del)   deleteFolder(idx);
            else if (chosen == expandAll)    m_tree->expandAll();
            else if (chosen == collapseAll)  m_tree->collapseAll();
            else if (chosen == importAct)    emit importBrushesRequested();
            return;
        }
        if (idx.isValid()) {                 // preset
            QAction* save = menu.addAction(tr("Save Current Preset"));
            QAction* saveAs = menu.addAction(tr("Save Current Preset As..."));
            QAction* dup = menu.addAction(tr("Duplicate Preset"));
            QAction* rename = menu.addAction(tr("Rename Preset..."));
            QAction* del = menu.addAction(tr("Delete Preset"));
            QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
            if (chosen == save)        saveCurrentPreset();
            else if (chosen == saveAs) saveCurrentPresetAs();
            else if (chosen == dup)    duplicateSelectedPreset();
            else if (chosen == rename) renamePreset(selectedPresetName());
            else if (chosen == del)    deleteSelected();
            return;
        }
        // empty area
        QAction* newP = menu.addAction(tr("New Preset"));
        QAction* newF = menu.addAction(tr("New Folder..."));
        QAction* saveAs = menu.addAction(tr("Save Current Preset As..."));
        menu.addSeparator();
        QAction* importAct = menu.addAction(tr("Import Brushes..."));
        QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == newP)           createNewPreset();
        else if (chosen == newF)      createFolderUnder(QModelIndex());
        else if (chosen == saveAs)    saveCurrentPresetAs();
        else if (chosen == importAct) emit importBrushesRequested();
        return;
    }

    // ── Brush Panel: select-only menu (no administrative actions) ──
    QMenu menu(this);
    if (idx.isValid() && m_treeModel->isFolder(idx)) {
        QAction* expand = menu.addAction(tr("Expand"));
        QAction* collapse = menu.addAction(tr("Collapse"));
        menu.addSeparator();
        QAction* expandAll = menu.addAction(tr("Expand All"));
        QAction* collapseAll = menu.addAction(tr("Collapse All"));
        menu.addSeparator();
        QAction* importAct = menu.addAction(tr("Import Brushes..."));

        QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == expand)
            m_tree->expand(idx);
        else if (chosen == collapse)
            m_tree->collapse(idx);
        else if (chosen == expandAll)
            m_tree->expandAll();
        else if (chosen == collapseAll)
            m_tree->collapseAll();
        else if (chosen == importAct)
            emit importBrushesRequested();
        return;
    }

    if (idx.isValid()) {
        QAction* openInSettings = nullptr;
        if (m_mode == BrushPresetListMode::BrushPanelSelector)
            openInSettings = menu.addAction(tr("Open in Settings"));
        QAction* source = nullptr;
        if (!idx.data(BrushLibraryTreeModel::SourceSubtitleRole).toString().isEmpty())
            source = menu.addAction(tr("Reveal Source Info"));
        QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (openInSettings && chosen == openInSettings) {
            selectTreeIndex(idx);
            onCurrentRowChanged(idx);
            emit openInSettingsRequested();
        } else if (source && chosen == source) {
            showSourceInfo(idx);
        }
        return;
    }

    QAction* importAct = menu.addAction(tr("Import Brushes..."));
    if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == importAct)
        emit importBrushesRequested();
}

void BrushLibraryListView::showSourceInfo(const QModelIndex& index)
{
    const QString text = index.data(Qt::ToolTipRole).toString();
    if (!text.isEmpty())
        QMessageBox::information(this, tr("Brush Source Info"), text);
}

// ── Preset Manager (SettingsManager mode) ─────────────────────

void BrushLibraryListView::setCurrentSettingsProvider(std::function<BrushSettings()> provider)
{
    m_settingsProvider = std::move(provider);
}

void BrushLibraryListView::populateManagerMenu(QMenu* menu)
{
    if (!menu || m_mode != BrushPresetListMode::SettingsManager)
        return;
    connect(menu->addAction(tr("Save Current Preset")), &QAction::triggered,
            this, &BrushLibraryListView::saveCurrentPreset);
    connect(menu->addAction(tr("Save Current Preset As...")), &QAction::triggered,
            this, &BrushLibraryListView::saveCurrentPresetAs);
    connect(menu->addAction(tr("Duplicate Preset")), &QAction::triggered,
            this, &BrushLibraryListView::duplicateSelectedPreset);
    connect(menu->addAction(tr("New Preset")), &QAction::triggered,
            this, &BrushLibraryListView::createNewPreset);
    connect(menu->addAction(tr("New Folder...")), &QAction::triggered,
            this, &BrushLibraryListView::createNewFolder);
    menu->addSeparator();
    connect(menu->addAction(tr("Delete Preset")), &QAction::triggered,
            this, &BrushLibraryListView::deleteSelected);
    menu->addSeparator();
    connect(menu->addAction(tr("Import Brushes...")), &QAction::triggered,
            this, &BrushLibraryListView::importBrushesRequested);
    connect(menu->addAction(tr("Reset Default Brushes")), &QAction::triggered,
            this, &BrushLibraryListView::resetDefaultPresets);
}

QModelIndex BrushLibraryListView::currentIndex() const
{
    return (m_tree && m_tree->selectionModel())
        ? m_tree->selectionModel()->currentIndex()
        : QModelIndex();
}

QString BrushLibraryListView::selectedPresetName() const
{
    if (!m_treeModel)
        return {};
    const BrushPreset* p = m_treeModel->presetForIndex(currentIndex());
    return p ? p->name : QString();
}

QString BrushLibraryListView::groupForSelection() const
{
    QString group;
    if (m_treeModel && m_treeModel->saveGroupForIndex(currentIndex(), &group))
        return group;
    return {};   // protected/imported selection → save into the Default Brushes root
}

BrushLibraryListView::Selection BrushLibraryListView::currentSelection() const
{
    Selection sel;
    if (m_mode != BrushPresetListMode::SettingsManager || !m_treeModel)
        return sel;
    const QModelIndex idx = currentIndex();
    if (!idx.isValid())
        return sel;
    if (m_treeModel->isFolder(idx)) {
        sel.kind = SelectionKind::Folder;
        sel.protectedItem = !m_treeModel->isManagedFolder(idx);
    } else if (m_treeModel->presetForIndex(idx)) {
        sel.kind = SelectionKind::Preset;
        sel.protectedItem = !m_treeModel->isManagedPreset(idx);
    }
    return sel;
}

void BrushLibraryListView::reloadPresets()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager)
        return;
    // Re-read from disk; the manager emits presetsChanged → the model rebuilds.
    m_manager->reloadFromDisk();
    warmCacheAsync();
}

void BrushLibraryListView::resetDefaultPresets()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager)
        return;
    if (QMessageBox::warning(
            this, tr("Reset Default Brushes"),
            tr("Reset the brush library to the default presets?\n"
               "Every custom preset and folder will be deleted.\n"
               "This action cannot be undone."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    // Wipe the library and rebuild the built-in kit; the manager emits
    // presetsChanged → the model rebuilds.
    m_manager->resetToDefaults();
    warmCacheAsync();
}

void BrushLibraryListView::selectPresetByName(const QString& name)
{
    if (name.isEmpty())
        return;
    revealPreset(name);   // selects without echoing a signal
    if (!m_treeModel)
        return;
    const QModelIndex idx = m_treeModel->indexForPresetName(name);
    if (const BrushPreset* p = m_treeModel->presetForIndex(idx))
        emit presetSelected(*p);
}

void BrushLibraryListView::saveCurrentPreset()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager)
        return;
    const QString name = selectedPresetName();
    if (name.isEmpty()) {            // nothing saved yet → Save As
        saveCurrentPresetAs();
        return;
    }
    if (!m_settingsProvider)
        return;
    m_manager->overwritePreset(name, m_settingsProvider());
    revealPreset(name);             // keep it selected (settings are already live)
}

void BrushLibraryListView::saveCurrentPresetAs()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager || !m_settingsProvider)
        return;
    QString name, group;
    if (!runSaveAsDialog(&name, &group))
        return;
    if (m_manager->saveAsNewPreset(name, group, m_settingsProvider()))
        selectPresetByName(name);
}

void BrushLibraryListView::duplicateSelectedPreset()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager)
        return;
    const QString name = selectedPresetName();
    if (name.isEmpty())
        return;
    const QString created = m_manager->duplicatePreset(name);
    if (!created.isEmpty())
        selectPresetByName(created);
}

void BrushLibraryListView::createNewPreset()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager)
        return;
    const QString created = m_manager->createDefaultPreset(groupForSelection());
    if (!created.isEmpty())
        selectPresetByName(created);
}

void BrushLibraryListView::createNewFolder()
{
    createFolderUnder(currentIndex());
}

void BrushLibraryListView::createFolderUnder(const QModelIndex& parentFolderIndex)
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_manager || !m_treeModel)
        return;

    // Parent: the folder itself, a preset's folder, or the default root for empty
    // space / non-folder selections.
    QStringList base;
    if (parentFolderIndex.isValid() && m_treeModel->isFolder(parentFolderIndex))
        base = m_treeModel->folderDisplayPath(parentFolderIndex);
    else if (parentFolderIndex.isValid())
        base = m_treeModel->targetFolderForDrop(parentFolderIndex);  // preset → its folder
    if (base.isEmpty())
        base = BrushLibraryTreeModel::defaultRootPath();

    bool ok = false;
    const QString typed = QInputDialog::getText(
        this, tr("New Folder"),
        tr("Folder name (use '/' for nested folders):"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || typed.isEmpty())
        return;

    QStringList parts = typed.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (QString& p : parts)
        p = p.trimmed();
    parts.removeAll(QString());
    if (parts.isEmpty())
        return;

    QStringList full = base;
    full += parts;
    if (m_treeModel->indexForFolderPath(full).isValid()) {
        QMessageBox::warning(this, tr("New Folder"),
            tr("A folder named \"%1\" already exists here.").arg(parts.last()));
        return;
    }

    m_manager->addFolder(full.join(QLatin1Char('/')));
    const QModelIndex created = m_treeModel->indexForFolderPath(full);
    if (created.isValid())
        selectTreeIndex(created);
}

void BrushLibraryListView::deleteSelected()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_treeModel)
        return;
    const QModelIndex idx = currentIndex();
    if (m_treeModel->isFolder(idx)) {
        deleteFolder(idx);
    } else {
        const QString name = selectedPresetName();
        if (!name.isEmpty())
            deletePresetByName(name);
    }
}

void BrushLibraryListView::renameSelected()
{
    if (m_mode != BrushPresetListMode::SettingsManager || !m_treeModel)
        return;
    const QModelIndex idx = currentIndex();
    if (m_treeModel->isFolder(idx))
        renameFolder(idx);
    else
        renamePreset(selectedPresetName());
}

void BrushLibraryListView::deletePresetByName(const QString& name)
{
    if (!m_manager || name.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Delete Preset"),
            tr("Do you really want to delete the preset \"%1\"?\n"
               "This action cannot be undone.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    // Pick a neighbour (in storage order) to land on after removal.
    const QStringList names = m_manager->presetNames();
    QString nextName;
    const int at = names.indexOf(name);
    if (at >= 0) {
        if (at + 1 < names.size())
            nextName = names.at(at + 1);
        else if (at - 1 >= 0)
            nextName = names.at(at - 1);
    }

    m_manager->removePreset(name);
    if (!nextName.isEmpty())
        selectPresetByName(nextName);
}

void BrushLibraryListView::deleteFolder(const QModelIndex& folderIndex)
{
    if (!m_manager || !m_treeModel)
        return;
    if (!m_treeModel->isManagedFolder(folderIndex)) {
        QMessageBox::information(this, tr("Delete Folder"),
            tr("This folder is protected and cannot be deleted."));
        return;
    }
    const QStringList path = m_treeModel->folderDisplayPath(folderIndex);
    const QString folderName = path.isEmpty() ? QString() : path.last();
    const QStringList names = m_treeModel->presetNamesUnder(folderIndex);

    if (QMessageBox::question(
            this, tr("Delete Folder"),
            tr("Do you really want to delete the folder \"%1\" and all presets inside it?\n"
               "This action cannot be undone.").arg(folderName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    m_manager->removeFolderTree(path.join(QLatin1Char('/')), names);
    // Selection was inside the removed folder; land on any remaining preset.
    const QStringList remaining = m_manager->presetNames();
    if (!remaining.isEmpty())
        selectPresetByName(remaining.first());
}

void BrushLibraryListView::renamePreset(const QString& name)
{
    if (!m_manager || name.isEmpty())
        return;
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename Preset"), tr("Preset name:"),
        QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == name)
        return;
    if (m_manager->contains(newName)) {
        QMessageBox::warning(this, tr("Rename Preset"),
            tr("A preset named \"%1\" already exists.").arg(newName));
        return;
    }
    if (m_manager->renamePreset(name, newName))
        selectPresetByName(newName);
}

void BrushLibraryListView::renameFolder(const QModelIndex& folderIndex)
{
    if (!m_treeModel)
        return;
    if (!m_treeModel->isManagedFolder(folderIndex)) {
        QMessageBox::information(this, tr("Rename Folder"),
            tr("This folder is protected and cannot be renamed."));
        return;
    }
    const QStringList path = m_treeModel->folderDisplayPath(folderIndex);
    if (path.isEmpty())
        return;
    const QString oldName = path.last();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename Folder"), tr("Folder name:"),
        QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    if (newName.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("Rename Folder"),
            tr("A folder name cannot contain '/'."));
        return;
    }
    QStringList newPath = path;
    newPath.last() = newName;
    if (!m_treeModel->relocateFolder(folderIndex, newPath)) {
        QMessageBox::warning(this, tr("Rename Folder"),
            tr("Could not rename the folder — a folder with that name may already exist."));
        return;
    }
    const QModelIndex moved = m_treeModel->indexForFolderPath(newPath);
    if (moved.isValid())
        selectTreeIndex(moved);
}

bool BrushLibraryListView::runSaveAsDialog(QString* outName, QString* outGroup)
{
    if (!m_manager || !m_treeModel || !outName || !outGroup)
        return false;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save Preset As"));
    auto* form = new QFormLayout(&dlg);

    auto* nameEdit = new QLineEdit(&dlg);
    const QString seed = selectedPresetName();
    nameEdit->setText(seed.isEmpty() ? tr("New Brush Preset") : seed);
    nameEdit->selectAll();
    form->addRow(tr("Name:"), nameEdit);

    auto* folderCombo = new QComboBox(&dlg);
    folderCombo->setEditable(true);
    const QVector<QStringList> folders = m_treeModel->managedFolderDisplayPaths();
    const QString selGroup = groupForSelection();
    int preselect = 0;
    for (const QStringList& f : folders) {
        const QString display = f.join(QLatin1Char('/'));
        folderCombo->addItem(display);
        if (BrushLibraryTreeModel::groupFromDisplayPath(f) == selGroup)
            preselect = folderCombo->count() - 1;
    }
    if (folderCombo->count() == 0)
        folderCombo->addItem(tr("Default Brushes"));
    folderCombo->setCurrentIndex(preselect);
    form->addRow(tr("Folder:"), folderCombo);

    auto* hint = new QLabel(
        tr("Type a path like \"My Brushes/Sketch\" to create nested folders."), &dlg);
    hint->setWordWrap(true);
    form->addRow(hint);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    while (true) {
        if (dlg.exec() != QDialog::Accepted)
            return false;
        const QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dlg, tr("Save Preset As"), tr("Please enter a name."));
            continue;
        }
        const QString group =
            BrushLibraryTreeModel::groupFromUserPath(folderCombo->currentText());
        if (m_manager->contains(name)) {
            const auto res = QMessageBox::question(
                &dlg, tr("Save Preset As"),
                tr("A preset named \"%1\" already exists. Replace it?").arg(name),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
            if (res == QMessageBox::Cancel)
                return false;
            if (res == QMessageBox::No)
                continue;            // let the user pick a different name
        }
        *outName = name;
        *outGroup = group;
        return true;
    }
}
