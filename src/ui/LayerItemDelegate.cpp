#include "LayerItemDelegate.hpp"
#include "controller/ImageController.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/Document.hpp"
#include "core/SolidColorData.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QEvent>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QApplication>
#include <QLineEdit>

#include <algorithm>

static constexpr int kItemHeight = 50;
static constexpr int kThumbSize = 40;
static constexpr int kMaskThumbSize = 40;
static constexpr int kTextThumbIconSize = 24;
static constexpr int kEyeSize = 20;
static constexpr int kLockIconSize = 20;
static constexpr int kLockBtnSize = 24;
static constexpr int kFxSize = 22;
static constexpr int kGroupIconSize = 24;
static constexpr int kGroupBtnSize = 26;
static constexpr int kMargin = 6;
static constexpr int kIndent = 16;
static constexpr int kGap = 4;
static constexpr int kArrowSize = 12;
static constexpr int kThumbToLabelPadding = 16;
static constexpr int kRightLockPadding = 5;
static constexpr int kDraggingRole = Qt::UserRole + 200;

static bool imageHasVisiblePixels(const QImage& image)
{
    if (image.isNull()) return false;
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            if (row[x * 4 + 3] != 0)
                return true;
        }
    }
    return false;
}

LayerItemDelegate::LayerItemDelegate(ImageController* controller, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_controller(controller)
{
}

void LayerItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    auto* node = nodeFromIndex(index);
    if (!node) return;

    auto* th = ThemeManager::instance()->current();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (index.data(kDraggingRole).toBool()) {
        painter->setOpacity(0.5);
    }

    QRect r = option.rect;

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(r, th->colorSurfacePressed);
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(r, th->colorSurfaceHover);
    } else {
        painter->fillRect(r, th->colorSurfaceRow);
    }

    int extraIndent = childIndent(index);
    if (extraIndent > 0)
        painter->fillRect(r.left(), r.top(), extraIndent, r.height(), th->colorPanelBackground);
    int x = r.left() + extraIndent;

    QFont iconFont = painter->font();
    iconFont.setPixelSize(th->fontSizeMD);
    painter->setFont(iconFont);

    // --- Eye (visibility) ---
    x += visibilityEyePadding();
    {
        static QPixmap visiblePix(":/icons/layer-visible.png");
        static QPixmap hiddenPix(":/icons/layer-invisible.png");
        QRect eyeRect = this->eyeRect(option, index);
        QPixmap pix = node->visible ? visiblePix : hiddenPix;
        painter->save();
        painter->setOpacity(th->iconOpacity);
        painter->drawPixmap(eyeRect, pix);
        painter->restore();

        const int visGap = visibilitySeparatorGap();
        const int sepX = eyeRect.right() + visGap + 1;
        painter->save();
        painter->setPen(QPen(th->colorBorder, 1));
        painter->drawLine(sepX, r.top(), sepX, r.bottom());
        painter->restore();

        x = contentStartAfterVisibilitySeparator(option, index);
    }

    // --- Layer Thumbnail ---
    int thisFlatIndex = index.data(Qt::UserRole).toInt();
    bool isActiveLayer = m_controller && (m_controller->activeLayerIndex() == thisFlatIndex);
    bool editingMask = m_controller && m_controller->isEditingMask() && isActiveLayer;
    bool hasMask = node->layer && !node->layer->maskImage.isNull();

    // Expand/collapse arrow for a layer that hosts Single-Layer-Mode
    // adjustment children (groups draw their own arrow below).
    if (node->type == LayerTreeNode::Type::Layer
        && node->hasVisibleAdjustmentChildren()) {
        QRect arrowR(x, r.center().y() - kArrowSize / 2, kArrowSize, kArrowSize);
        painter->setPen(th->colorTextPrimary);
        QFont arrowFont = painter->font();
        arrowFont.setPixelSize(10);
        painter->setFont(arrowFont);
        painter->drawText(arrowR, Qt::AlignCenter, node->collapsed
            ? QString::fromUtf8("\xe2\x96\xb6") : QString::fromUtf8("\xe2\x96\xbc"));
        x += kArrowSize + kGap;
    }

    QRect thumbRect(x, r.center().y() - kThumbSize / 2, kThumbSize, kThumbSize);
    QRect innerThumb = thumbRect.adjusted(2, 2, -2, -2);
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (node->type == LayerTreeNode::Type::Group) {
        static QPixmap groupClosedPix(":/icons/layer-group-folder-close.png");
        static QPixmap groupOpenPix(":/icons/layer-group-folder-open.png");
        QRect arrowR = expandRect(option, index);
        painter->setPen(th->colorTextPrimary);
        QFont arrowFont = painter->font();
        arrowFont.setPixelSize(10);
        painter->setFont(arrowFont);
        if (node->collapsed)
            painter->drawText(arrowR, Qt::AlignCenter, QString::fromUtf8("\xe2\x96\xb6"));
        else
            painter->drawText(arrowR, Qt::AlignCenter, QString::fromUtf8("\xe2\x96\xbc"));

        QRect groupBtnRect = groupIconRect(option, index);
        QRect groupPixmapRect = groupBtnRect.adjusted(
            (kGroupBtnSize - kGroupIconSize) / 2,
            (kGroupBtnSize - kGroupIconSize) / 2,
            -(kGroupBtnSize - kGroupIconSize) / 2,
            -(kGroupBtnSize - kGroupIconSize) / 2);
        painter->save();
        painter->setOpacity(th->iconOpacity);
        painter->drawPixmap(groupPixmapRect, node->collapsed ? groupClosedPix : groupOpenPix);
        painter->restore();
        x = groupBtnRect.right() + 1;
    } else {
        bool hasVisiblePixels = false;
        if (node->type == LayerTreeNode::Type::Adjustment) {
            static QPixmap adjPix(":/icons/layer-adjustments.png");
            static QPixmap curvesPix(":/icons/curves-layer.png");
            static QPixmap colorBalancePix(":/icons/color-balance-adjustment.png");
            static QPixmap hueSatPix(":/icons/hue-saturation-layer.png");
            const QString adjType = node->adjustment ? node->adjustment->type : QString();

            // Solid Color shows the fill colour itself (checkerboard behind it
            // only where the colour is translucent) rather than a type icon.
            if (adjType == QLatin1String("solidcolor")) {
                const QColor c =
                    solidcolor::SolidColorData::fromParams(node->adjustment->params).color;
                if (c.alpha() < 255) {
                    const int cell = 8;
                    const QColor a1(200, 200, 200), a2(150, 150, 150);
                    for (int yy = 0; yy * cell < innerThumb.height(); ++yy)
                        for (int xx = 0; xx * cell < innerThumb.width(); ++xx)
                            painter->fillRect(innerThumb.left() + xx * cell,
                                              innerThumb.top() + yy * cell, cell, cell,
                                              ((xx + yy) & 1) ? a1 : a2);
                }
                painter->fillRect(innerThumb, c);
                painter->setPen(QPen(QColor(QStringLiteral("#ffffff")), 0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));
                hasVisiblePixels = true;
            } else {
            const QPixmap* typePix = &adjPix;
            if (adjType == QLatin1String("curves"))
                typePix = &curvesPix;
            else if (adjType == QLatin1String("colorbalance"))
                typePix = &colorBalancePix;
            else if (adjType == QLatin1String("huesaturation"))
                typePix = &hueSatPix;
            painter->fillRect(innerThumb, th->colorBackgroundSecondary);
            const QRect adjIconRect(
                thumbRect.center().x() - kTextThumbIconSize / 2,
                thumbRect.center().y() - kTextThumbIconSize / 2,
                kTextThumbIconSize, kTextThumbIconSize);
            painter->save();
            painter->setOpacity(th->iconOpacity);
            painter->drawPixmap(adjIconRect, *typePix);
            painter->restore();
            painter->setPen(QPen(QColor(QStringLiteral("#ffffff")), 0));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));
            hasVisiblePixels = true;
            }
        } else if (node->layer) {
            if (node->layer->isTextLayer()) {
                static QPixmap textPix(":/icons/text.png");
                painter->fillRect(innerThumb, th->colorBackgroundSecondary);
                const QRect textIconRect(
                    thumbRect.center().x() - kTextThumbIconSize / 2,
                    thumbRect.center().y() - kTextThumbIconSize / 2,
                    kTextThumbIconSize,
                    kTextThumbIconSize);
                painter->save();
                painter->setOpacity(th->iconOpacity);
                painter->drawPixmap(textIconRect, textPix);
                painter->restore();
                painter->setPen(QPen(QColor(QStringLiteral("#ffffff")), 0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));
                hasVisiblePixels = true;
            } else {
                // Thumbnails are ALWAYS baked off the UI thread by LayerPanel
                // (scheduleThumbnailBuild): scaling a layer image here would freeze
                // the panel on a large/merged layer. The delegate only ever draws
                // the last async result — a placeholder shows until the worker
                // lands the first bake (dirty nodes were queued on the last
                // refresh, so this is momentary).
                if (!node->cachedThumb.isNull()) {
                    painter->drawPixmap(innerThumb, QPixmap::fromImage(node->cachedThumb));
                    hasVisiblePixels = imageHasVisiblePixels(node->cachedThumb);
                } else {
                    painter->fillRect(innerThumb, th->colorSurface);
                }
            }
        } else {
            painter->fillRect(innerThumb, th->colorSurface);
        }

        if (node->type != LayerTreeNode::Type::Adjustment
            && !(node->layer && node->layer->isTextLayer())) {
            painter->setPen(QPen(QColor(QStringLiteral("#ffffff")), 0));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));
        }

        // Active target border for layer thumbnail
        if (editingMask || hasMask) {
            QColor borderColor = editingMask ? QColor(QStringLiteral("#ffffff")) : th->colorAccent;
            painter->setPen(QPen(borderColor, 0));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));
        }

        x += kThumbSize + kGap;

        // --- Mask Thumbnail (real image, next to layer thumbnail) ---
        if (hasMask) {
            QRect maskRect(x, r.center().y() - kMaskThumbSize / 2, kMaskThumbSize, kMaskThumbSize);
            QRect innerMask = maskRect.adjusted(2, 2, -2, -2);
            if (node->layer->maskThumbDirty || node->layer->cachedMaskThumb.isNull()) {
                node->layer->cachedMaskThumb = node->layer->maskImage.scaled(
                    kMaskThumbSize, kMaskThumbSize, Qt::KeepAspectRatio, Qt::FastTransformation);
                node->layer->maskThumbDirty = false;
            }
            painter->drawPixmap(innerMask, QPixmap::fromImage(node->layer->cachedMaskThumb));

            if (editingMask) {
                painter->setPen(QPen(th->colorWarning, 0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(maskRect.adjusted(0, 0, -1, -1));
            } else if (hasMask) {
                painter->setPen(QPen(QColor(QStringLiteral("#ffffff")), 0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(maskRect.adjusted(0, 0, -1, -1));
            }
            x += kMaskThumbSize + kGap;
        }
    }

    x += kThumbToLabelPadding;

    // --- Indicators (right side, laid out right-to-left) ---
    QRect lockR = lockRect(option, extraIndent);
    QRect fxR = layerStylesRect(option, extraIndent);
    int rightX = fxR.left() - kGap;

    QString typeStr;
    switch (node->type) {
    case LayerTreeNode::Type::Group:       typeStr = ""; break;
    case LayerTreeNode::Type::Adjustment:  typeStr = "\xe2\x97\x90"; break;
    case LayerTreeNode::Type::Effect:      typeStr = "fx"; break;
    default: break;
    }
    if (!typeStr.isEmpty()) {
        QRect badgeRect(rightX - 22, r.center().y() - 8, 20, 16);
        painter->setPen(th->colorTextDisabled);
        QFont subFont = painter->font();
        subFont.setPixelSize(th->fontSizeXS);
        painter->setFont(subFont);
        painter->drawText(badgeRect, Qt::AlignCenter, typeStr);
        rightX -= 26;
    }

    if (node->type == LayerTreeNode::Type::Layer && node->layer) {
        const bool activeFx = std::any_of(node->effects.begin(), node->effects.end(),
            [](const LayerEffect& e) { return e.enabled; });
        QColor fill = activeFx ? th->colorAccent : th->colorSurfaceHover;
        QColor pen = activeFx ? th->colorAccent : th->colorBorder;
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(pen, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(fxR.adjusted(0, 0, -1, -1), 3, 3);
        QFont fxFont = painter->font();
        fxFont.setPixelSize(14);
        fxFont.setBold(true);
        painter->setFont(fxFont);
        painter->setPen(activeFx ? Qt::white : th->colorTextSecondary);
        painter->drawText(fxR, Qt::AlignCenter, QStringLiteral("fx"));
    }

    // --- Lock (right aligned) ---
    {
        static QPixmap lockPix(":/icons/lock.png");
        static QPixmap lockOpenPix(":/icons/lock-open.png");
        auto& pix = (node->lockFlags != LockNone) ? lockPix : lockOpenPix;
        const QRect lockBtnRect = lockRect(option, extraIndent);
        const QRect lockIconRect = lockBtnRect.adjusted(
            (kLockBtnSize - kLockIconSize) / 2,
            (kLockBtnSize - kLockIconSize) / 2,
            -(kLockBtnSize - kLockIconSize) / 2,
            -(kLockBtnSize - kLockIconSize) / 2);
        painter->save();
        painter->setOpacity(th->iconOpacity);
        painter->drawPixmap(lockIconRect, pix);
        painter->restore();
    }

    // --- Name (fills space between thumbnails and right indicators) ---
    QFont nameFont = painter->font();
    nameFont.setPixelSize(th->fontSizeMD);
    nameFont.setBold(false);
    painter->setFont(nameFont);
    painter->setPen(th->colorTextPrimary);

    QRect nameRect = labelRect(option, index);
    QString displayName = node->name.isEmpty()
        ? QString("Layer %1").arg(index.data(Qt::UserRole).toInt() + 1)
        : node->name;
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(displayName, Qt::ElideRight, nameRect.width()));

    // --- Bottom separator ---
    QPen borderPen(th->colorPanelBackground);
    borderPen.setWidth(1);
    painter->setPen(borderPen);
    painter->drawLine(r.bottomLeft(), r.bottomRight());

    painter->restore();
}

QSize LayerItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(0, kItemHeight);
}

bool LayerItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);

        auto* node = nodeFromIndex(index);
        // Layer hosting adjustment children: toggle expand via its arrow.
        if (node && node->type == LayerTreeNode::Type::Layer
            && node->hasVisibleAdjustmentChildren()) {
            QRect ar = expandRect(option, index);
            if (ar.contains(mouseEvent->pos())) {
                node->collapsed = !node->collapsed;
                auto* tree = qobject_cast<QTreeWidget*>(const_cast<QWidget*>(option.widget));
                if (tree) {
                    if (QTreeWidgetItem* item = tree->itemFromIndex(index))
                        item->setExpanded(!node->collapsed);
                }
                return true;
            }
        }
        if (node && node->type == LayerTreeNode::Type::Group) {
            QRect ar = expandRect(option, index);
            if (ar.contains(mouseEvent->pos())) {
                node->collapsed = !node->collapsed;
                auto* tree = qobject_cast<QTreeWidget*>(const_cast<QWidget*>(option.widget));
                if (tree) {
                    QTreeWidgetItem* item = tree->itemFromIndex(index);
                    if (item) {
                        item->setExpanded(!node->collapsed);
                    }
                }
                return true;
            }
            QRect gr = groupIconRect(option, index);
            if (gr.contains(mouseEvent->pos())) {
                node->collapsed = !node->collapsed;
                auto* tree = qobject_cast<QTreeWidget*>(const_cast<QWidget*>(option.widget));
                if (tree) {
                    QTreeWidgetItem* item = tree->itemFromIndex(index);
                    if (item) {
                        item->setExpanded(!node->collapsed);
                    }
                }
                return true;
            }
        }

        int indent = childIndent(index);
        const bool hasMask = node && node->layer && !node->layer->maskImage.isNull();
        QRect eye = eyeRect(option, index);
        if (eye.contains(mouseEvent->pos())) {
            const int flat = index.data(Qt::UserRole).toInt();
            if (mouseEvent->modifiers() & Qt::AltModifier)
                emit visibilitySoloRequested(flat);    // solo/isolate
            else
                emit visibilityToggled(flat);
            return true;
        }
        QRect lk = lockRect(option, indent);
        if (lk.contains(mouseEvent->pos())) {
            emit lockToggled(index.data(Qt::UserRole).toInt(), 0);
            return true;
        }
        QRect fx = layerStylesRect(option, indent);
        // The fx (layer styles) button is only drawn for Layer nodes; adjustment
        // and group nodes have no layer styles, so don't claim its hit area.
        if (node && node->type == LayerTreeNode::Type::Layer && node->layer
            && fx.contains(mouseEvent->pos())) {
            emit layerStylesRequested(index.data(Qt::UserRole).toInt());
            return true;
        }
        QRect th = thumbRect(option, index);
        if (th.contains(mouseEvent->pos())) {
            // Clicking the pixel thumbnail targets the layer's pixels. Drop any
            // active mask editing, then let normal row selection proceed so the
            // clicked layer becomes active (a different layer also resets to
            // pixels via activeLayerChanged).
            if (m_controller && m_controller->isEditingMask())
                m_controller->setEditingMask(false);
            return false;
        }

        QRect mt = maskThumbRect(option, index);
        if (mt.contains(mouseEvent->pos())) {
            if (!hasMask)
                return false;
            emit maskToggled(index.data(Qt::UserRole).toInt(), mouseEvent->modifiers());
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

int LayerItemDelegate::handleSolidColorThumbDoubleClick(const QStyleOptionViewItem& option,
                                                        const QModelIndex& index,
                                                        const QPoint& pos)
{
    auto* node = nodeFromIndex(index);
    if (!node || !node->isAdjustmentLayer() || !node->adjustment
        || node->adjustment->type != QLatin1String("solidcolor"))
        return -1;
    if (!thumbRect(option, index).contains(pos))
        return -1;
    const int flat = index.data(Qt::UserRole).toInt();
    emit solidColorThumbDoubleClicked(flat);
    return flat;
}

QWidget* LayerItemDelegate::createEditor(QWidget* parent,
                                          const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    auto* editor = new QLineEdit(parent);
    editor->setStyleSheet(ThemeManager::instance()->current()->layerItemDelegateEditorStyleSheet());
    return editor;
}

void LayerItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit) return;
    auto* node = nodeFromIndex(index);
    if (!node) return;
    lineEdit->setText(node->name);
    lineEdit->selectAll();
}

void LayerItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                      const QModelIndex& index) const
{
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit) return;
    QString newName = lineEdit->text().trimmed();
    if (newName.isEmpty()) return;
    model->setData(index, newName, Qt::EditRole);
}

void LayerItemDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const
{
    const QRect nameRect = labelRect(option, index);
    if (!nameRect.isValid() || nameRect.width() <= 0) {
        editor->setGeometry(option.rect);
        return;
    }
    editor->setGeometry(nameRect.adjusted(0, -2, 0, 2));
}

LayerTreeNode* LayerItemDelegate::nodeFromIndex(const QModelIndex& index) const
{
    if (!m_controller || !m_controller->document()) return nullptr;
    int flatIdx = index.data(Qt::UserRole).toInt();
    return m_controller->document()->nodeAt(flatIdx);
}

int LayerItemDelegate::childIndent(const QModelIndex& index) const
{
    auto* node = nodeFromIndex(index);
    int depth = 0;
    while (node && node->parent) {
        ++depth;
        node = node->parent;
    }
    return depth * kIndent;
}

QRect LayerItemDelegate::eyeRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QRect r = option.rect;
    int x = r.left() + childIndent(index) + visibilityEyePadding();
    return QRect(x, r.center().y() - kEyeSize / 2, kEyeSize, kEyeSize);
}

QRect LayerItemDelegate::lockRect(const QStyleOptionViewItem& option, int extraIndent) const
{
    QRect r = option.rect;
    Q_UNUSED(extraIndent)
    int x = r.right() - kMargin - kRightLockPadding - kLockBtnSize;
    return QRect(x, r.center().y() - kLockBtnSize / 2, kLockBtnSize, kLockBtnSize);
}

QRect LayerItemDelegate::layerStylesRect(const QStyleOptionViewItem& option, int extraIndent) const
{
    QRect r = option.rect;
    Q_UNUSED(extraIndent)
    const QRect lockR = lockRect(option, extraIndent);
    int x = lockR.left() - kGap - kFxSize;
    return QRect(x, r.center().y() - kFxSize / 2, kFxSize, kFxSize);
}

int LayerItemDelegate::layerExpandArrowSpace(const QModelIndex& index) const
{
    auto* node = nodeFromIndex(index);
    if (node && node->type == LayerTreeNode::Type::Layer
        && node->hasVisibleAdjustmentChildren())
        return kArrowSize + kGap;
    return 0;
}

QRect LayerItemDelegate::thumbRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QRect r = option.rect;
    int x = contentStartAfterVisibilitySeparator(option, index)
        + layerExpandArrowSpace(index);
    return QRect(x, r.center().y() - kThumbSize / 2, kThumbSize, kThumbSize);
}

QRect LayerItemDelegate::maskThumbRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QRect r = option.rect;
    int x = contentStartAfterVisibilitySeparator(option, index)
        + layerExpandArrowSpace(index) + kThumbSize + kGap;
    return QRect(x, r.center().y() - kMaskThumbSize / 2, kMaskThumbSize, kMaskThumbSize);
}

QRect LayerItemDelegate::groupIconRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QRect r = option.rect;
    int x = contentStartAfterVisibilitySeparator(option, index) + kArrowSize + kGap;
    return QRect(x, r.center().y() - kGroupBtnSize / 2, kGroupBtnSize, kGroupBtnSize);
}

QRect LayerItemDelegate::labelRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QRect r = option.rect;
    auto* node = nodeFromIndex(index);
    if (!node)
        return QRect();

    const int extraIndent = childIndent(index);
    int leftX = contentStartAfterVisibilitySeparator(option, index);
    if (node->type == LayerTreeNode::Type::Group) {
        leftX = groupIconRect(option, index).right() + 1;
    } else {
        leftX += layerExpandArrowSpace(index) + kThumbSize + kGap;
        if (node->layer && !node->layer->maskImage.isNull())
            leftX += kMaskThumbSize + kGap;
    }
    leftX += kThumbToLabelPadding;

    int rightX = layerStylesRect(option, extraIndent).left() - kGap;
    switch (node->type) {
    case LayerTreeNode::Type::Adjustment:
    case LayerTreeNode::Type::Effect:
        rightX -= 26;
        break;
    default:
        break;
    }

    return QRect(leftX, r.top() + (kItemHeight - 18) / 2, qMax(rightX - leftX, 0), 18);
}

QRect LayerItemDelegate::expandRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QRect r = option.rect;
    int x = contentStartAfterVisibilitySeparator(option, index);
    return QRect(x, r.center().y() - kArrowSize / 2, kArrowSize, kArrowSize);
}

int LayerItemDelegate::visibilityEyePadding() const
{
    auto* th = ThemeManager::instance()->current();
    return th ? th->spaceMD : kGap;
}

int LayerItemDelegate::visibilitySeparatorGap() const
{
    auto* th = ThemeManager::instance()->current();
    return th ? th->spaceMD : kGap;
}

int LayerItemDelegate::contentStartAfterVisibilitySeparator(const QStyleOptionViewItem& option,
                                                            const QModelIndex& index) const
{
    const QRect eyeR = eyeRect(option, index);
    const int visGap = visibilitySeparatorGap();
    return eyeR.right() + visGap + 1 + visGap + 1;
}
