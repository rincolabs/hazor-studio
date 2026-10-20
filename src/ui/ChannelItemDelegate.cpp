#include "ChannelItemDelegate.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QPixmap>

namespace {
constexpr int kMargin = 6;
constexpr int kGap = 6;
constexpr int kEyeSize = 20;
constexpr int kBadgeSize = 8;
constexpr int kRowPadding = 6;
// Thumbnail edge per panel setting (1 small / 2 medium / 3 large).
constexpr int kThumbSmall = 22;
constexpr int kThumbMedium = 32;
constexpr int kThumbLarge = 44;
// Row height when thumbnails are off: tall enough for the eye and the label.
constexpr int kRowHeightNoThumb = 24;
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
    const int height = thumb > 0 ? thumb + kRowPadding * 2 : kRowHeightNoThumb;
    return QSize(option.rect.width(), height);
}

QRect ChannelItemDelegate::eyeRect(const QStyleOptionViewItem& option) const
{
    const QRect r = option.rect;
    return QRect(r.left() + kMargin, r.center().y() - kEyeSize / 2,
                 kEyeSize, kEyeSize);
}

QRect ChannelItemDelegate::thumbRect(const QStyleOptionViewItem& option) const
{
    const int thumb = thumbnailPixels();
    if (thumb <= 0)
        return QRect();
    const QRect r = option.rect;
    const int x = eyeRect(option).right() + kGap + 1;
    return QRect(x, r.center().y() - thumb / 2, thumb, thumb);
}

void ChannelItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    auto* th = ThemeManager::instance()->current();
    const bool armed = index.data(RoleArmed).toBool();
    const bool visible = index.data(RoleVisible).toBool();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect r = option.rect;
    // Three states, three backgrounds. ARMED is the edit target and gets the
    // selected surface; a merely visible row gets the plain row surface; the
    // hover tint still applies on top of either.
    if (armed)
        painter->fillRect(r, th->colorSurfaceSelected);
    else if (option.state & QStyle::State_MouseOver)
        painter->fillRect(r, th->colorSurfaceHover);
    else
        painter->fillRect(r, th->colorSurfaceRow);

    if (armed) {
        // A left accent bar, so the edit target is readable even where the
        // selected surface is subtle.
        painter->fillRect(QRect(r.left(), r.top(), 2, r.height()), th->colorAccent);
    }

    // ── Eye ──
    {
        static QPixmap visiblePix(QStringLiteral(":/icons/layer-visible.png"));
        static QPixmap hiddenPix(QStringLiteral(":/icons/layer-invisible.png"));
        painter->save();
        painter->setOpacity(th->iconOpacity);
        painter->drawPixmap(eyeRect(option), visible ? visiblePix : hiddenPix);
        painter->restore();

        const int sepX = eyeRect(option).right() + kGap / 2;
        painter->save();
        painter->setPen(QPen(th->colorBorder, 1));
        painter->drawLine(sepX, r.top(), sepX, r.bottom());
        painter->restore();
    }

    int x = eyeRect(option).right() + kGap + 1;

    // ── Thumbnail ──
    const QRect thumb = thumbRect(option);
    if (!thumb.isEmpty()) {
        painter->save();
        painter->setPen(QPen(th->colorBorder, 1));
        painter->setBrush(th->colorSurfaceDark);
        painter->drawRect(thumb.adjusted(0, 0, -1, -1));
        const QImage image = index.data(RoleThumbnail).value<QImage>();
        if (!image.isNull()) {
            const QImage scaled = image.scaled(thumb.size() - QSize(2, 2),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            const QPoint at(thumb.left() + 1 + (thumb.width() - 2 - scaled.width()) / 2,
                            thumb.top() + 1 + (thumb.height() - 2 - scaled.height()) / 2);
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
        x = thumb.right() + kGap + 2;
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

    painter->restore();
}
