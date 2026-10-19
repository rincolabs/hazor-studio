#include "FlowLayout.hpp"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem* item;
    while ((item = takeAt(0)))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::horizontalSpacing() const
{
    if (m_hSpace >= 0)
        return m_hSpace;
    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0)
        return m_vSpace;
    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

void FlowLayout::setRowAlignment(Qt::Alignment alignment)
{
    m_rowAlignment = alignment & Qt::AlignHorizontal_Mask;
    if (!m_rowAlignment)
        m_rowAlignment = Qt::AlignLeft;
    invalidate();
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size())
        return m_items.takeAt(index);
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effectiveRect = rect.adjusted(left, top, -right, -bottom);
    int y = effectiveRect.y();

    auto itemSpacing = [this](int index, Qt::Orientation orientation) {
        const int configured = orientation == Qt::Horizontal
            ? horizontalSpacing() : verticalSpacing();
        if (configured >= 0)
            return configured;

        const QWidget* widget = m_items[index]->widget();
        return widget ? widget->style()->layoutSpacing(
                            QSizePolicy::PushButton, QSizePolicy::PushButton, orientation)
                      : 0;
    };

    auto layoutRow = [&](int first, int end, int rowWidth, int rowHeight) {
        int x = effectiveRect.x();
        const int remainingWidth = qMax(0, effectiveRect.width() - rowWidth);
        if (m_rowAlignment.testFlag(Qt::AlignHCenter))
            x += remainingWidth / 2;
        else if (m_rowAlignment.testFlag(Qt::AlignRight))
            x += remainingWidth;

        if (!testOnly) {
            for (int index = first; index < end; ++index) {
                QLayoutItem* item = m_items[index];
                item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
                x += item->sizeHint().width();
                if (index + 1 < end)
                    x += itemSpacing(index, Qt::Horizontal);
            }
        }
        y += rowHeight;
    };

    int rowStart = 0;
    int rowWidth = 0;
    int rowHeight = 0;
    for (int index = 0; index < m_items.size(); ++index) {
        const QSize itemSize = m_items[index]->sizeHint();
        const int gap = rowWidth > 0 ? itemSpacing(index - 1, Qt::Horizontal) : 0;
        const int nextWidth = rowWidth + gap + itemSize.width();

        if (rowWidth > 0 && nextWidth > effectiveRect.width()) {
            layoutRow(rowStart, index, rowWidth, rowHeight);
            y += itemSpacing(index, Qt::Vertical);
            rowStart = index;
            rowWidth = itemSize.width();
            rowHeight = itemSize.height();
        } else {
            rowWidth = nextWidth;
            rowHeight = qMax(rowHeight, itemSize.height());
        }
    }

    if (rowWidth > 0)
        layoutRow(rowStart, m_items.size(), rowWidth, rowHeight);

    return y - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* parent = this->parent();
    if (!parent) {
        return -1;
    } else if (parent->isWidgetType()) {
        QWidget* pw = static_cast<QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    } else {
        return static_cast<QLayout*>(parent)->spacing();
    }
}
