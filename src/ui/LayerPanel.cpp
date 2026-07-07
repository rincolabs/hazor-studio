#include "LayerPanel.hpp"
#include "LayerItemDelegate.hpp"
#include "ScrubbableValueInput.h"
#include "IconUtils.hpp"
#include "controller/ImageController.hpp"
#include "core/AdjustmentTypes.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QPushButton>
#include <QComboBox>
#include <QButtonGroup>
#include <QLabel>
#include <QIcon>
#include <QMenu>
#include <QHeaderView>
#include <QDropEvent>
#include <QEvent>
#include <QInputDialog>
#include <QLineEdit>

#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrent>
#include <QMouseEvent>
#include <QStyleOptionViewItem>
#include <QKeyEvent>
#include <QApplication>
#include <QWidget>
#include <QPersistentModelIndex>
#include <QItemSelectionModel>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <vector>

namespace {
static constexpr int kDraggingRole = Qt::UserRole + 200;
class LayerTreeWidget : public QTreeWidget {
public:
    explicit LayerTreeWidget(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {}

protected:
    static bool isItemInTree(const QTreeWidgetItem* item, const QTreeWidget* tree)
    {
        return item && tree && item->treeWidget() == tree;
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        m_pressItem = nullptr;
        m_preserveSelectionOnPress = false;
        if (event && event->button() == Qt::LeftButton) {
            m_pressPos = event->pos();
            m_pressItem = itemAt(event->pos());
            if (isItemInTree(m_pressItem, this)) {
                const bool plainClick = !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
                const bool preserveMultiSelection = plainClick
                    && m_pressItem->isSelected()
                    && selectedItems().size() > 1;
                if (preserveMultiSelection) {
                    m_preserveSelectionOnPress = true;
                    // Keep selection set intact while updating only current item.
                    setCurrentItem(m_pressItem, 0, QItemSelectionModel::NoUpdate);
                    return;
                }
                if (plainClick)
                    setCurrentItem(m_pressItem);
                // For plain clicks setCurrentItem clears selection; ExtendedSelection
                // handles Ctrl/Shift natively, so we skip to preserve multi-select.
            }
        }
        QTreeWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_preserveSelectionOnPress) {
            if (m_pressItem && event && (event->buttons() & Qt::LeftButton)) {
                const int dist = (event->pos() - m_pressPos).manhattanLength();
                if (dist >= QApplication::startDragDistance()) {
                    startDrag(Qt::MoveAction);
                    m_pressItem = nullptr;
                    m_preserveSelectionOnPress = false;
                }
            }
            return;
        }

        if (m_pressItem && event && (event->buttons() & Qt::LeftButton)) {
            const int dist = (event->pos() - m_pressPos).manhattanLength();
            if (dist >= QApplication::startDragDistance()) {
                startDrag(Qt::MoveAction);
                m_pressItem = nullptr;
                m_preserveSelectionOnPress = false;
                return;
            }
        }
        QTreeWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_preserveSelectionOnPress) {
            m_preserveSelectionOnPress = false;
            m_pressItem = nullptr;
            return;
        }
        m_pressItem = nullptr;
        QTreeWidget::mouseReleaseEvent(event);
    }


    void startDrag(Qt::DropActions supportedActions) override
    {
        QTreeWidgetItem* item = currentItem();
        const QPersistentModelIndex dragIndex(
            item ? QPersistentModelIndex(indexFromItem(item, 0))
                 : QPersistentModelIndex());
        if (dragIndex.isValid())
            model()->setData(dragIndex, true, kDraggingRole);
        viewport()->update();

        QTreeWidget::startDrag(supportedActions);

        if (dragIndex.isValid())
            model()->setData(dragIndex, false, kDraggingRole);
        viewport()->update();
    }

private:
    QPoint m_pressPos;
    QTreeWidgetItem* m_pressItem = nullptr;
    bool m_preserveSelectionOnPress = false;
};
} // namespace

static const char* kBlendModeNames[] = {
    "Normal", "Multiply", "Screen", "Overlay",
    "Darken", "Lighten", "Color Dodge", "Color Burn",
    "Hard Light", "Soft Light", "Difference", "Exclusion",
    "Hue", "Saturation", "Color", "Luminosity"
};
static constexpr int kNumBlendModes = sizeof(kBlendModeNames) / sizeof(kBlendModeNames[0]);

static int blendModeToComboIndex(BlendMode mode)
{
    return static_cast<int>(mode);
}

static BlendMode comboIndexToBlendMode(int idx)
{
    if (idx < 0 || idx >= kNumBlendModes) return BlendMode::Normal;
    return static_cast<BlendMode>(idx);
}

LayerPanel::LayerPanel(QWidget* parent)
    : QWidget(parent)
{
    connect(&m_thumbWatcher,
            &QFutureWatcher<std::shared_ptr<ThumbBatch>>::finished,
            this, &LayerPanel::onThumbnailsReady);

    auto* t = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("layerPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    // layout->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(t->spaceSM);

    m_topBar = new QWidget(this);
    auto* topLay = new QVBoxLayout(m_topBar);
    m_topBar->setObjectName("layerTopBar");
    // topBar->setStyleSheet(QStringLiteral(R"(
    //         #layerTopBar {
    //             border-bottom: 1px solid %1;
    //         }
    //     )").arg(t->colorBorder.name())
    // );

    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->setSpacing(2);

    // Row 1: blend mode + opacity
    m_blendRow = new QWidget(m_topBar);
    m_blendRow->setObjectName("layerBlendModes");
    auto* row1 = new QHBoxLayout(m_blendRow);
    row1->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    row1->setSpacing(t->spaceMD);

    m_blendCombo = new QComboBox(m_blendRow);

    for (int i = 0; i < kNumBlendModes; ++i)
        m_blendCombo->addItem(tr(kBlendModeNames[i]));
    m_blendCombo->setToolTip(tr("Blending Mode"));
    m_blendCombo->setMinimumWidth(80);
    row1->addWidget(m_blendCombo, 1);

    m_opacitySlider = new ScrubbableValueInput(tr("Opacity"), 0, 100, 100, tr("%"), 1, m_blendRow);
    row1->addWidget(m_opacitySlider);

    topLay->addWidget(m_blendRow);

    // Row 2: kind + lock label + lock buttons
    m_lockRow = new QWidget(m_topBar);
    m_lockRow->setObjectName("layerLock");

    auto* row2 = new QHBoxLayout(m_lockRow);
    // row2->setContentsMargins(t->spaceXS, t->spaceXS, t->spaceXS, t->spaceMD);
    row2->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, 0);
    row2->setSpacing(t->spaceMD);



    auto* lockLabel = new QLabel(tr("Lock:"), m_lockRow);
    row2->addWidget(lockLabel);

    m_lockGroup = new QButtonGroup(this);
    m_lockGroup->setExclusive(false);

    auto makeLockBtn = [&](const QString& iconPath, const QString& tip, int id) {
        auto* btn = new QPushButton(m_lockRow);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(18, 18));
        btn->setCheckable(true);
        btn->setFixedSize(24, 24);
        btn->setToolTip(tip);
        btn->setProperty("lockFlag", id);
        m_lockGroup->addButton(btn, id);
        row2->addWidget(btn);
    };

    makeLockBtn(":/icons/lock-transparent.png", tr("Lock Transparent Pixels"), LockTransparent);
    makeLockBtn(":/icons/lock-image.png",       tr("Lock Image Pixels"),      LockImage);
    makeLockBtn(":/icons/lock-position.png",    tr("Lock Position"),          LockPosition);
    makeLockBtn(":/icons/lock-all.png",         tr("Lock All"),               LockAll);

    row2->addStretch();

    m_kindCombo = new QComboBox(m_lockRow);
    m_kindCombo->addItems({tr("Kind"), tr("Pixel"), tr("Text"), tr("Vector"),
                           tr("Adjustment") });
    m_kindCombo->setToolTip(tr("Filter by layer type"));
    m_kindCombo->setMinimumWidth(80);
    row2->addWidget(m_kindCombo);

    topLay->addWidget(m_lockRow);

    layout->addWidget(m_topBar);

    m_tree = new LayerTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setAnimated(true);
    m_tree->setStyleSheet(t->layerPanelStyleSheet());
    m_tree->setIndentation(0);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setFocusPolicy(Qt::StrongFocus);
    m_tree->viewport()->installEventFilter(this);
    m_tree->installEventFilter(this);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setMouseTracking(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    setFocusProxy(m_tree);
    layout->addWidget(m_tree, 1);

    m_actionBar = new QWidget(this);
    m_actionBar->setObjectName(QStringLiteral("layerActionBar"));
    auto* bottomLayout = new QHBoxLayout(m_actionBar);
    bottomLayout->setSpacing(t->spaceMD);
    auto makeIconBtn = [&](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(28, 28);
        btn->setToolTip(tooltip);
        return btn;
    };
    m_addBtn = makeIconBtn(":/icons/add-layer.png",    tr("Add Layer"));
    m_groupBtn = makeIconBtn(":/icons/add-group.png", tr("Add Group"));
    m_adjustBtn = makeIconBtn(":/icons/layer-adjustments.png",
                              tr("New Adjustment Layer"));
    m_removeBtn = makeIconBtn(":/icons/delete-layer.png", tr("Delete Layer"));
    m_dupBtn = makeIconBtn(":/icons/duplicate-layer.png", tr("Duplicate Layer"));
    m_maskBtn = makeIconBtn(":/icons/layer-mask.png",  tr("Add Layer Mask"));

    // Dropdown listing the available adjustments (Grayscale today; the menu
    // grows with adjustments::registry()).
    auto* adjustMenu = new QMenu(m_adjustBtn);
    for (const auto& info : adjustments::registry()) {
        const QString id = info.id;
        adjustMenu->addAction(info.displayName, this, [this, id]() {
            // Solid Color opens the colour picker first (cancel = no layer);
            // MainWindow owns that flow.
            if (id == QLatin1String("solidcolor")) {
                emit solidColorRequested();
                return;
            }
            if (m_controller)
                m_controller->addAdjustmentLayer(id);
        });
    }
    m_adjustBtn->setMenu(adjustMenu);

    bottomLayout->addStretch();
    bottomLayout->addWidget(m_addBtn);
    bottomLayout->addWidget(m_groupBtn);
    bottomLayout->addWidget(m_adjustBtn);
    bottomLayout->addWidget(m_dupBtn);
    bottomLayout->addWidget(m_maskBtn);
    bottomLayout->addWidget(m_removeBtn);
    layout->addWidget(m_actionBar);

    connect(m_lockGroup, &QButtonGroup::idClicked,
            this, &LayerPanel::onLockButtonClicked);
    connect(m_blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayerPanel::onBlendModeChanged);
    connect(m_opacitySlider, &ScrubbableValueInput::editingStarted, this, [this]() {
        if (!m_controller) return;
        int active = m_controller->document()->activeFlatIndex;
        m_controller->beginNodeOpacity(active);
    });
    connect(m_opacitySlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        if (!m_controller) return;
        int active = m_controller->document()->activeFlatIndex;
        m_controller->previewNodeOpacity(active, static_cast<float>(v) / 100.0f);
    });
    connect(m_opacitySlider, &ScrubbableValueInput::editingFinished, this, [this](double v) {
        if (!m_controller) return;
        int active = m_controller->document()->activeFlatIndex;
        m_controller->commitNodeOpacity(active, static_cast<float>(v) / 100.0f);
    });

    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, &LayerPanel::onCurrentItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_updating || !m_controller) return;
        QSignalBlocker blocker(m_tree);

        // Ensure at least one item stays selected when multi-selection loses everything
        if (m_tree->selectedItems().isEmpty() && m_tree->topLevelItemCount() > 0) {
            QTreeWidgetItem* fallback = m_tree->currentItem();
            if (!fallback || fallback->treeWidget() != m_tree) {
                fallback = m_tree->topLevelItem(0);
            }
            if (fallback && fallback->treeWidget() == m_tree) {
                m_tree->setCurrentItem(fallback);
                return;
            }
        }

        syncTreeSelectionToDocument();
    });

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
        if (m_updating || !m_controller || col != 0) return;
        int flatIdx = item->data(0, Qt::UserRole).toInt();
        QString newName = item->text(0).trimmed();
        if (newName.isEmpty()) return;
        auto* node = m_controller->document()->nodeAt(flatIdx);
        if (!node) return;
        node->name = newName;
        node->nameIsAuto = false;   // user picked this name; stop auto-naming it
        if (node->layer) node->layer->name = newName;
    });

    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (!m_controller) return;
        int idx = item->data(0, Qt::UserRole).toInt();
        auto* node = m_controller->document()->nodeAt(idx);
        if (node) node->collapsed = false;
    });

    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        if (!m_controller) return;
        int idx = item->data(0, Qt::UserRole).toInt();
        auto* node = m_controller->document()->nodeAt(idx);
        if (node) node->collapsed = true;
    });

    connect(m_addBtn, &QPushButton::clicked, this, &LayerPanel::addLayerRequested);
    connect(m_groupBtn, &QPushButton::clicked, this, [this]() {
        if (m_controller) m_controller->newGroup();
    });
    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refresh(); });
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (m_controller) m_controller->removeSelectedNodes();
    });
    connect(m_dupBtn, &QPushButton::clicked, this, [this]() {
        if (!m_controller) return;
        auto* item = m_tree->currentItem();
        if (item) {
            int idx = item->data(0, Qt::UserRole).toInt();
            m_controller->duplicateNode(idx);
        }
    });
    connect(m_maskBtn, &QPushButton::clicked, this, [this]() {
        if (!m_controller) return;
        auto* item = m_tree->currentItem();
        if (!item) return;
        int idx = item->data(0, Qt::UserRole).toInt();
        // This button only ever *adds* a mask — it never removes one. The user sees
        // a single "Add Mask" action; the two variants below are transparent:
        //  • an active canvas selection builds the mask from that selection;
        //  • otherwise a fresh mask is added (Alt+Click = Hide-All / black).
        Document* doc = m_controller->document();
        const bool hasSelection = doc && doc->selection.active() && !doc->selection.isEmpty();
        if (hasSelection) {
            m_controller->createMaskFromSelection(idx);
        } else if (!m_controller->hasLayerMask(idx)) {
            const bool alt = QApplication::keyboardModifiers() & Qt::AltModifier;
            m_controller->addLayerMask(idx, /*revealAll=*/!alt);
        }
    });

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyTheme();
    });
    applyTheme();

    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);

        QTreeWidgetItem* item = m_tree->itemAt(pos);
        int idx = item ? item->data(0, Qt::UserRole).toInt() : -1;

        if (idx >= 0) {
            menu.addAction(tr("Duplicate Layer"), [this, idx]() {
                if (m_controller) m_controller->duplicateNode(idx);
            });
            menu.addAction(tr("Merge Down"), [this, idx]() {
                if (!m_controller)
                    return;
                // Flat order puts the stack TOP at index 0, so the layer below
                // is at a HIGHER index — and not necessarily idx+1 (clipped
                // adjustments occupy flat slots; groups must not be crossed).
                // mergeDownTargetFlat resolves the real target; idx-1 pointed
                // at the layer ABOVE and merged the wrong way.
                const int dst = m_controller->mergeDownTargetFlat(idx);
                if (dst >= 0)
                    m_controller->mergeLayers(idx, dst);
            });
            menu.addSeparator();
            bool hasMask = m_controller && m_controller->hasLayerMask(idx);
            menu.addAction(hasMask ? tr("Remove Layer Mask") : tr("Add Layer Mask"),
                           [this, idx]() {
                if (m_controller) m_controller->toggleLayerMask(idx);
            });
            if (hasMask) {
                menu.addAction(tr("Invert Mask"), [this, idx]() {
                    if (m_controller) m_controller->invertLayerMask(idx);
                });
                menu.addAction(tr("Clear Mask"), [this, idx]() {
                    if (m_controller) m_controller->clearLayerMask(idx);
                });
                menu.addAction(tr("Apply Mask"), [this, idx]() {
                    if (m_controller) m_controller->applyLayerMask(idx);
                });
                menu.addSeparator();
                menu.addAction(tr("Copy Mask"), [this, idx]() {
                    if (m_controller) m_controller->copyLayerMask(idx);
                });
            }
            if (m_controller && m_controller->hasCopiedMask()) {
                menu.addAction(tr("Paste Mask"), [this, idx]() {
                    if (m_controller) m_controller->pasteLayerMask(idx);
                });
            }

            if (m_controller) {
                auto* node = m_controller->document() ? m_controller->document()->nodeAt(idx) : nullptr;
                const bool isPixelLayer = node && node->type == LayerTreeNode::Type::Layer
                    && node->layer && !node->layer->isTextLayer() && !node->layer->isShapeLayer();
                if (isPixelLayer) {
                    menu.addSeparator();
                    menu.addAction(tr("AI Upscale..."), [this, idx]() {
                        emit aiUpscaleRequested(idx);
                    });
                }
            }

            // ── Layer Style (effects) ──
            auto* node = m_controller ? m_controller->document()->nodeAt(idx) : nullptr;
            const bool hasEffects = node && !node->effects.empty();
            if (hasEffects || (m_controller && m_controller->hasCopiedEffects())) {
                menu.addSeparator();
                if (hasEffects) {
                    menu.addAction(tr("Copy Layer Style"), [this, idx]() {
                        if (m_controller) m_controller->copyLayerEffects(idx);
                    });
                }
                if (m_controller && m_controller->hasCopiedEffects()) {
                    menu.addAction(tr("Paste Layer Style"), [this]() {
                        if (m_controller) m_controller->pasteLayerEffects();
                    });
                }
                if (hasEffects) {
                    menu.addAction(tr("Enable/Disable Effects"), [this, idx]() {
                        if (m_controller) m_controller->toggleAllLayerEffects(idx);
                    });
                    menu.addAction(tr("Clear Layer Style"), [this, idx]() {
                        if (m_controller) m_controller->clearLayerEffects(idx);
                    });
                }
            }

            menu.addSeparator();
            menu.addAction(tr("Rename..."), [this, idx]() {
                if (!m_controller) return;
                auto* node = m_controller->document()->nodeAt(idx);
                if (!node) return;
                bool ok;
                QString newName = QInputDialog::getText(
                    this, tr("Rename"), tr("Name:"),
                    QLineEdit::Normal, node->name, &ok);
                if (ok && !newName.isEmpty()) {
                    node->name = newName;
                    node->nameIsAuto = false;   // user picked this name; stop auto-naming it
                    if (node->layer) node->layer->name = newName;
                    refresh();
                }
            });
            menu.addSeparator();
            menu.addAction(tr("Delete"), [this]() {
                if (m_controller) m_controller->removeSelectedNodes();
            });
        }
        menu.addSeparator();
        menu.addAction(tr("New Layer"), [this]() {
            emit addLayerRequested();
        });
        menu.addAction(tr("New Group"), [this]() {
            if (m_controller) m_controller->newGroup();
        });

        menu.exec(m_tree->mapToGlobal(pos));
    });
}

void LayerPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral(
        "LayerPanel { background: %1; color: %2; }"
        "#layerTopBar, #layerActionBar { background: %1; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));

    if (m_blendRow) {
        m_blendRow->setStyleSheet(QStringLiteral(R"(
            #layerBlendModes {
                background: %1;
                border-bottom: 1px solid %2;
                border-top: 1px solid %2;
                padding: 0px;
                margin: 0px;
            }
        )").arg(t->colorSurface.name(), t->colorBorder.name()));
    }

    if (m_lockRow) {
        m_lockRow->setStyleSheet(QStringLiteral(R"(
            #layerLock {
                background: %1;
                border-bottom: 0px solid %2;
                padding: 0px;
                margin: 0px;
            }
        )").arg(t->colorSurface.name(), t->colorBorder.name()));
    }

    if (m_actionBar)
        m_actionBar->setStyleSheet(QStringLiteral("#layerActionBar { background: %1; }")
            .arg(t->colorSurface.name()));

    if (m_tree)
        m_tree->setStyleSheet(t->layerPanelStyleSheet());
}

void LayerPanel::setController(ImageController* ctrl)
{
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }
    m_controller = ctrl;

    if (m_delegate)
        m_delegate->deleteLater();
    m_delegate = new LayerItemDelegate(ctrl, this);
    m_tree->setItemDelegate(m_delegate);

    connect(m_delegate, &LayerItemDelegate::visibilityToggled,
            this, &LayerPanel::onVisibilityToggled);
    connect(m_delegate, &LayerItemDelegate::visibilitySoloRequested,
            this, [this](int flatIndex) {
        if (m_controller)
            m_controller->toggleSoloVisibility(flatIndex);
    });
    connect(m_delegate, &LayerItemDelegate::lockToggled,
            this, &LayerPanel::onLockToggled);
    connect(m_delegate, &LayerItemDelegate::layerStylesRequested,
            this, &LayerPanel::layerStylesRequested);
    connect(m_delegate, &LayerItemDelegate::solidColorThumbDoubleClicked,
            this, &LayerPanel::solidColorEditRequested);

    connect(m_delegate, &LayerItemDelegate::maskToggled,
            this, [this](int flatIndex, Qt::KeyboardModifiers mods) {
        if (!m_controller) return;
        const bool ctrl  = mods & Qt::ControlModifier;
        const bool shift = mods & Qt::ShiftModifier;
        const bool alt   = mods & Qt::AltModifier;
        if (ctrl) {
            // Ctrl loads the mask into the selection; Shift/Alt pick the combine
            // rule (conventional): Ctrl=Replace, +Shift=Add, +Alt=Subtract,
            // +Shift+Alt=Intersect.
            SelectMode mode = SelectMode::Replace;
            if (shift && alt) mode = SelectMode::Intersect;
            else if (alt)     mode = SelectMode::Subtract;
            else if (shift)   mode = SelectMode::Add;
            m_controller->combineMaskToSelection(flatIndex, mode);
        } else if (shift) {
            bool enabled = m_controller->isLayerMaskEnabled(flatIndex);
            m_controller->setLayerMaskEnabled(flatIndex, !enabled);
        } else if (alt) {
            emit grayscaleViewRequested(true);
        } else {
            // Plain click on the mask thumbnail: make this layer active and
            // target its mask. setActiveNode resets the target to pixels first
            // (activeLayerChanged), so re-enable mask right after. Returning to
            // pixels is done by clicking the pixel thumbnail or another row.
            m_controller->setActiveNode(flatIndex);
            m_controller->setEditingMask(true);
        }
    });

    if (ctrl) {
        connect(ctrl, &ImageController::layerChanged, this, [this](int) { refresh(); });
        connect(ctrl, &ImageController::activeLayerChanged, this, [this](int) { refresh(); });
        connect(ctrl, &ImageController::selectionChanged, this, [this]() { refresh(); });
        connect(ctrl, &ImageController::imageChanged, this, [this]() {
            // Hold the panel during an adjustment drag: a Single-Layer-Mode layer
            // thumbnail recomputes computeEffectedImage() (full-res CPU bake) on
            // every imageChanged, which on the shared GUI thread keeps the canvas
            // drag from being fluid. Nothing structural changes mid-drag; the
            // commit (live edit -> false) issues one refresh() with the result.
            if (m_adjustmentLiveEdit)
                return;
            refresh();
        });
        connect(ctrl, &ImageController::adjustmentLiveEditChanged, this,
                [this](bool active) {
            m_adjustmentLiveEdit = active;
            if (!active)
                refresh();
        });
        connect(ctrl, &ImageController::maskEditingChanged, this, [this](bool) {
            if (m_tree) m_tree->viewport()->update();
        });
    }
    refresh();
}

void LayerPanel::refresh()
{
    m_updating = true;
    m_tree->blockSignals(true);
    m_tree->clear();

    if (!m_controller || !m_controller->document())
        return;

    auto* doc = m_controller->document();
    auto flat = doc->flatten();

    QHash<LayerTreeNode*, QTreeWidgetItem*> itemMap;

    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        auto* node = flat[i];
        auto* item = new QTreeWidgetItem();
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, Qt::UserRole, i);
        item->setData(0, Qt::UserRole + 1, static_cast<int>(node->type));
        item->setData(0, Qt::UserRole + 2, node->collapsed ? 1 : 0);

        // Groups and layers hosting Single-Layer-Mode adjustments are
        // expandable in the tree.
        if (node->type == LayerTreeNode::Type::Group
            || node->hasVisibleAdjustmentChildren())
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

        item->setText(0, node->name.isEmpty()
            ? QString("Layer %1").arg(i + 1) : node->name);
        item->setToolTip(0, node->name);

        if (node->parent && itemMap.contains(node->parent))
            itemMap[node->parent]->addChild(item);
        else
            m_tree->addTopLevelItem(item);

        itemMap[node] = item;
    }

    for (auto it = itemMap.begin(); it != itemMap.end(); ++it) {
        const bool expandable = it.key()->type == LayerTreeNode::Type::Group
            || it.key()->hasVisibleAdjustmentChildren();
        if (expandable && !it.key()->collapsed)
            it.value()->setExpanded(true);
    }

    int activeIdx = doc->activeFlatIndex;
    if (activeIdx >= 0 && activeIdx < static_cast<int>(flat.size())) {
        auto* activeItem = itemMap.value(flat[activeIdx]);
        if (activeItem)
            m_tree->setCurrentItem(activeItem);
    }

    // Sync multi-selection from Document to tree
    m_tree->clearSelection();
    for (int selIdx : doc->selectedFlatIndices) {
        if (selIdx >= 0 && selIdx < static_cast<int>(flat.size())) {
            auto* selItem = itemMap.value(flat[selIdx]);
            if (selItem)
                selItem->setSelected(true);
        }
    }

    m_tree->blockSignals(false);
    m_updating = false;

    int kindIdx = m_kindCombo->currentIndex();
    if (kindIdx > 0) {
        auto matchesFilter = [&](LayerTreeNode* n) -> bool {
            if (!n) return false;
            switch (kindIdx) {
            case 1: return n->type == LayerTreeNode::Type::Layer && n->layer && !n->layer->isTextLayer() && !n->layer->isShapeLayer();
            case 2: return n->layer && n->layer->isTextLayer();
            case 3: return n->layer && n->layer->isShapeLayer();
            case 4: return n->type == LayerTreeNode::Type::Adjustment;
            default: return false;
            }
        };

        std::function<bool(QTreeWidgetItem*)> hasMatchingDescendant = [&](QTreeWidgetItem* item) -> bool {
            for (int i = 0; i < item->childCount(); ++i) {
                auto* child = item->child(i);
                if (matchesFilter(doc->nodeAt(child->data(0, Qt::UserRole).toInt()))) return true;
                if (hasMatchingDescendant(child)) return true;
            }
            return false;
        };

        QTreeWidgetItemIterator it(m_tree);
        while (*it) {
            int srcIdx = (*it)->data(0, Qt::UserRole).toInt();
            bool show = matchesFilter(doc->nodeAt(srcIdx)) || hasMatchingDescendant(*it);
            (*it)->setHidden(!show);
            ++it;
        }
    }

    updateTopBarFromActiveLayer();
    updateActiveTargetFromCurrent();

    // Bake any dirty effected/clipped-adjustment thumbnails off the UI thread.
    scheduleThumbnailBuild();
}

void LayerPanel::scheduleThumbnailBuild()
{
    if (!m_controller || !m_controller->document())
        return;
    auto* doc = m_controller->document();

    // Collect EVERY dirty pixel/shape layer thumbnail. All thumbnail baking runs
    // off the UI thread — the delegate never scales an image itself, so even a
    // huge merged layer can't freeze the panel. Text/adjustment nodes draw an
    // icon (no image bake) and are skipped.
    std::vector<int> indices;
    auto flat = doc->flatten();
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        auto* n = flat[i];
        if (n && n->type == LayerTreeNode::Type::Layer && n->layer
            && !n->layer->isTextLayer()
            && (n->thumbnailDirty || n->cachedThumb.isNull()))
            indices.push_back(i);
    }
    if (indices.empty())
        return;

    // One build at a time; if the tree changed while busy, rebuild once it lands.
    if (m_thumbWatcher.isRunning()) {
        m_thumbRescheduleNeeded = true;
        return;
    }

    // COW snapshot of the tree — pixel buffers are shared, safe to read on the
    // worker (a concurrent UI write detaches the original, not the snapshot).
    auto snapshot = std::make_shared<std::vector<std::unique_ptr<LayerTreeNode>>>();
    snapshot->reserve(doc->roots.size());
    for (const auto& root : doc->roots)
        snapshot->push_back(root->shallowClone());

    const quint64 token = ++m_thumbToken;
    constexpr int kThumbBakeMaxDim = 64;

    auto future = QtConcurrent::run(
        [snapshot, indices, token]() -> std::shared_ptr<ThumbBatch> {
            auto batch = std::make_shared<ThumbBatch>();
            batch->token = token;
            for (int idx : indices) {
                LayerTreeNode* n = LayerTreeNode::findByFlatIndex(*snapshot, idx);
                if (!n) continue;
                QImage thumb = n->computeEffectedThumbnail(kThumbBakeMaxDim);
                if (!thumb.isNull())
                    batch->thumbs.insert(idx, thumb);
            }
            return batch;
        });
    m_thumbWatcher.setFuture(future);
}

void LayerPanel::onThumbnailsReady()
{
    auto batch = m_thumbWatcher.result();
    if (batch && batch->token == m_thumbToken
        && m_controller && m_controller->document()) {
        auto* doc = m_controller->document();
        auto flat = doc->flatten();
        bool any = false;
        for (auto it = batch->thumbs.constBegin(); it != batch->thumbs.constEnd(); ++it) {
            const int idx = it.key();
            if (idx < 0 || idx >= static_cast<int>(flat.size()))
                continue;
            auto* n = flat[idx];
            // Re-validate against the live tree: apply only if the node at this
            // index is still a pixel/shape layer (stale structural changes bumped
            // m_thumbToken and were discarded above).
            if (!n || n->type != LayerTreeNode::Type::Layer || !n->layer
                || n->layer->isTextLayer())
                continue;
            n->cachedThumb = it.value();
            n->thumbnailDirty = false;
            any = true;
        }
        if (any && m_tree && m_tree->viewport())
            m_tree->viewport()->update();
    }

    if (m_thumbRescheduleNeeded) {
        m_thumbRescheduleNeeded = false;
        scheduleThumbnailBuild();
    }
}

void LayerPanel::onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous)
    if (!m_controller) return;
    if (!current) {
        if (previous && previous->treeWidget() == m_tree) {
            QSignalBlocker blocker(m_tree);
            m_tree->setCurrentItem(previous);
            current = previous;
        } else if (m_tree->topLevelItemCount() > 0) {
            QTreeWidgetItem* first = m_tree->topLevelItem(0);
            QSignalBlocker blocker(m_tree);
            m_tree->setCurrentItem(first);
            current = first;
        }
    }

    if (current) {
        int idx = current->data(0, Qt::UserRole).toInt();
        if (idx >= 0) {
            // Selection synced via itemSelectionChanged → setSelectedIndices
        }
    }
    updateTopBarFromActiveLayer();
    updateActiveTargetFromCurrent();
}

void LayerPanel::onVisibilityToggled(int flatIndex)
{
    if (!m_controller) return;
    auto* node = m_controller->document()->nodeAt(flatIndex);
    if (!node) return;
    m_controller->setNodeVisibility(flatIndex, !node->visible);
}

void LayerPanel::onLockToggled(int flatIndex, int)
{
    if (!m_controller) return;
    auto* node = m_controller->document()->nodeAt(flatIndex);
    if (!node) return;
    // Tree-row lock icon: master toggle. If anything is locked, clear all locks;
    // otherwise engage Lock All. Routed through the controller for one undo step.
    int newFlags = (node->lockFlags == LockNone) ? LockAll : LockNone;
    m_controller->setNodeLockFlags(flatIndex, newFlags);
    updateTopBarFromActiveLayer();
}

void LayerPanel::onBlendModeChanged(int index)
{
    if (!m_controller) return;
    auto* doc = m_controller->document();
    int active = doc->activeFlatIndex;
    if (active < 0 || active >= doc->flatCount()) return;
    m_controller->setNodeBlendMode(active, comboIndexToBlendMode(index));
}

void LayerPanel::onOpacityChanged(int value)
{
    if (!m_controller) return;
    auto* doc = m_controller->document();
    int active = doc->activeFlatIndex;
    if (active < 0 || active >= doc->flatCount()) return;
    m_controller->setNodeOpacity(active, static_cast<float>(value) / 100.0f);
}

void LayerPanel::onLockButtonClicked(int id)
{
    if (!m_controller) return;
    auto* doc = m_controller->document();
    if (!doc) return;
    auto* btn = m_lockGroup->button(id);
    if (!btn) return;

    // Apply the toggle to every selected layer (fallback: the active one), as a
    // single undoable step. `id` is the LockFlag bit; btn->isChecked() already
    // reflects the post-click state.
    std::vector<int> indices = doc->selectedList();
    if (indices.empty() && doc->activeFlatIndex >= 0
        && doc->activeFlatIndex < doc->flatCount())
        indices.push_back(doc->activeFlatIndex);
    if (!indices.empty())
        m_controller->setNodesLockBit(indices, id, btn->isChecked());

    updateTopBarFromActiveLayer();
}

void LayerPanel::focusLayerList()
{
    if (!m_tree)
        return;
    m_tree->setFocus(Qt::ShortcutFocusReason);
    if (!m_tree->currentItem()) {
        const auto rows = visibleLayerRows();
        if (!rows.isEmpty())
            setCurrentVisibleRow(0, false);
    }
}

QVector<QTreeWidgetItem*> LayerPanel::visibleLayerRows() const
{
    QVector<QTreeWidgetItem*> rows;
    if (!m_tree)
        return rows;

    std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item || item->isHidden())
            return;
        rows.append(item);
        if (!item->isExpanded())
            return;
        for (int i = 0; i < item->childCount(); ++i)
            visit(item->child(i));
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        visit(m_tree->topLevelItem(i));
    return rows;
}

int LayerPanel::currentVisibleRow(const QVector<QTreeWidgetItem*>& rows) const
{
    if (!m_tree)
        return -1;
    auto* current = m_tree->currentItem();
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i] == current)
            return i;
    }
    return -1;
}

bool LayerPanel::setCurrentVisibleRow(int row, bool extendSelection)
{
    const auto rows = visibleLayerRows();
    if (rows.isEmpty())
        return false;

    const int lastRow = static_cast<int>(rows.size()) - 1;
    row = std::clamp(row, 0, lastRow);
    auto* target = rows[row];
    if (!target)
        return false;

    const int previousRow = currentVisibleRow(rows);
    m_updating = true;
    m_tree->clearSelection();
    if (extendSelection && previousRow >= 0) {
        const int first = std::min(previousRow, row);
        const int last = std::max(previousRow, row);
        for (int i = first; i <= last; ++i)
            rows[i]->setSelected(true);
    } else {
        target->setSelected(true);
    }
    m_tree->setCurrentItem(target);
    m_tree->scrollToItem(target);
    m_updating = false;

    syncTreeSelectionToDocument();
    updateActiveTargetFromCurrent();
    return true;
}

bool LayerPanel::moveCurrentVisibleRow(int delta, bool extendSelection)
{
    const auto rows = visibleLayerRows();
    if (rows.isEmpty())
        return false;

    int row = currentVisibleRow(rows);
    if (row < 0)
        row = 0;
    return setCurrentVisibleRow(row + delta, extendSelection);
}

bool LayerPanel::syncTreeSelectionToDocument()
{
    if (!m_controller || !m_controller->document() || !m_tree)
        return false;

    std::set<int> indices;
    for (auto* item : m_tree->selectedItems()) {
        if (!item)
            continue;
        int idx = item->data(0, Qt::UserRole).toInt();
        if (idx >= 0)
            indices.insert(idx);
    }

    if (indices.empty() && m_tree->currentItem()) {
        const int idx = m_tree->currentItem()->data(0, Qt::UserRole).toInt();
        if (idx >= 0)
            indices.insert(idx);
    }

    m_updating = true;
    m_controller->setSelectedIndices(indices);
    m_updating = false;
    updateTopBarFromActiveLayer();
    updateActiveTargetFromCurrent();
    return true;
}

void LayerPanel::updateActiveTargetFromCurrent()
{
    m_activeTarget = ActiveTarget::None;
    if (!m_controller || !m_controller->document() || !m_tree)
        return;
    auto* item = m_tree->currentItem();
    if (!item)
        return;
    const int idx = item->data(0, Qt::UserRole).toInt();
    auto* node = m_controller->document()->nodeAt(idx);
    if (!node)
        return;
    if (node->type == LayerTreeNode::Type::Adjustment) {
        m_activeTarget = ActiveTarget::Adjustment;
    } else if (node->layer) {
        m_activeTarget = ActiveTarget::Pixels;
    }
}

bool LayerPanel::eventFilter(QObject* obj, QEvent* event)
{
    if ((obj == m_tree || obj == m_tree->viewport()) && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const bool shift = keyEvent->modifiers() & Qt::ShiftModifier;
        const bool plain = !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (plain) {
            switch (keyEvent->key()) {
            case Qt::Key_Up:
                return moveCurrentVisibleRow(-1, shift);
            case Qt::Key_Down:
                return moveCurrentVisibleRow(1, shift);
            case Qt::Key_Home:
                return setCurrentVisibleRow(0, shift);
            case Qt::Key_End: {
                const auto rows = visibleLayerRows();
                return !rows.isEmpty() && setCurrentVisibleRow(rows.size() - 1, shift);
            }
            case Qt::Key_PageUp: {
                const auto rows = visibleLayerRows();
                if (rows.isEmpty())
                    return false;
                const int current = std::max(0, currentVisibleRow(rows));
                const QRect rect = m_tree->visualItemRect(rows[current]);
                const int rowHeight = std::max(1, rect.height());
                const int pageRows = std::max(1, m_tree->viewport()->height() / rowHeight - 1);
                return setCurrentVisibleRow(current - pageRows, shift);
            }
            case Qt::Key_PageDown: {
                const auto rows = visibleLayerRows();
                if (rows.isEmpty())
                    return false;
                const int current = std::max(0, currentVisibleRow(rows));
                const QRect rect = m_tree->visualItemRect(rows[current]);
                const int rowHeight = std::max(1, rect.height());
                const int pageRows = std::max(1, m_tree->viewport()->height() / rowHeight - 1);
                return setCurrentVisibleRow(current + pageRows, shift);
            }
            case Qt::Key_Left: {
                auto* item = m_tree->currentItem();
                if (item && item->isExpanded()) {
                    item->setExpanded(false);
                    return true;
                }
                if (item && item->parent()) {
                    const auto rows = visibleLayerRows();
                    const int parentRow = rows.indexOf(item->parent());
                    return parentRow >= 0 && setCurrentVisibleRow(parentRow, false);
                }
                break;
            }
            case Qt::Key_Right: {
                auto* item = m_tree->currentItem();
                if (item && item->childCount() > 0 && !item->isExpanded()) {
                    item->setExpanded(true);
                    return true;
                }
                break;
            }
            case Qt::Key_F2:
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (auto* item = m_tree->currentItem()) {
                    m_tree->editItem(item, 0);
                    return true;
                }
                break;
            default:
                break;
            }
        }
        if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
            if (m_controller)
                m_controller->removeSelectedNodes();
            return true;
        }
    }
    if (obj == m_tree->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QModelIndex index = m_tree->indexAt(mouseEvent->pos());
        if (!index.isValid())
            return false;

        auto* delegate = qobject_cast<LayerItemDelegate*>(m_tree->itemDelegate());
        if (!delegate)
            return false;

        QStyleOptionViewItem option;
        option.rect = m_tree->visualRect(index);
        option.widget = m_tree->viewport();

        // Double-click on a Solid Color fill layer's colour thumbnail opens its
        // colour picker (emits solidColorThumbDoubleClicked → solidColorEditRequested).
        // Checked here because this filter consumes thumbnail double-clicks before
        // the view/delegate ever see them.
        if (delegate->handleSolidColorThumbDoubleClick(option, index, mouseEvent->pos()) >= 0)
            return true;

        if (!delegate->labelRect(option, index).contains(mouseEvent->pos()))
            return true;
    }
    if (obj == m_tree->viewport() && event->type() == QEvent::Drop) {
        if (!m_controller || !m_controller->document()) return false;

        auto* srcItem = m_tree->currentItem();
        if (!srcItem) return false;

        int srcIdx = srcItem->data(0, Qt::UserRole).toInt();
        auto* doc = m_controller->document();
        if (!doc) return false;

        auto* dropEvent = static_cast<QDropEvent*>(event);
        auto* targetItem = m_tree->itemAt(dropEvent->position().toPoint());

        auto isAncestor = [](LayerTreeNode* maybeAncestor,
                             LayerTreeNode* child) -> bool {
            for (auto* p = child; p; p = p->parent) {
                if (p == maybeAncestor)
                    return true;
            }
            return false;
        };

        auto findFlatIndex = [doc](LayerTreeNode* target) -> int {
            if (!doc || !target) return -1;
            auto now = doc->flatten();
            for (int i = 0; i < static_cast<int>(now.size()); ++i) {
                if (now[i] == target)
                    return i;
            }
            return -1;
        };

        // Build dragged set from selection if source item belongs to it,
        // otherwise drag only the current source item.
        std::vector<LayerTreeNode*> draggedNodes;
        {
            const bool srcSelected = srcItem->isSelected();
            auto flat = doc->flatten();
            std::set<LayerTreeNode*> uniq;

            if (srcSelected) {
                for (auto* selectedItem : m_tree->selectedItems()) {
                    if (!selectedItem) continue;
                    const int idx = selectedItem->data(0, Qt::UserRole).toInt();
                    if (idx < 0 || idx >= static_cast<int>(flat.size())) continue;
                    auto* candidate = flat[idx];
                    if (candidate)
                        uniq.insert(candidate);
                }
            } else {
                if (srcIdx >= 0 && srcIdx < static_cast<int>(flat.size()))
                    uniq.insert(flat[srcIdx]);
            }

            // Normalize: if parent is selected, skip selected descendants.
            for (auto* n : uniq) {
                bool descendantOfSelected = false;
                for (auto* p = n ? n->parent : nullptr; p; p = p->parent) {
                    if (uniq.count(p) > 0) {
                        descendantOfSelected = true;
                        break;
                    }
                }
                if (!descendantOfSelected)
                    draggedNodes.push_back(n);
            }
        }

        auto sortByFlat = [&]() {
            std::sort(draggedNodes.begin(), draggedNodes.end(),
                      [&](LayerTreeNode* a, LayerTreeNode* b) {
                          return findFlatIndex(a) < findFlatIndex(b);
                      });
        };
        sortByFlat();

        auto reorderMany = [&](int targetIndex) {
            if (draggedNodes.empty()) return;
            targetIndex = std::clamp(targetIndex, 0, doc->flatCount());
            for (auto* movingNode : draggedNodes) {
                const int movingIdx = findFlatIndex(movingNode);
                if (movingIdx < 0) continue;

                // Pass the raw target to reorderNode — it applies its own
                // internal adjustment for removal (toFlat-1 when moving down).
                // Do NOT pre-adjust here or the two corrections would compound.
                int insertAt = std::clamp(targetIndex, 0, doc->flatCount());

                m_controller->reorderNode(movingIdx, insertAt);
                targetIndex = insertAt + 1;
            }
        };

        // A single dragged adjustment is special-cased: dropping it ON a
        // compatible leaf layer engages Single Layer Mode; dropping it onto a
        // stack gap or a group returns it to Normal Mode.
        LayerTreeNode* draggedAdjustment =
            (draggedNodes.size() == 1 && draggedNodes.front()
             && draggedNodes.front()->isAdjustmentLayer())
                ? draggedNodes.front() : nullptr;

        if (targetItem) {
            QRect itemRect = m_tree->visualItemRect(targetItem);
            QPoint pt = dropEvent->position().toPoint();
            int relY = pt.y() - itemRect.top();
            int itemH = std::max(1, itemRect.height());
            bool topZone = relY < itemH / 4;
            bool onItem = (relY >= itemH / 4 && relY <= itemH * 3 / 4);
            bool bottomZone = relY > itemH * 3 / 4;

            int dstIdx = targetItem->data(0, Qt::UserRole).toInt();
            auto* dstNode = doc->nodeAt(dstIdx);

            auto isCompatibleAdjTarget = [](const LayerTreeNode* n) {
                return n && n->type == LayerTreeNode::Type::Layer && n->layer;
            };

            if (draggedAdjustment && onItem && isCompatibleAdjTarget(dstNode)
                && dstNode != draggedAdjustment) {
                // Single Layer Mode: nest under the compatible layer.
                const int adjIdx = findFlatIndex(draggedAdjustment);
                const int tgtIdx = findFlatIndex(dstNode);
                if (adjIdx >= 0 && tgtIdx >= 0)
                    m_controller->moveAdjustmentToLayer(adjIdx, tgtIdx);
            } else if (draggedAdjustment
                       && draggedAdjustment->parent
                       && draggedAdjustment->parent->type == LayerTreeNode::Type::Layer) {
                // Currently nested; a drop into a stack gap returns it to the
                // Normal-Mode stack.
                int targetIndex = dstIdx;
                if (bottomZone)
                    targetIndex = dstIdx + 1;
                const int adjIdx = findFlatIndex(draggedAdjustment);
                if (adjIdx >= 0)
                    m_controller->moveAdjustmentToStack(adjIdx, targetIndex);
            } else if (onItem && dstNode && dstNode->type == LayerTreeNode::Type::Group) {
                for (auto* movingNode : draggedNodes) {
                    if (!movingNode || movingNode == dstNode) continue;
                    if (isAncestor(movingNode, dstNode)) continue;
                    const int movingIdx = findFlatIndex(movingNode);
                    const int groupIdx = findFlatIndex(dstNode);
                    if (movingIdx < 0 || groupIdx < 0 || movingIdx == groupIdx)
                        continue;
                    m_controller->moveNodeIntoGroup(movingIdx, groupIdx);
                }
            } else {
                int targetIndex = dstIdx;
                if (bottomZone)
                    targetIndex = dstIdx + 1;
                else if (topZone)
                    targetIndex = dstIdx;
                reorderMany(targetIndex);
            }
        } else {
            reorderMany(doc->flatCount());
        }

        refresh();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void LayerPanel::updateTopBarFromActiveLayer()
{
    if (!m_controller) {
        m_opacitySlider->setValue(100);
        return;
    }

    auto* doc = m_controller->document();
    int active = doc->activeFlatIndex;
    if (active < 0 || active >= doc->flatCount()) {
        m_blendCombo->setCurrentIndex(0);
        m_opacitySlider->setValue(100);
        return;
    }

    auto* node = doc->nodeAt(active);
    if (!node) return;

    m_blendCombo->blockSignals(true);
    m_blendCombo->setCurrentIndex(blendModeToComboIndex(node->blendMode));
    m_blendCombo->blockSignals(false);

    int opacityPct = static_cast<int>(node->opacity * 100.0f);
    m_opacitySlider->blockSignals(true);
    m_opacitySlider->setValue(opacityPct);
    m_opacitySlider->blockSignals(false);

    // Lock buttons reflect the folded lock state of the selection. A partial
    // lock reads as checked whenever Lock All is set (it implies all three), and
    // the three partial buttons are disabled while the active layer is fully
    // locked so the master/partial relationship stays unambiguous. In a
    // multi-selection a button is checked only when *every* selected layer
    // satisfies it (mixed → unchecked).
    std::vector<int> sel = doc->selectedList();
    if (sel.empty()) sel.push_back(active);
    const bool activeFullyLocked = node->isFullyLocked();
    auto allSelected = [&](bool (LayerTreeNode::*pred)() const) {
        if (sel.empty()) return false;
        for (int idx : sel) {
            auto* n = doc->nodeAt(idx);
            if (!n || !(n->*pred)()) return false;
        }
        return true;
    };
    for (auto* btn : m_lockGroup->buttons()) {
        int flag = btn->property("lockFlag").toInt();
        bool checked = false;
        switch (flag) {
        case LockTransparent: checked = allSelected(&LayerTreeNode::isTransparencyLocked); break;
        case LockImage:       checked = allSelected(&LayerTreeNode::isPixelEditingLocked); break;
        case LockPosition:    checked = allSelected(&LayerTreeNode::isPositionLocked); break;
        case LockAll:         checked = allSelected(&LayerTreeNode::isFullyLocked); break;
        default: break;
        }
        btn->blockSignals(true);
        btn->setChecked(checked);
        btn->setEnabled(flag == LockAll ? true : !activeFullyLocked);
        btn->blockSignals(false);
    }
}
