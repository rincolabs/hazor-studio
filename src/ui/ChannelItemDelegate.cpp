#include "ChannelItemDelegate.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QPixmap>

namespace {
// Keep the base channel row visually aligned with LayerItemDelegate. Channels
// retain their own data and interactions; only the list metrics are shared.
constexpr int kItemHeight = 50;
constexpr int kMargin = 6;
constexpr int kGap = 4;
constexpr int kEyeSize = 20;
constexpr int kBadgeSize = 8;
constexpr int kThumbToLabelPadding = 16;
constexpr int kExpandedRowPadding = 10;
// Thumbnail edge per panel setting (1 small / 2 medium / 3 large).
constexpr int kThumbSmall = 40;
constexpr int kThumbMedium = 52;
constexpr int kThumbLarge = 64;

int visibilitySpacing()
{
    auto* th = ThemeManager::instance()->current();
    return th ? th->spaceMD : kMargin;
}
} // namespace

ChannelItemDelegate::ChannelItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ChannelItemDelegate::setThumbnailSize(int size)
{
    m_thumbnailSize = size < 0 ? 0 : (size > 3 ? 3 : size);
}

int ChannelItemDelegate::thumbnailPixels() const
{
    switch (m_thumbnailSize) {
    case 1: return kThumbSmall;
    case 2: return kThumbMedium;
    case 3: return kThumbLarge;
    default: return 0;
    }
}

QSize ChannelItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    Q_UNUSED(index);
    const int thumb = thumbnailPixels();
    const int height = thumb > kThumbSmall
        ? thumb + kExpandedRowPadding
        : kItemHeight;
    return QSize(option.rect.width(), height);
}

QRect ChannelItemDelegate::eyeRect(const QStyleOptionViewItem& option) const
{
    const QRect r = option.rect;
    return QRect(r.left() + visibilitySpacing(), r.center().y() - kEyeSize / 2,
                 kEyeSize, kEyeSize);
}

QRect ChannelItemDelegate::thumbRect(const QStyleOptionViewItem& option) const
{
    const int thumb = thumbnailPixels();
    if (thumb <= 0)
        return QRect();
    const QRect r = option.rect;
    const int spacing = visibilitySpacing();
    const int x = eyeRect(option).right() + spacing + 1 + spacing + 1;
    return QRect(x, r.center().y() - thumb / 2, thumb, thumb);
}

void ChannelItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    auto* th = ThemeManager::instance()->current();
    const bool armed = index.data(RoleArmed).toBool();
    const bool visible = index.data(RoleVisible).toBool();
    const bool selected = option.state & QStyle::State_Selected;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect r = option.rect;
    // Selection follows the Layers list. The armed edit target is also treated
    // as selected because it is the row the channel tools currently modify.
    if (armed || selected)
        painter->fillRect(r, th->colorSurfacePressed);
    else if (option.state & QStyle::State_MouseOver)
        painter->fillRect(r, th->colorSurfaceHover);
    else
        painter->fillRect(r, th->colorSurfaceRow);

    // ── Eye ──
    {
        static QPixmap visiblePix(QStringLiteral(":/icons/layer-visible.png"));
        static QPixmap hiddenPix(QStringLiteral(":/icons/layer-invisible.png"));
        painter->save();
        painter->setOpacity(th->iconOpacity);
        painter->drawPixmap(eyeRect(option), visible ? visiblePix : hiddenPix);
        painter->restore();

        const int sepX = eyeRect(option).right() + visibilitySpacing() + 1;
        painter->save();
        painter->setPen(QPen(th->colorBorder, 1));
        painter->drawLine(sepX, r.top(), sepX, r.bottom());
        painter->restore();
    }

    const int spacing = visibilitySpacing();
    int x = eyeRect(option).right() + spacing + 1 + spacing + 1;

    // ── Thumbnail ──
    const QRect thumb = thumbRect(option);
    if (!thumb.isEmpty()) {
        painter->save();
        painter->setPen(QPen(th->colorBorder, 1));
        painter->setBrush(th->colorSurfaceDark);
        painter->drawRect(thumb.adjusted(0, 0, -1, -1));
        const QImage image = index.data(RoleThumbnail).value<QImage>();
        if (!image.isNull()) {
            const QRect innerThumb = thumb.adjusted(2, 2, -2, -2);
            const QImage scaled = image.scaled(innerThumb.size(),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            const QPoint at(innerThumb.left() + (innerThumb.width() - scaled.width()) / 2,
                            innerThumb.top() + (innerThumb.height() - scaled.height()) / 2);
            painter->drawImage(at, scaled);
        }
        painter->restore();

        // Ink / overlay colour badge in the thumbnail's corner, so a spot
        // channel's ink is identifiable without opening its options.
        const QColor badge = index.data(RoleBadgeColor).value<QColor>();
        if (badge.isValid()) {
            painter->save();
            painter->setPen(QPen(th->colorBorder, 1));
            painter->setBrush(badge);
            painter->drawRect(QRect(thumb.right() - kBadgeSize,
                                    thumb.bottom() - kBadgeSize,
                                    kBadgeSize - 1, kBadgeSize - 1));
            painter->restore();
        }
        x = thumb.right() + kGap + kThumbToLabelPadding + 1;
    }

    // ── Name ──
    {
        QFont font = painter->font();
        font.setPixelSize(th->fontSizeMD);
        painter->setFont(font);
        painter->setPen(visible || armed ? th->colorTextPrimary : th->colorTextDisabled);
        const QRect textRect(x, r.top(), r.right() - x - kMargin, r.height());
        const QString text = index.data(Qt::DisplayRole).toString();
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(
                              text, Qt::ElideRight, textRect.width()));
    }

    // Match the subtle row boundary used by the Layers delegate.
    painter->setPen(QPen(th->colorPanelBackground, 1));
    painter->drawLine(r.bottomLeft(), r.bottomRight());

    painter->restore();
}
