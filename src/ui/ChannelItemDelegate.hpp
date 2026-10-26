#pragma once

#include <QImage>
#include <QStyledItemDelegate>

class ImageController;

// Row painter for the Channels panel: eye, thumbnail, name, and the visual
// difference between a channel that is ARMED for editing, one that is merely
// visible, and one that is hidden — three states the panel has to keep
// distinguishable because they are three independent properties.
//
// The thumbnail content is supplied per row by the panel (which owns the
// composite source); the delegate only scales and frames it.
class ChannelItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // Data roles the panel fills in on each row.
    enum Role {
        RoleKind = Qt::UserRole,        // ChannelsPanel::RowKind
        RolePayload,                    // component index or channel index
        RoleVisible,                    // bool — the eye
        RoleArmed,                      // bool — armed as an edit target
        RoleThumbnail,                  // QImage — already square-ish
        RoleBadgeColor,                 // QColor — spot ink / overlay colour
    };

    explicit ChannelItemDelegate(QObject* parent = nullptr);

    // 0 = no thumbnails, 1 = small, 2 = medium, 3 = large.
    void setThumbnailSize(int size);
    int thumbnailSize() const { return m_thumbnailSize; }
    int thumbnailPixels() const;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    QRect eyeRect(const QStyleOptionViewItem& option) const;
    QRect thumbRect(const QStyleOptionViewItem& option) const;

private:
    int m_thumbnailSize = 1;
};
