#pragma once

#include <QStyledItemDelegate>

class ImageController;
class LayerTreeNode;

class LayerItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit LayerItemDelegate(ImageController* controller, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;
    QRect labelRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    // If `pos` (viewport coords) is over a Solid Color fill layer's colour
    // thumbnail for `index`, returns its flat index and emits
    // solidColorThumbDoubleClicked; otherwise returns -1. Called from the tree's
    // mouseDoubleClickEvent so the picker opens reliably (the editable item would
    // otherwise consume the double-click for in-place rename).
    int handleSolidColorThumbDoubleClick(const QStyleOptionViewItem& option,
                                         const QModelIndex& index,
                                         const QPoint& pos);

signals:
    void visibilityToggled(int flatIndex);
    void visibilitySoloRequested(int flatIndex);   // Alt+Click eye → solo/isolate
    void lockToggled(int flatIndex, int lockFlag);
    void maskToggled(int flatIndex, Qt::KeyboardModifiers mods);
    void layerStylesRequested(int flatIndex);
    // Double-click on a Solid Color fill layer's colour thumbnail → edit colour.
    void solidColorThumbDoubleClicked(int flatIndex);

private:
    LayerTreeNode* nodeFromIndex(const QModelIndex& index) const;
    int childIndent(const QModelIndex& index) const;
    int visibilityEyePadding() const;
    int visibilitySeparatorGap() const;
    int contentStartAfterVisibilitySeparator(const QStyleOptionViewItem& option,
                                             const QModelIndex& index) const;
    QRect eyeRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QRect lockRect(const QStyleOptionViewItem& option, int extraIndent = 0) const;
    QRect layerStylesRect(const QStyleOptionViewItem& option, int extraIndent = 0) const;
    QRect thumbRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QRect maskThumbRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QRect groupIconRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QRect expandRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    // Width reserved before a Layer node's thumbnail for the expand/collapse
    // arrow it draws when it hosts Single-Layer-Mode adjustment children
    // (groups handle their own arrow). 0 for everything else.
    int layerExpandArrowSpace(const QModelIndex& index) const;

    ImageController* m_controller;
};
