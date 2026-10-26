#include "ChannelsPanel.hpp"

#include "ChannelItemDelegate.hpp"
#include "IconUtils.hpp"
#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "renderer/CanvasView.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppMessageDialog.hpp"
#include "ui/dialogs/ChannelDialogs.hpp"

#include <QApplication>
#include <QActionGroup>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kButtonSize = 24;
const char* kThumbSizeSetting = "channels/thumbnailSize";

const char* componentName(int component)
{
    switch (component) {
    case 0: return "Red";
    case 1: return "Green";
    default: return "Blue";
    }
}

// Grayscale view of one component, taken from the ALREADY DOWNSCALED
// composite so the per-pixel pass costs the thumbnail's area and not the
// document's. Equal weights, like the canvas channel view: the thumbnail has
// to show the component's values, not a luminance-weighted version of them.
QImage componentThumbnail(const QImage& compositeThumb, int component)
{
    if (compositeThumb.isNull())
        return {};
    const QImage rgba = compositeThumb.convertToFormat(QImage::Format_RGBA8888);
    QImage out(rgba.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* src = rgba.constScanLine(y);
        uchar* dst = out.scanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            // Matted against white, the same paper the rest of the export path
            // uses, so a transparent region does not read as solid black.
            const int a = src[x * 4 + 3];
            dst[x] = static_cast<uchar>(src[x * 4 + component] * a / 255 + (255 - a));
        }
    }
    return out;
}

// Longest edge a thumbnail needs, with headroom so the delegate's final
// scale-to-fit still has pixels to work with on a HiDPI screen.
int thumbnailSourceEdge(int boxPixels)
{
    return std::max(16, boxPixels * 2);
}

QImage downscaleForThumbnail(const QImage& source, int boxPixels)
{
    if (source.isNull() || boxPixels <= 0)
        return {};
    const int edge = thumbnailSourceEdge(boxPixels);
    if (source.width() <= edge && source.height() <= edge)
        return source;
    return source.scaled(edge, edge, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
}

} // namespace

ChannelsPanel::ChannelsPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyTheme();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() { applyTheme(); refresh(); });
}

void ChannelsPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(0);
    m_tree->setAnimated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setMouseTracking(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setFocusPolicy(Qt::StrongFocus);
    m_tree->viewport()->installEventFilter(this);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    setFocusProxy(m_tree);

    m_delegate = new ChannelItemDelegate(this);
    m_delegate->setThumbnailSize(
        QSettings().value(QLatin1String(kThumbSizeSetting), 1).toInt());
    m_tree->setItemDelegate(m_delegate);
    layout->addWidget(m_tree, 1);

    m_actionBar = new QWidget(this);
    auto* bar = new QHBoxLayout(m_actionBar);
    bar->setContentsMargins(4, 4, 4, 4);
    bar->setSpacing(4);

    auto addButton = [&](const QString& icon, const QString& tip) {
        auto* button = new QPushButton(m_actionBar);
        button->setIcon(makeIcon(icon));
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(kButtonSize, kButtonSize);
        button->setToolTip(tip);
        button->setFlat(true);
        bar->addWidget(button);
        return button;
    };

    bar->addStretch();
    m_loadSelectionBtn = addButton(QStringLiteral(":/icons/selection-new.png"),
                                   tr("Load channel as selection"));
    m_saveSelectionBtn = addButton(QStringLiteral(":/icons/layer-mask.png"),
                                   tr("Save selection as channel"));
    m_newChannelBtn = addButton(QStringLiteral(":/icons/ui-new.png"),
                                tr("Create new channel (Alt: options)"));
    m_deleteChannelBtn = addButton(QStringLiteral(":/icons/delete-layer.png"),
                                   tr("Delete channel"));
    m_optionsBtn = addButton(QStringLiteral(":/icons/ui-settings.png"),
                             tr("Panel options"));
    layout->addWidget(m_actionBar);

    connect(m_loadSelectionBtn, &QPushButton::clicked, this, [this]() {
        loadCurrentAsSelection(Qt::NoModifier);
    });
    connect(m_saveSelectionBtn, &QPushButton::clicked,
            this, &ChannelsPanel::saveSelectionAsChannel);
    connect(m_newChannelBtn, &QPushButton::clicked, this, [this]() {
        // Alt+click opens the options dialog, plain click creates the standard
        // empty (black) alpha channel.
        newAlphaChannel(QApplication::keyboardModifiers() & Qt::AltModifier);
    });
    connect(m_deleteChannelBtn, &QPushButton::clicked,
            this, &ChannelsPanel::deleteCurrentChannel);
    connect(m_optionsBtn, &QPushButton::clicked, this, [this]() {
        showPanelOptions(m_optionsBtn->mapToGlobal(
            QPoint(0, m_optionsBtn->height())));
    });

    connect(m_tree, &QWidget::customContextMenuRequested,
            this, &ChannelsPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_updating)
            return;
        updateActionBar();
    });
}

void ChannelsPanel::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(th->layerPanelStyleSheet());
    if (m_tree)
        m_tree->setStyleSheet(th->layerPanelStyleSheet());
}

void ChannelsPanel::setController(ImageController* controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &ImageController::channelsChanged,
                this, &ChannelsPanel::refresh);
        // Undo/redo and pixel edits both land here; the panel has to follow
        // because a channel may have come back, gone away or changed content.
        connect(m_controller, &ImageController::historyChanged,
                this, &ChannelsPanel::refresh);
        connect(m_controller, &ImageController::imageChanged,
                this, &ChannelsPanel::refresh);
        connect(m_controller, &ImageController::selectionChanged,
                this, &ChannelsPanel::updateActionBar);
    }
    refresh();
}

void ChannelsPanel::setCanvas(CanvasView* canvas)
{
    m_canvas = canvas;
}

QTreeWidgetItem* ChannelsPanel::itemAt(int row) const
{
    return (m_tree && row >= 0 && row < m_tree->topLevelItemCount())
        ? m_tree->topLevelItem(row) : nullptr;
}

ChannelsPanel::RowKind ChannelsPanel::rowKind(const QTreeWidgetItem* item) const
{
    if (!item)
        return RowKind::Composite;
    return static_cast<RowKind>(item->data(0, ChannelItemDelegate::RoleKind).toInt());
}

int ChannelsPanel::rowPayload(const QTreeWidgetItem* item) const
{
    return item ? item->data(0, ChannelItemDelegate::RolePayload).toInt() : -1;
}

int ChannelsPanel::selectedChannelIndex() const
{
    auto* item = m_tree ? m_tree->currentItem() : nullptr;
    if (!item || rowKind(item) != RowKind::Extra)
        return -1;
    return rowPayload(item);
}

void ChannelsPanel::refresh()
{
    if (m_updating)
        return;
    rebuildRows();
    updateActionBar();
}

void ChannelsPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // The canvas projection can become ready while this tab is hidden. Its
    // one-shot projectionUpdated signal has already passed by the time the user
    // opens Channels, so force the rows to re-read the cached projection now.
    // Thumbnail images themselves remain cached; only the row data is rebuilt.
    m_rowSignature.clear();
    refresh();
}

void ChannelsPanel::rebuildRows()
{
    if (!m_tree)
        return;
    auto* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc) {
        m_updating = true;
        m_tree->clear();
        m_rowSignature.clear();
        m_updating = false;
        return;
    }

    const ChannelState& st = doc->channelState;
    const int boxPixels = m_delegate->thumbnailPixels();
    const bool wantThumbs = boxPixels > 0;
    const int components = std::min(3, doc->colorChannelCount());

    // The composite behind the RGB and R/G/B rows, identified by its BUFFER
    // rather than by doc->compositionGeneration: it is composited off thread,
    // so at any moment the available projection may still be the previous
    // generation's. Keying on the generation tagged a thumbnail with pixels
    // that had not landed yet and froze it one edit behind until the next
    // composition change; the buffer's cacheKey changes exactly when there is
    // something new to draw. Zero while thumbnails are off, which also keeps
    // the rows from being rebuilt for composites nothing displays.
    const QImage composite = (wantThumbs && m_canvas) ? m_canvas->documentCompositeImage()
                                                      : QImage();
    const qint64 compositeKey = composite.isNull() ? 0 : composite.cacheKey();

    // Skip the rebuild when nothing on display moved. refresh() is wired to
    // imageChanged(), which fires on every painted dab, and recreating the
    // rows (and their thumbnails) each time was pure waste.
    QString signature;
    signature.reserve(128);
    signature += QString::number(boxPixels) + QLatin1Char('|')
               + QString::number(components) + QLatin1Char('|')
               + QString::number(compositeKey) + QLatin1Char('|')
               + QString::number(doc->channelViewGeneration) + QLatin1Char('|')
               + (st.compositeIsEditTarget() ? QLatin1Char('C') : QLatin1Char('-'));
    for (int c = 0; c < components; ++c) {
        signature += st.colorVisible[c] ? QLatin1Char('v') : QLatin1Char('-');
        signature += st.colorSelected[c] ? QLatin1Char('s') : QLatin1Char('-');
    }
    for (const auto& ch : doc->channels) {
        signature += QLatin1Char('|') + QString::number(ch.id) + QLatin1Char(':')
                   + ch.name + QLatin1Char(':')
                   + (ch.visible ? QLatin1Char('v') : QLatin1Char('-'))
                   + (st.isChannelSelected(ch.id) ? QLatin1Char('s') : QLatin1Char('-'))
                   + (ch.isSpot() ? QLatin1Char('S') : QLatin1Char('A'));
    }
    if (doc->quickMask.active)
        signature += QStringLiteral("|qm");
    if (signature == m_rowSignature && m_tree->topLevelItemCount() > 0)
        return;
    m_rowSignature = signature;

    // Composite + component thumbnails, derived once per composite buffer.
    if (wantThumbs) {
        if (composite.isNull()) {
            m_compositeThumb = QImage();
            for (auto& thumb : m_componentThumbs)
                thumb = QImage();
            m_compositeThumbKey = 0;
        } else if (m_compositeThumbKey != compositeKey
                   || m_compositeThumbSize != boxPixels
                   || m_compositeThumb.isNull()) {
            m_compositeThumb = downscaleForThumbnail(composite, boxPixels);
            for (int c = 0; c < 3; ++c)
                m_componentThumbs[c] = componentThumbnail(m_compositeThumb, c);
            m_compositeThumbKey = compositeKey;
            m_compositeThumbSize = boxPixels;
        }
    }

    m_updating = true;

    // Rows are REUSED whenever the structure still matches, and only recreated
    // when it really changed. refresh() can run from inside a mouse press (a
    // click arms a channel, which emits channelsChanged), and deleting the
    // items the view is still handling that press against would lose the drag
    // it was about to start.
    int row = 0;
    const bool structureMatches = [&]() {
        int expected = (components == 3 ? 1 : 0) + components
                     + static_cast<int>(doc->channels.size())
                     + (doc->quickMask.active ? 1 : 0);
        return m_tree->topLevelItemCount() == expected;
    }();
    if (!structureMatches)
        m_tree->clear();

    auto addRow = [&](RowKind kind, int payload, const QString& label,
                      bool visible, bool armed, const QImage& thumb,
                      const QColor& badge) {
        QTreeWidgetItem* item = structureMatches ? m_tree->topLevelItem(row)
                                                 : new QTreeWidgetItem(m_tree);
        ++row;
        if (!item)
            return item;
        item->setText(0, label);
        item->setData(0, ChannelItemDelegate::RoleKind, static_cast<int>(kind));
        item->setData(0, ChannelItemDelegate::RolePayload, payload);
        item->setData(0, ChannelItemDelegate::RoleVisible, visible);
        item->setData(0, ChannelItemDelegate::RoleArmed, armed);
        item->setData(0, ChannelItemDelegate::RoleBadgeColor,
                      badge.isValid() ? QVariant(badge) : QVariant());
        item->setData(0, ChannelItemDelegate::RoleThumbnail,
                      (wantThumbs && !thumb.isNull()) ? QVariant(thumb) : QVariant());
        // Only the extra channels reorder; the colour components are fixed.
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (kind == RowKind::Extra)
            flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        item->setFlags(flags);
        return item;
    };

    // RGB composite: the edit and view of the components together, not a
    // fourth buffer.
    if (components == 3) {
        addRow(RowKind::Composite, -1, tr("RGB"),
               st.allColorVisible(), st.compositeIsEditTarget(),
               m_compositeThumb, QColor());
    }

    for (int c = 0; c < components; ++c) {
        // A component row is armed only when it is an individual edit target.
        // With the composite armed all three are selected, but the RGB row is
        // the one that represents that state.
        const bool armed = st.colorSelected[c] && !st.compositeIsEditTarget();
        addRow(RowKind::Component, c, tr(componentName(c)),
               st.colorVisible[c], armed, m_componentThumbs[c], QColor());
    }

    for (int i = 0; i < static_cast<int>(doc->channels.size()); ++i) {
        const DocumentChannel& ch = doc->channels[static_cast<size_t>(i)];
        if (wantThumbs && (ch.thumbDirty || ch.thumbCache.isNull()
                           || ch.thumbCacheBox != boxPixels)) {
            ch.thumbCache = downscaleForThumbnail(ch.data, boxPixels);
            ch.thumbCacheBox = boxPixels;
            ch.thumbDirty = false;
        }
        addRow(RowKind::Extra, i, ch.name, ch.visible,
               st.isChannelSelected(ch.id),
               wantThumbs ? ch.thumbCache : QImage(),
               ch.isSpot() ? ch.inkColor : ch.overlayColor);
    }

    // Quick Mask appears as a temporary channel while the mode is on, and is
    // never written to Document::channels: leaving the mode turns it back into
    // a selection, and keeping it takes an explicit Save Selection.
    if (doc->quickMask.active) {
        // const view of the selection: the mutating accessor would drop
        // SelectionMask's cached isEmpty()/bounds on every panel refresh.
        const Document* constDoc = doc;
        addRow(RowKind::QuickMask, -1, tr("Quick Mask"), true, true,
               wantThumbs ? downscaleForThumbnail(constDoc->selection.image(), boxPixels)
                          : QImage(),
               doc->quickMask.overlayColor);
    }

    // Reflect the edit target as the current row, so keyboard navigation and
    // the context menu agree with what the tools write to.
    for (int row = 0; row < m_tree->topLevelItemCount(); ++row) {
        auto* item = m_tree->topLevelItem(row);
        if (item->data(0, ChannelItemDelegate::RoleArmed).toBool()) {
            m_tree->setCurrentItem(item);
            break;
        }
    }

    m_updating = false;
}

void ChannelsPanel::updateActionBar()
{
    auto* doc = m_controller ? m_controller->document() : nullptr;
    const bool hasDoc = doc != nullptr;
    const bool hasSelection = hasDoc && doc->selection.active()
                           && !doc->selection.isEmpty();
    auto* item = m_tree ? m_tree->currentItem() : nullptr;
    const bool isExtra = item && rowKind(item) == RowKind::Extra;

    if (m_loadSelectionBtn) {
        // Any channel can become a selection — a colour component through its
        // intensity, an extra channel through its coverage.
        m_loadSelectionBtn->setEnabled(hasDoc && item
                                       && rowKind(item) != RowKind::Composite);
    }
    if (m_saveSelectionBtn)
        m_saveSelectionBtn->setEnabled(hasSelection && doc->canAddChannel());
    if (m_newChannelBtn)
        m_newChannelBtn->setEnabled(hasDoc && doc->canAddChannel());
    if (m_deleteChannelBtn)
        m_deleteChannelBtn->setEnabled(isExtra);
}

// ── Row interaction ────────────────────────────────────────────

bool ChannelsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_tree || watched != m_tree->viewport())
        return QWidget::eventFilter(watched, event);

    const auto kind = [this](QTreeWidgetItem* item) { return rowKind(item); };
    Q_UNUSED(kind);

    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        auto* item = m_tree->itemAt(mouse->pos());
        if (!item)
            return QWidget::eventFilter(watched, event);

        QStyleOptionViewItem option;
        option.rect = m_tree->visualItemRect(item);

        if (m_delegate->eyeRect(option).contains(mouse->pos())) {
            if (event->type() == QEvent::MouseButtonPress)
                onEyeToggled(item);
            return true;
        }

        const QRect thumb = m_delegate->thumbRect(option);
        if (!thumb.isEmpty() && thumb.contains(mouse->pos())) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                onThumbnailDoubleClicked(item);
                return true;
            }
            if (mouse->modifiers() & Qt::ControlModifier) {
                // Ctrl+click on the thumbnail loads the channel as a selection
                // whatever is armed for editing — the shortcut is about the
                // channel under the cursor, not the edit target.
                onThumbnailActivated(item, mouse->modifiers());
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonDblClick) {
            if (rowKind(item) == RowKind::Extra) {
                m_tree->setCurrentItem(item);
                renameCurrentChannel();
            }
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress
            && mouse->button() == Qt::LeftButton) {
            onRowClicked(item, mouse->modifiers());
            // Fall through so the view still handles the press for drag start.
        }
    }

    if (event->type() == QEvent::Drop) {
        // Reordering is applied by index through the controller, so it is one
        // undoable step and the spot overprint order follows the panel.
        auto* drop = static_cast<QDropEvent*>(event);
        auto* source = m_tree->currentItem();
        auto* target = m_tree->itemAt(drop->position().toPoint());
        if (!source || rowKind(source) != RowKind::Extra) {
            return true;  // colour components never move
        }
        if (!target || rowKind(target) != RowKind::Extra) {
            return true;
        }
        const int from = rowPayload(source);
        const int to = rowPayload(target);
        if (m_controller && from >= 0 && to >= 0 && from != to)
            m_controller->reorderChannel(from, to);
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void ChannelsPanel::onRowClicked(QTreeWidgetItem* item, Qt::KeyboardModifiers mods)
{
    if (!m_controller || !item)
        return;
    const bool additive = mods & Qt::ShiftModifier;
    switch (rowKind(item)) {
    case RowKind::Composite:
        m_controller->selectCompositeChannel();
        break;
    case RowKind::Component:
        m_controller->selectColorComponent(rowPayload(item), additive);
        break;
    case RowKind::Extra:
        m_controller->selectChannelAt(rowPayload(item), additive);
        break;
    case RowKind::QuickMask:
        break;  // the temporary channel is always the target while the mode is on
    }
}

void ChannelsPanel::onEyeToggled(QTreeWidgetItem* item)
{
    if (!m_controller || !item)
        return;
    auto* doc = m_controller->document();
    if (!doc)
        return;
    switch (rowKind(item)) {
    case RowKind::Composite: {
        // The RGB row's eye drives all three components at once.
        const bool show = !doc->channelState.allColorVisible();
        for (int c = 0; c < 3; ++c)
            m_controller->setColorChannelVisible(c, show);
        break;
    }
    case RowKind::Component: {
        const int c = rowPayload(item);
        m_controller->setColorChannelVisible(c, !doc->channelState.colorVisible[c]);
        break;
    }
    case RowKind::Extra: {
        const int index = rowPayload(item);
        if (index >= 0 && index < static_cast<int>(doc->channels.size())) {
            m_controller->setChannelVisible(
                index, !doc->channels[static_cast<size_t>(index)].visible);
        }
        break;
    }
    case RowKind::QuickMask:
        break;
    }
}

void ChannelsPanel::onThumbnailActivated(QTreeWidgetItem* item,
                                         Qt::KeyboardModifiers mods)
{
    loadCurrentAsSelectionFor(item, mods);
}

void ChannelsPanel::onThumbnailDoubleClicked(QTreeWidgetItem* item)
{
    if (rowKind(item) == RowKind::Extra)
        openChannelOptions(rowPayload(item));
}

// ── Commands ───────────────────────────────────────────────────

void ChannelsPanel::newAlphaChannel(bool withDialog)
{
    if (!m_controller || !m_controller->document())
        return;
    auto* doc = m_controller->document();
    const QString suggested = doc->uniqueChannelName(ChannelKind::Alpha);
    if (!withDialog) {
        m_controller->newAlphaChannel(suggested,
                                      ChannelMaskRepresentation::MaskedAreas,
                                      QColor(255, 0, 0), 0.5f);
        return;
    }
    if (m_channelDlg) {
        m_channelDlg->raise();
        m_channelDlg->activateWindow();
        return;
    }
    // Modeless (see m_channelDlg): the channel is created on OK, against
    // whatever document is active then.
    auto* dialog = new AlphaChannelDialog(tr("New Channel"), suggested,
                                          ChannelMaskRepresentation::MaskedAreas,
                                          QColor(255, 0, 0), 0.5f, false, this);
    m_channelDlg = dialog;
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Tool);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        if (!m_controller || !m_controller->document())
            return;
        m_controller->newAlphaChannel(dialog->channelName(), dialog->representation(),
                                      dialog->overlayColor(), dialog->overlayOpacity());
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void ChannelsPanel::newSpotChannel()
{
    if (!m_controller || !m_controller->document())
        return;
    auto* doc = m_controller->document();
    if (m_channelDlg) {
        m_channelDlg->raise();
        m_channelDlg->activateWindow();
        return;
    }
    auto* dialog = new SpotChannelDialog(tr("New Spot Channel"),
                                         doc->uniqueChannelName(ChannelKind::Spot),
                                         QColor(255, 0, 255), 0.0f, QString(), false, this);
    m_channelDlg = dialog;
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Tool);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        if (!m_controller || !m_controller->document())
            return;
        m_controller->newSpotChannel(dialog->channelName(), dialog->inkColor(),
                                     dialog->solidity(), dialog->inkBook());
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void ChannelsPanel::duplicateCurrentChannel()
{
    const int index = selectedChannelIndex();
    if (index < 0)
        return;
    // MainWindow owns the list of open documents and the "new document" case,
    // so it runs the dialog; a same-document duplicate still ends up in
    // ImageController::duplicateChannel either way.
    emit duplicateChannelRequested(index);
}

void ChannelsPanel::deleteCurrentChannel()
{
    const int index = selectedChannelIndex();
    if (index < 0 || !m_controller)
        return;
    auto* doc = m_controller->document();
    if (!doc)
        return;
    const QString name = doc->channels[static_cast<size_t>(index)].name;
    if (!AppMessageDialog::confirm(
            this, tr("Delete Channel"),
            tr("Delete the channel \"%1\"? This affects no other channel and no "
               "layer.").arg(name))) {
        return;
    }
    m_controller->deleteChannelAt(index);
}

void ChannelsPanel::openChannelOptions(int channelIndex)
{
    if (!m_controller)
        return;
    auto* doc = m_controller->document();
    if (!doc || channelIndex < 0
        || channelIndex >= static_cast<int>(doc->channels.size()))
        return;
    const DocumentChannel& ch = doc->channels[static_cast<size_t>(channelIndex)];

    if (m_channelDlg) {
        m_channelDlg->raise();
        m_channelDlg->activateWindow();
        return;
    }

    // Modeless (see m_channelDlg). The settings are captured from the channel on
    // OK rather than now: the dialog no longer blocks the document, so the
    // channel may have moved or changed while it was open.
    QDialog* dialog = nullptr;
    if (ch.isSpot()) {
        auto* spot = new SpotChannelDialog(tr("Spot Channel Options"), ch.name,
                                           ch.inkColor, ch.solidity, ch.inkBook,
                                           true, this);
        dialog = spot;
        connect(spot, &QDialog::accepted, this, [this, spot, channelIndex]() {
            ChannelSettings settings;
            if (!captureChannelSettings(channelIndex, settings))
                return;
            settings.name = spot->channelName();
            settings.inkColor = spot->inkColor();
            settings.solidity = spot->solidity();
            settings.inkBook = spot->inkBook();
            if (spot->convertToAlpha())
                settings.kind = ChannelKind::Alpha;
            m_controller->setChannelSettings(channelIndex, settings);
        });
    } else {
        auto* alpha = new AlphaChannelDialog(tr("Channel Options"), ch.name,
                                              ch.representation, ch.overlayColor,
                                              ch.overlayOpacity, true, this);
        dialog = alpha;
        connect(alpha, &QDialog::accepted, this, [this, alpha, channelIndex]() {
            ChannelSettings settings;
            if (!captureChannelSettings(channelIndex, settings))
                return;
            settings.name = alpha->channelName();
            settings.representation = alpha->representation();
            settings.overlayColor = alpha->overlayColor();
            settings.overlayOpacity = alpha->overlayOpacity();
            if (alpha->convertToSpot()) {
                settings.kind = ChannelKind::Spot;
                // An alpha has no ink colour of its own; seed it from the overlay
                // so the converted spot previews with the colour the user saw.
                if (!settings.inkColor.isValid())
                    settings.inkColor = alpha->overlayColor();
            }
            m_controller->setChannelSettings(channelIndex, settings);
        });
    }

    m_channelDlg = dialog;
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Tool);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

// Re-reads the channel at `channelIndex` into `out`. Returns false when it is
// gone — the channel dialogs are modeless, so the document can change under one.
bool ChannelsPanel::captureChannelSettings(int channelIndex, ChannelSettings& out) const
{
    auto* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc || channelIndex < 0
        || channelIndex >= static_cast<int>(doc->channels.size()))
        return false;
    out = ChannelSettings::capture(doc->channels[static_cast<size_t>(channelIndex)]);
    return true;
}

void ChannelsPanel::renameCurrentChannel()
{
    const int index = selectedChannelIndex();
    if (index < 0 || !m_controller)
        return;
    auto* doc = m_controller->document();
    if (!doc)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename Channel"), tr("Channel name:"), QLineEdit::Normal,
        doc->channels[static_cast<size_t>(index)].name, &ok);
    if (ok && !name.trimmed().isEmpty())
        m_controller->renameChannel(index, name.trimmed());
}

void ChannelsPanel::mergeCurrentSpotChannel()
{
    const int index = selectedChannelIndex();
    if (index < 0 || !m_controller)
        return;
    auto* doc = m_controller->document();
    if (!doc || !doc->channels[static_cast<size_t>(index)].isSpot())
        return;
    // Destructive and not reversible except through the history: the ink has to
    // land in real colour pixels, which means flattening the layer stack.
    if (!AppMessageDialog::confirm(
            this, tr("Merge Spot Channel"),
            tr("Merging the spot channel bakes its ink into the colour channels "
               "and flattens the image. Continue?"))) {
        return;
    }
    m_controller->mergeSpotChannel(index);
}

void ChannelsPanel::saveSelectionAsChannel()
{
    if (m_controller)
        m_controller->saveSelectionAsChannel();
}

void ChannelsPanel::loadCurrentAsSelection(Qt::KeyboardModifiers mods)
{
    loadCurrentAsSelectionFor(m_tree ? m_tree->currentItem() : nullptr, mods);
}

void ChannelsPanel::loadCurrentAsSelectionFor(QTreeWidgetItem* item,
                                              Qt::KeyboardModifiers mods)
{
    if (!m_controller || !item)
        return;
    // Ctrl+click replaces, +Shift adds, +Alt subtracts, +Alt+Shift intersects —
    // the same modifier set the rest of the app uses for selection combining.
    SelectMode mode = SelectMode::Replace;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    if (alt && shift) mode = SelectMode::Intersect;
    else if (alt)     mode = SelectMode::Subtract;
    else if (shift)   mode = SelectMode::Add;

    switch (rowKind(item)) {
    case RowKind::Component:
        m_controller->loadColorComponentAsSelection(rowPayload(item), mode, false);
        break;
    case RowKind::Extra:
        m_controller->loadChannelAsSelection(rowPayload(item), mode, false);
        break;
    case RowKind::Composite:
    case RowKind::QuickMask:
        break;
    }
}

// ── Menus ──────────────────────────────────────────────────────

void ChannelsPanel::showContextMenu(const QPoint& pos)
{
    if (!m_controller || !m_tree)
        return;
    auto* doc = m_controller->document();
    if (!doc)
        return;

    auto* item = m_tree->itemAt(pos);
    if (item)
        m_tree->setCurrentItem(item);
    const RowKind kind = rowKind(item);
    const int channelIndex = kind == RowKind::Extra ? rowPayload(item) : -1;
    const bool isSpot = channelIndex >= 0
        && doc->channels[static_cast<size_t>(channelIndex)].isSpot();

    QMenu menu(this);
    menu.addAction(tr("New Channel..."), this, [this]() { newAlphaChannel(true); });
    menu.addAction(tr("New Spot Channel..."), this, &ChannelsPanel::newSpotChannel);
    menu.addSeparator();

    // Only the entries that can actually run are offered: a command that looks
    // available and then does nothing is worse than no command.
    auto* duplicate = menu.addAction(tr("Duplicate Channel..."), this,
                                     &ChannelsPanel::duplicateCurrentChannel);
    duplicate->setEnabled(channelIndex >= 0);
    auto* remove = menu.addAction(tr("Delete Channel"), this,
                                  &ChannelsPanel::deleteCurrentChannel);
    remove->setEnabled(channelIndex >= 0);
    menu.addSeparator();

    auto* options = menu.addAction(tr("Channel Options..."), this,
                                   [this, channelIndex]() {
        openChannelOptions(channelIndex);
    });
    options->setEnabled(channelIndex >= 0);
    auto* rename = menu.addAction(tr("Rename Channel..."), this,
                                  &ChannelsPanel::renameCurrentChannel);
    rename->setEnabled(channelIndex >= 0);
    menu.addSeparator();

    auto* mergeSpot = menu.addAction(tr("Merge Spot Channel"), this,
                                     &ChannelsPanel::mergeCurrentSpotChannel);
    mergeSpot->setEnabled(isSpot);
    menu.addSeparator();

    auto* split = menu.addAction(tr("Split Channels"), this, [this]() {
        emit splitChannelsRequested();
    });
    // Splitting consumes a flattened image, so it needs a document with pixels
    // and no extra channels in the way.
    split->setEnabled(doc->flatCount() > 0 && doc->channels.empty());
    menu.addAction(tr("Merge Channels..."), this, [this]() {
        emit mergeChannelsRequested();
    });
    menu.addSeparator();

    menu.addAction(tr("Save Selection..."), this, [this]() {
        emit saveSelectionDialogRequested();
    });
    menu.addAction(tr("Load Selection..."), this, [this]() {
        emit loadSelectionDialogRequested();
    });
    menu.addSeparator();
    menu.addAction(tr("Panel Options..."), this, [this]() {
        showPanelOptions(QCursor::pos());
    });

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ChannelsPanel::showPanelOptions(const QPoint& globalPos)
{
    if (!m_controller)
        return;
    auto* doc = m_controller->document();

    QMenu menu(this);
    auto* sizeMenu = menu.addMenu(tr("Thumbnail Size"));
    auto* group = new QActionGroup(sizeMenu);
    group->setExclusive(true);
    const QStringList labels{tr("None"), tr("Small"), tr("Medium"), tr("Large")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* action = sizeMenu->addAction(labels[i]);
        action->setCheckable(true);
        action->setChecked(m_delegate->thumbnailSize() == i);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, i]() {
            m_delegate->setThumbnailSize(i);
            QSettings().setValue(QLatin1String(kThumbSizeSetting), i);
            refresh();
        });
    }

    auto* inColor = menu.addAction(tr("Show Channels in Color"));
    inColor->setCheckable(true);
    inColor->setChecked(doc && doc->channelState.showChannelsInColor);
    inColor->setEnabled(doc != nullptr);
    connect(inColor, &QAction::triggered, this, [this](bool on) {
        if (m_controller)
            m_controller->setShowChannelsInColor(on);
    });

    menu.exec(globalPos);
}
