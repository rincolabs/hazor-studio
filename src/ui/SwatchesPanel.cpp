#include "SwatchesPanel.hpp"

#include "core/ColorEngine.hpp"
#include "core/SwatchManager.hpp"
#include "ui/IconUtils.hpp"
#include "ui/colorpicker/ColorPickerDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/ScrollBarStyle.hpp"

#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QTransform>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kTypeRole = Qt::UserRole + 1;   // 0 = folder, 1 = collection
constexpr int kSwatchSize = 24;

QPixmap makeSwatchPixmap(const QColor& color, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (color.alpha() < 255) {
        // Checkerboard behind translucent swatches.
        const int c = size / 4;
        for (int y = 0; y < size; y += c)
            for (int x = 0; x < size; x += c)
                p.fillRect(x, y, c, c, ((x / c + y / c) % 2) ? QColor(200, 200, 200)
                                                             : QColor(150, 150, 150));
    }
    p.fillRect(0, 0, size, size, color);
    p.setPen(QColor(0, 0, 0, 60));
    p.drawRect(0, 0, size - 1, size - 1);
    p.end();
    return pix;
}

QIcon makeCollectionIcon(const SwatchNode* col)
{
    const int s = 16;
    QPixmap pix(s, s);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    const int n = col ? col->colors.size() : 0;
    if (n == 0) {
        p.fillRect(0, 0, s, s, QColor(120, 120, 120));
    } else {
        const int h = s / 2;
        const QRect cells[4] = { {0, 0, h, h}, {h, 0, s - h, h},
                                 {0, h, h, s - h}, {h, h, s - h, s - h} };
        for (int i = 0; i < 4; ++i)
            p.fillRect(cells[i], col->colors[i % n].color);
    }
    p.setPen(QColor(0, 0, 0, 70));
    p.drawRect(0, 0, s - 1, s - 1);
    p.end();
    return QIcon(pix);
}

QString tooltipForColor(const SwatchColor& sc)
{
    const QColor c = sc.color;
    return QStringLiteral("%1\nRGB(%2, %3, %4)\n%5")
        .arg(SwatchManager::displayName(sc))
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(c.name(QColor::HexRgb).toUpper());
}

}  // namespace

// --- SwatchGrid ------------------------------------------------------------

SwatchGrid::SwatchGrid(QWidget* parent)
    : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSpacing(0);
    setIconSize(QSize(kSwatchSize, kSwatchSize));
    setGridSize(QSize(kSwatchSize + 0, kSwatchSize + 0));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SwatchGrid::dropEvent(QDropEvent* event)
{
    QListWidget::dropEvent(event);
    emit reordered();
}

void SwatchGrid::resizeEvent(QResizeEvent* event)
{
    QListWidget::resizeEvent(event);
    // Width may have changed how many swatches fit per row → re-fit the height.
    refreshContentHeight();
}

void SwatchGrid::setMaxContentRows(int rows)
{
    rows = qMax(2, rows);
    if (m_maxContentRows == rows)
        return;
    m_maxContentRows = rows;
    refreshContentHeight();
}

void SwatchGrid::refreshContentHeight()
{
    const int pitch = qMax(1, gridSize().height());
    const int cellW = qMax(1, gridSize().width());
    const int perRow = qMax(1, viewport()->width() / cellW);
    int colorRows = (count() + perRow - 1) / perRow;   // ceil
    if (colorRows < 1)
        colorRows = 1;                                  // empty still shows a row
    // One spare row beyond the colors so there's always room to add/drop one,
    // capped so the panel keeps space for the preset tree.
    const int rows = qBound(2, colorRows + 1, qMax(2, m_maxContentRows));
    const int h = rows * pitch;
    if (maximumHeight() != h)
        setFixedHeight(h);
}

// --- SwatchesPanel ---------------------------------------------------------

SwatchesPanel::SwatchesPanel(ColorEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
{
    setAttribute(Qt::WA_StyledBackground, true);

    // Pre-render a downward arrow (the asset only ships the collapsed/right
    // chevron) so the tree's open branches read correctly.
    {
        QPixmap src(QStringLiteral(":/icons/ui-arrow.png"));
        if (!src.isNull()) {
            QTransform tr;
            tr.rotate(90);
            const QPixmap down = src.transformed(tr, Qt::SmoothTransformation);
            QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            if (dir.isEmpty())
                dir = QDir::tempPath();
            QDir().mkpath(dir);
            m_arrowDownPath = dir + QStringLiteral("/swatches-arrow-down.png");
            down.save(m_arrowDownPath, "PNG");
        }
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setMinimumHeight(90);

    // Colors on top (content-sized), preset tree fills the rest below them,
    // action bar at the very bottom.
    m_grid = new SwatchGrid(this);
    root->addWidget(m_grid, 0);

    m_emptyHint = new QLabel(tr("Select a palette to view its colors."), this);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->hide();
    root->addWidget(m_emptyHint, 0);

    root->addWidget(m_tree, 1);

    m_actionBar = new QWidget(this);
    m_actionBar->setObjectName(QStringLiteral("swatchesActionBar"));
    auto* bottomLayout = new QHBoxLayout(m_actionBar);
    bottomLayout->setContentsMargins(4, 3, 4, 3);
    bottomLayout->setSpacing(4);
    bottomLayout->addStretch();

    auto makeToolButton = [this](const QString& icon, const QString& tip) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(icon));
        btn->setIconSize(QSize(16, 16));
        btn->setFixedSize(24, 24);
        btn->setToolTip(tip);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFlat(true);
        return btn;
    };
    m_newCollectionBtn = makeToolButton(QStringLiteral(":/icons/ui-add.png"), tr("New Palette"));
    m_newFolderBtn = makeToolButton(QStringLiteral(":/icons/ui-folder-close.png"), tr("New Folder"));
    m_deleteBtn = makeToolButton(QStringLiteral(":/icons/ui-delete.png"), tr("Delete"));
    bottomLayout->addWidget(m_newCollectionBtn);
    bottomLayout->addWidget(m_newFolderBtn);
    bottomLayout->addWidget(m_deleteBtn);
    root->addWidget(m_actionBar, 0);

    // Tree interactions
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SwatchesPanel::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &SwatchesPanel::onTreeItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, &SwatchesPanel::onTreeItemCollapsed);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SwatchesPanel::onTreeContextMenu);

    // Grid interactions
    connect(m_grid, &QListWidget::itemClicked, this, &SwatchesPanel::onColorClicked);
    connect(m_grid, &QListWidget::itemDoubleClicked, this, &SwatchesPanel::onColorDoubleClicked);
    connect(m_grid, &SwatchGrid::reordered, this, &SwatchesPanel::onGridReordered);
    connect(m_grid, &QWidget::customContextMenuRequested, this, &SwatchesPanel::onGridContextMenu);

    // Bottom bar
    connect(m_newCollectionBtn, &QPushButton::clicked, this, &SwatchesPanel::onNewCollection);
    connect(m_newFolderBtn, &QPushButton::clicked, this, &SwatchesPanel::onNewFolder);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SwatchesPanel::onDeleteSelected);

    // Live sync with the shared store.
    auto* mgr = SwatchManager::instance();
    connect(mgr, &SwatchManager::structureChanged, this, &SwatchesPanel::rebuildTree);
    connect(mgr, &SwatchManager::collectionColorsChanged, this, &SwatchesPanel::onColorsChanged);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &SwatchesPanel::applyTheme);

    applyTheme();
    rebuildTree();
}

void SwatchesPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    const QString arrowRight = QStringLiteral(":/icons/ui-arrow.png");
    const QString arrowDown = m_arrowDownPath.isEmpty() ? arrowRight : m_arrowDownPath;

    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: %1; color: %3; border: none; outline: 0; }"
        "QTreeWidget::item { background: %8; height: 22px; border: 1px solid %4; margin: 1px; }"
        "QTreeWidget::item:selected { background: %4; color: %5; }"
        "QTreeWidget::item:hover:!selected { background: %2; }"
        "QTreeView::branch:has-children:closed { border-image: none; image: url(%6); }"
        "QTreeView::branch:has-children:open { border-image: none; image: url(%7); }")
        .arg(t->colorBackgroundTertiary.name(),
             t->colorSurfaceHover.name(),
             t->colorTextPrimary.name(),
             t->colorSurfacePressed.name(),
             t->colorTextPrimary.name(),
             arrowRight, arrowDown, t->colorSurface.name()));

    ScrollBarQssOptions sb;
    sb.minLength = 24;
    sb.borderRadius = 4;
    sb.handleMargin = 2;
    sb.horizontal = false;
    m_grid->setStyleSheet(QStringLiteral(
        "QListWidget { background: %1; border: none; outline: 0; }"
        "QListWidget::item { border: 1px solid transparent; border-radius: 2px; }"
        "QListWidget::item:selected { border: 2px solid %2; }"
        "QListWidget::item:hover { border: 1px solid %3; }")
        .arg(t->colorSurface.name(),          // panel/grid background
             t->colorSurfaceSelected.name(),  // selected swatch outline
             t->colorBorder.name())           // hover swatch outline
        + scrollBarQss(t->colorSurface, t->colorSurfaceHover, sb));  // track + handle

    m_emptyHint->setStyleSheet(QStringLiteral("color: %1; padding: 12px;")
                                   .arg(t->colorTextSecondary.name()));

    const QString btnStyle = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 3px; }"
        "QPushButton:hover { background: %1; }"
        "QPushButton:pressed { background: %2; }")
        .arg(t->colorSurfaceHover.name(), t->colorSurfacePressed.name());
    m_newCollectionBtn->setStyleSheet(btnStyle);
    m_newFolderBtn->setStyleSheet(btnStyle);
    m_deleteBtn->setStyleSheet(btnStyle);

    setStyleSheet(QStringLiteral("SwatchesPanel { background: %1; color: %2; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));
    if (m_actionBar) {
        m_actionBar->setStyleSheet(QStringLiteral(
            "#swatchesActionBar { background: %1; border-top: 1px solid %2; }")
            .arg(t->colorSurface.name(), t->colorBorder.name()));
    }
}

void SwatchesPanel::updateGridCap()
{
    const int pitch = qMax(1, m_grid->gridSize().height());
    // Cap the grid so it never grows past the point where the preset tree would
    // drop below its minimum (plus the bottom action bar).
    constexpr int kBottomBarH = 34;
    const int avail = height() - m_tree->minimumHeight() - kBottomBarH;
    m_grid->setMaxContentRows(qMax(2, avail / pitch));
}

void SwatchesPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateGridCap();
}

// --- Tree ------------------------------------------------------------------

void SwatchesPanel::addTreeNodesRecursive(const SwatchNode* node, QTreeWidgetItem* parentItem)
{
    auto* item = parentItem ? new QTreeWidgetItem(parentItem)
                            : new QTreeWidgetItem(m_tree);
    item->setText(0, node->name);
    item->setData(0, kIdRole, node->id);
    item->setData(0, kTypeRole, node->isFolder() ? 0 : 1);
    if (node->isFolder()) {
        item->setIcon(0, makeIcon(QStringLiteral(":/icons/ui-folder-open.png")));
        for (const auto& child : node->children)
            addTreeNodesRecursive(child.get(), item);
    } else {
        item->setIcon(0, makeCollectionIcon(node));
    }
}

void SwatchesPanel::rebuildTree()
{
    const QString prev = m_currentCollectionId.isEmpty() ? selectedNodeId() : m_currentCollectionId;

    m_buildingTree = true;
    m_tree->clear();
    auto* mgr = SwatchManager::instance();
    for (const auto& rootNode : mgr->roots())
        addTreeNodesRecursive(rootNode.get(), nullptr);
    m_tree->expandAll();
    m_buildingTree = false;

    if (!prev.isEmpty() && mgr->findNode(prev))
        selectNodeById(prev);
    if (m_tree->selectedItems().isEmpty())
        selectNodeById(mgr->firstCollectionId());
    onTreeSelectionChanged();
}

void SwatchesPanel::selectNodeById(const QString& id)
{
    if (id.isEmpty())
        return;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, kIdRole).toString() == id) {
            m_tree->setCurrentItem(*it);
            return;
        }
        ++it;
    }
}

QString SwatchesPanel::selectedNodeId() const
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return QString();
    return items.first()->data(0, kIdRole).toString();
}

void SwatchesPanel::onTreeSelectionChanged()
{
    if (m_buildingTree)
        return;
    const auto items = m_tree->selectedItems();
    if (!items.isEmpty() && items.first()->data(0, kTypeRole).toInt() == 1)
        m_currentCollectionId = items.first()->data(0, kIdRole).toString();
    else
        m_currentCollectionId.clear();
    refreshColors();
}

void SwatchesPanel::onTreeItemExpanded(QTreeWidgetItem* item)
{
    if (item && item->data(0, kTypeRole).toInt() == 0)
        item->setIcon(0, makeIcon(QStringLiteral(":/icons/ui-folder-open.png")));
}

void SwatchesPanel::onTreeItemCollapsed(QTreeWidgetItem* item)
{
    if (item && item->data(0, kTypeRole).toInt() == 0)
        item->setIcon(0, makeIcon(QStringLiteral(":/icons/ui-folder-close.png")));
}

// --- Colors grid -----------------------------------------------------------

void SwatchesPanel::onColorsChanged(const QString& collectionId)
{
    if (collectionId == m_currentCollectionId)
        refreshColors();
}

void SwatchesPanel::refreshColors()
{
    m_grid->clear();
    SwatchNode* col = SwatchManager::instance()->findCollection(m_currentCollectionId);
    if (!col) {
        m_grid->hide();
        m_emptyHint->show();
        return;
    }
    m_emptyHint->hide();
    m_grid->show();
    for (const SwatchColor& sc : col->colors) {
        auto* item = new QListWidgetItem(m_grid);
        item->setIcon(QIcon(makeSwatchPixmap(sc.color, kSwatchSize)));
        item->setData(kIdRole, sc.id);
        item->setToolTip(tooltipForColor(sc));
        item->setSizeHint(m_grid->gridSize());   // keep cell == row pitch
    }
    updateGridCap();
    m_grid->refreshContentHeight();
}

void SwatchesPanel::onColorClicked(QListWidgetItem* item)
{
    if (!item || !m_engine)
        return;
    SwatchNode* col = SwatchManager::instance()->findCollection(m_currentCollectionId);
    if (!col)
        return;
    const QString colorId = item->data(kIdRole).toString();
    for (const SwatchColor& sc : col->colors) {
        if (sc.id == colorId) {
            const bool background =
                QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::MetaModifier);
            if (background)
                m_engine->setBackgroundColor(sc.color);
            else
                m_engine->setForegroundColor(sc.color);
            return;
        }
    }
}

void SwatchesPanel::onColorDoubleClicked(QListWidgetItem* item)
{
    editColorItem(item);
}

void SwatchesPanel::editColorItem(QListWidgetItem* item)
{
    if (!item)
        return;
    SwatchNode* col = SwatchManager::instance()->findCollection(m_currentCollectionId);
    if (!col)
        return;
    const QString colorId = item->data(kIdRole).toString();
    QColor current;
    QString name;
    for (const SwatchColor& sc : col->colors) {
        if (sc.id == colorId) { current = sc.color; name = sc.name; break; }
    }
    if (!current.isValid())
        return;

    auto* dlg = new ColorPickerDialog(current, ColorPickerMode::Foreground, this);
    const QString collectionId = m_currentCollectionId;
    connect(dlg, &ColorPickerDialog::colorAccepted, this,
            [collectionId, colorId, name](const QColor& color) {
                SwatchManager::instance()->setColor(collectionId, colorId, color, name);
            });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void SwatchesPanel::renameColorItem(QListWidgetItem* item)
{
    if (!item)
        return;
    SwatchNode* col = SwatchManager::instance()->findCollection(m_currentCollectionId);
    if (!col)
        return;
    const QString colorId = item->data(kIdRole).toString();
    QColor current;
    QString name;
    for (const SwatchColor& sc : col->colors) {
        if (sc.id == colorId) { current = sc.color; name = sc.name; break; }
    }
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Color"), tr("Name:"),
                                                  QLineEdit::Normal, name, &ok);
    if (ok)
        SwatchManager::instance()->setColor(m_currentCollectionId, colorId, current, newName);
}

void SwatchesPanel::onGridReordered()
{
    if (m_currentCollectionId.isEmpty())
        return;
    QVector<QString> order;
    order.reserve(m_grid->count());
    for (int i = 0; i < m_grid->count(); ++i)
        order.push_back(m_grid->item(i)->data(kIdRole).toString());
    SwatchManager::instance()->reorderColors(m_currentCollectionId, order);
}

void SwatchesPanel::addColorToCurrent(const QColor& color, const QString& name)
{
    if (!color.isValid())
        return;
    auto* mgr = SwatchManager::instance();
    QString target = m_currentCollectionId;
    if (!mgr->findCollection(target)) {
        // No collection selected — fall back to the first, or create one.
        target = mgr->firstCollectionId();
        if (target.isEmpty())
            target = mgr->createCollection(SwatchManager::defaultCollectionName());
        m_currentCollectionId = target;
        selectNodeById(target);
    }
    mgr->addColor(target, color, name);
}

void SwatchesPanel::onGridContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    QListWidgetItem* item = m_grid->itemAt(pos);

    QAction* addFg = menu.addAction(tr("Add Foreground Color"));
    QAction* addBg = menu.addAction(tr("Add Background Color"));
    QAction* addNew = menu.addAction(tr("New Color…"));
    QAction* editAct = nullptr;
    QAction* renameAct = nullptr;
    QAction* deleteAct = nullptr;
    if (item) {
        menu.addSeparator();
        editAct = menu.addAction(tr("Edit…"));
        renameAct = menu.addAction(tr("Rename…"));
        deleteAct = menu.addAction(tr("Delete Color"));
    }

    QAction* chosen = menu.exec(m_grid->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == addFg && m_engine) {
        addColorToCurrent(m_engine->foregroundColor());
    } else if (chosen == addBg && m_engine) {
        addColorToCurrent(m_engine->backgroundColor());
    } else if (chosen == addNew) {
        const QColor seed = m_engine ? m_engine->foregroundColor() : QColor(Qt::white);
        auto* dlg = new ColorPickerDialog(seed, ColorPickerMode::Foreground, this);
        connect(dlg, &ColorPickerDialog::colorAccepted, this,
                [this](const QColor& color) { addColorToCurrent(color); });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    } else if (chosen == editAct) {
        editColorItem(item);
    } else if (chosen == renameAct) {
        renameColorItem(item);
    } else if (chosen == deleteAct) {
        SwatchManager::instance()->removeColor(m_currentCollectionId,
                                               item->data(kIdRole).toString());
    }
}

// --- Structural operations -------------------------------------------------

QString SwatchesPanel::parentForNewNode() const
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return QString();
    QTreeWidgetItem* sel = items.first();
    if (sel->data(0, kTypeRole).toInt() == 0)
        return sel->data(0, kIdRole).toString();          // selected folder → inside it
    QTreeWidgetItem* parent = sel->parent();               // selected collection → its sibling
    return parent ? parent->data(0, kIdRole).toString() : QString();
}

void SwatchesPanel::onNewCollection()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Palette"), tr("Palette name:"),
                                               QLineEdit::Normal, tr("Untitled Palette"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    const QString id = SwatchManager::instance()->createCollection(name, parentForNewNode());
    if (!id.isEmpty()) {
        m_currentCollectionId = id;
        selectNodeById(id);
    }
}

void SwatchesPanel::onNewFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, tr("New Folder"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    const QString id = SwatchManager::instance()->createFolder(name, parentForNewNode());
    if (!id.isEmpty())
        selectNodeById(id);
}

void SwatchesPanel::onDeleteSelected()
{
    // Prefer deleting a selected color; otherwise delete the selected tree node.
    if (QListWidgetItem* gridItem = m_grid->currentItem();
        gridItem && m_grid->hasFocus() && !m_currentCollectionId.isEmpty()) {
        SwatchManager::instance()->removeColor(m_currentCollectionId,
                                               gridItem->data(kIdRole).toString());
        return;
    }

    const QString id = selectedNodeId();
    SwatchNode* node = SwatchManager::instance()->findNode(id);
    if (!node)
        return;
    const bool isFolder = node->isFolder();
    const QString question = isFolder
        ? tr("Delete folder “%1” and all of its contents?").arg(node->name)
        : tr("Delete palette “%1”?").arg(node->name);
    if (QMessageBox::question(this, tr("Delete"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        SwatchManager::instance()->removeNode(id);
    }
}

void SwatchesPanel::onTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (item)
        m_tree->setCurrentItem(item);

    QMenu menu(this);
    QAction* newPalette = menu.addAction(tr("New Palette…"));
    QAction* newFolder = menu.addAction(tr("New Folder…"));
    QAction* renameAct = nullptr;
    QAction* deleteAct = nullptr;
    if (item) {
        menu.addSeparator();
        renameAct = menu.addAction(tr("Rename…"));
        deleteAct = menu.addAction(tr("Delete"));
    }

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == newPalette) {
        onNewCollection();
    } else if (chosen == newFolder) {
        onNewFolder();
    } else if (chosen == renameAct && item) {
        bool ok = false;
        const QString newName = QInputDialog::getText(this, tr("Rename"), tr("Name:"),
                                                      QLineEdit::Normal, item->text(0), &ok);
        if (ok && !newName.trimmed().isEmpty())
            SwatchManager::instance()->renameNode(item->data(0, kIdRole).toString(), newName);
    } else if (chosen == deleteAct) {
        onDeleteSelected();
    }
}
