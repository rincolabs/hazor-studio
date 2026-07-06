#include "BrushPresetItemDelegate.hpp"
#include "BrushLibraryTreeModel.hpp"
#include "BrushPresetListModel.hpp"
#include "IconUtils.hpp"
#include "brush/BrushPreviewCache.hpp"
#include "brush/BrushPreset.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QIcon>
#include <QPainter>
#include <QWidget>

BrushPresetItemDelegate::BrushPresetItemDelegate(BrushPreviewCache* cache, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_cache(cache)
{
}

QSize BrushPresetItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const
{
    Q_UNUSED(option);
    if (m_compact)
        return QSize(kCompactCell, kCompactCell);
    if (const auto* tree = qobject_cast<const BrushLibraryTreeModel*>(index.model())) {
        if (tree->isFolder(index))
            return QSize(130, 30);
    }
    // Width is only a hint: in ListMode the view stretches each row to the
    // viewport width, which is the rect handed to paint().
    return QSize(120, kRowHeight);
}

void BrushPresetItemDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    const auto* treeModel = qobject_cast<const BrushLibraryTreeModel*>(index.model());
    if (treeModel && treeModel->isFolder(index)) {
        const Theme* t = ThemeManager::instance()->current();
        const QRect rect = option.rect;
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool open = option.state & QStyle::State_Open;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QColor bg = Qt::transparent;
        if (selected)
            bg = t->colorSurfaceSelected;
        else if (hovered)
            bg = t->colorSurfaceHover;
        if (bg.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(rect.adjusted(2, 1, -2, -1), t->radiusSM, t->radiusSM);
        }

        const QString iconPath = treeModel->data(index, BrushLibraryTreeModel::FolderIdRole)
                                     .toString().contains(QLatin1String("imported-brushes"))
            ? QStringLiteral(":/icons/brush-folder-imported.png")
            : (open ? QStringLiteral(":/icons/ui-folder-open.png")
                    : QStringLiteral(":/icons/ui-folder-close.png"));
        const QRect iconRect(rect.left() + t->spaceXS,
                             rect.top() + (rect.height() - 18) / 2,
                             18, 18);
        makeIcon(iconPath).paint(painter, iconRect);

        const int countWidth = 46;
        QRect nameRect = rect.adjusted(t->spaceXS + 24, 0, -countWidth - t->spaceSM, 0);
        QRect countRect(rect.right() - countWidth - t->spaceSM, rect.top(), countWidth, rect.height());

        QFont f = option.font;
        f.setBold(true);
        painter->setFont(f);
        painter->setPen(selected ? t->colorTextBright : t->colorTextPrimary);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, nameRect.width()));

        painter->setFont(option.font);
        painter->setPen(selected ? t->colorTextBright : t->colorTextSecondary);
        painter->drawText(countRect, Qt::AlignRight | Qt::AlignVCenter,
                          QString::number(index.data(BrushLibraryTreeModel::BrushCountRole).toInt()));
        painter->restore();
        return;
    }

    const auto* listModel = qobject_cast<const BrushPresetListModel*>(index.model());
    const BrushPreset* preset = nullptr;
    if (treeModel)
        preset = treeModel->presetForIndex(index);
    else if (listModel)
        preset = listModel->presetAt(index.row());
    if (!preset || !m_cache) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    if (m_compact) {
        paintCompact(painter, option, *preset);
        return;
    }

    const Theme* t = ThemeManager::instance()->current();
    const QRect rect = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // ── Background ──
    QColor bg = Qt::transparent;
    if (selected)
        bg = t->colorSurfaceSelected;
    else if (hovered)
        bg = t->colorSurfaceHover;
    if (bg.alpha() > 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(rect.adjusted(2, 1, -2, -1), t->radiusMD, t->radiusMD);
    }
    if (selected) {
        QPen pen(t->colorAccent);
        pen.setWidth(1);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(2, 1, -3, -2), t->radiusMD, t->radiusMD);
    }

    const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;
    const QColor ink = selected ? t->colorTextBright : t->colorTextPrimary;

    // A preset that carries its own preview image gets a full-row-height thumbnail.
    const bool hasPreview = !preset->previewImageData.isEmpty();
    const RowLayout lay = layoutFor(rect, hasPreview);

    // ── Column 1: tip preview ──
    const QPixmap tip = m_cache->tip(*preset, lay.tip.size(), dpr, ink);
    painter->drawPixmap(lay.tip.topLeft(), tip);

    // ── Column 2: stroke preview (top) + name (bottom) ──
    if (lay.hasStroke) {
        // Rendered at a fixed reference size and scaled to the cell, so the same
        // cached pixmap serves any row width (enables full startup pre-warm).
        const QPixmap stroke = m_cache->stroke(*preset, strokeRefSize(), dpr, ink);
        painter->drawPixmap(lay.stroke, stroke);

        const QString subtitle = index.data(BrushLibraryTreeModel::SourceSubtitleRole).toString();
        painter->setPen(selected ? t->colorTextBright : t->colorTextSecondary);
        QFont f = option.font;
        // The font may be sized in pixels (pointSizeF() == -1) rather than points;
        // scale whichever metric is actually set so we never pass a negative size.
        if (f.pointSizeF() > 0.0)
            f.setPointSizeF(f.pointSizeF() * 0.92);
        else if (f.pixelSize() > 0)
            f.setPixelSize(qMax(1, qRound(f.pixelSize() * 0.92)));
        painter->setFont(f);
        const QString label = subtitle.isEmpty()
            ? preset->name
            : QStringLiteral("%1  %2").arg(preset->name, subtitle);
        const QString elided = painter->fontMetrics().elidedText(label, Qt::ElideRight, lay.name.width());
        painter->drawText(lay.name, Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

    painter->restore();
}

BrushPresetItemDelegate::RowLayout
BrushPresetItemDelegate::layoutFor(const QRect& rect, bool fillHeight) const
{
    const Theme* t = ThemeManager::instance()->current();
    const int pad = t->spaceSM;

    // The preview thumbnail fills the row height (a square that size); the rendered
    // tip keeps its small fixed square.
    const int tipS = fillHeight ? qMax(1, rect.height() - pad * 2) : kTipSize;

    RowLayout lay;
    lay.tip = QRect(rect.left() + pad,
                    rect.top() + (rect.height() - tipS) / 2,
                    tipS, tipS);

    const int rightLeft = lay.tip.right() + pad + t->spaceSM;
    const int rightWidth = rect.right() - rightLeft - pad;
    lay.hasStroke = rightWidth > 8;
    if (lay.hasStroke) {
        const int strokeH = rect.height() - kNameHeight - t->spaceXS * 2;
        lay.stroke = QRect(rightLeft, rect.top() + t->spaceXS, rightWidth, strokeH);
        lay.name = QRect(rightLeft, lay.stroke.bottom(), rightWidth, kNameHeight);
    }
    return lay;
}

QSize BrushPresetItemDelegate::strokeRefSize() const
{
    const Theme* t = ThemeManager::instance()->current();
    const int h = kRowHeight - kNameHeight - t->spaceXS * 2;
    return QSize(kStrokeRefWidth, qMax(1, h));
}

void BrushPresetItemDelegate::paintCompact(QPainter* painter,
                                           const QStyleOptionViewItem& option,
                                           const BrushPreset& preset) const
{
    const Theme* t = ThemeManager::instance()->current();
    const QRect rect = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QColor bg = Qt::transparent;
    if (selected)
        bg = t->colorSurfaceSelected;
    else if (hovered)
        bg = t->colorSurfaceHover;
    if (bg.alpha() > 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(rect.adjusted(2, 2, -2, -2), t->radiusSM, t->radiusSM);
    }
    if (selected) {
        QPen pen(t->colorAccent);
        pen.setWidth(1);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(2, 2, -3, -3), t->radiusSM, t->radiusSM);
    }

    const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;
    const QColor ink = selected ? t->colorTextBright : t->colorTextPrimary;
    const QRect tipRect(rect.left() + (rect.width() - kCompactTip) / 2,
                        rect.top() + (rect.height() - kCompactTip) / 2,
                        kCompactTip, kCompactTip);
    const QPixmap tip = m_cache->tip(preset, QSize(kCompactTip, kCompactTip), dpr, ink);
    painter->drawPixmap(tipRect.topLeft(), tip);

    painter->restore();
}
