#include "CustomShapesPanel.hpp"

#include "shape/SvgShapeConverter.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/ScrollBarStyle.hpp"

#include <QAbstractListModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QFrame>
#include <QFutureWatcher>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QScrollBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {

constexpr int kCellWidth = 72;
constexpr int kCellHeight = 76;
constexpr int kIconSize = 40;
constexpr int kCellSpacing = 6;

enum ShapeIconRoles {
    InfoRole = Qt::UserRole + 1,
    ThumbnailRole,
    ValidRole
};

struct ShapeIconListItem {
    ShapeIconInfo icon;
    QImage thumbnail;
    bool valid = false;
};

QPainterPath painterPathFor(const VectorPath& vectorPath)
{
    QPainterPath path;
    path.setFillRule(vectorPath.fillRule);
    for (const auto& command : vectorPath.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
            path.moveTo(command.p1);
            break;
        case PathCommandType::LineTo:
            path.lineTo(command.p1);
            break;
        case PathCommandType::QuadTo:
            path.quadTo(command.p1, command.p2);
            break;
        case PathCommandType::CubicTo:
            path.cubicTo(command.p1, command.p2, command.p3);
            break;
        case PathCommandType::ClosePath:
            path.closeSubpath();
            break;
        }
    }
    return path;
}

QImage renderThumbnail(const VectorPath& vectorPath, const QColor& color)
{
    QPainterPath path = painterPathFor(vectorPath);
    const QRectF bounds = vectorPath.localBounds.isValid() ? vectorPath.localBounds : path.boundingRect();
    if (bounds.isEmpty())
        return {};

    QImage image(QSize(kIconSize, kIconSize), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal pad = 7.0;
    const qreal sx = (kIconSize - pad * 2.0) / bounds.width();
    const qreal sy = (kIconSize - pad * 2.0) / bounds.height();
    const qreal scale = std::max<qreal>(0.001, std::min(sx, sy));

    QTransform xf;
    xf.translate(kIconSize * 0.5, kIconSize * 0.5);
    xf.scale(scale, -scale);
    xf.translate(-bounds.center().x(), -bounds.center().y());
    path = xf.map(path);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(path);
    painter.end();
    return image;
}

QList<ShapeIconListItem> prepareIconItems(QList<ShapeIconInfo> icons,
                                          const QString& query,
                                          const QColor& thumbnailColor)
{
    const QString needle = query.trimmed();
    QList<ShapeIconListItem> items;
    items.reserve(icons.size());

    SvgShapeConverter converter;
    for (const ShapeIconInfo& icon : icons) {
        if (!needle.isEmpty()
            && !icon.name.contains(needle, Qt::CaseInsensitive)
            && !icon.id.contains(needle, Qt::CaseInsensitive)
            && !icon.category.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }

        const SvgShapeConversionResult converted = converter.convertFromResource(icon.resourcePath);
        if (!converted.success)
            continue;

        QImage thumbnail = renderThumbnail(converted.shapeData.path, thumbnailColor);
        if (thumbnail.isNull())
            continue;

        ShapeIconListItem item;
        item.icon = icon;
        item.valid = true;
        item.thumbnail = std::move(thumbnail);
        items.push_back(std::move(item));
    }
    return items;
}

QString viewStyle(const Theme* t)
{
    ScrollBarQssOptions sb;
    sb.minLength = 30;
    sb.borderRadius = 4;
    sb.handleMargin = 2;
    sb.horizontal = false;
    sb.hideOtherOrientation = true; // collapse any stray horizontal scrollbar
    return QStringLiteral(
        "QListView { background: %1; border: none; outline: none; }"
        "QListView::item { border: none; }")
        .arg(t->colorSurface.name())
        + scrollBarQss(t->colorBackgroundTertiary, t->colorSurfaceHover, sb);
}

} // namespace

class ShapeIconListModel : public QAbstractListModel {
public:
    explicit ShapeIconListModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
            return {};

        const ShapeIconListItem& item = m_items.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return item.icon.name;
        case Qt::ToolTipRole:
            return QStringLiteral("%1 - %2").arg(item.icon.name, item.icon.category);
        case InfoRole:
            return QVariant::fromValue(item.icon);
        case ThumbnailRole:
            return item.thumbnail;
        case ValidRole:
            return item.valid;
        default:
            return {};
        }
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        Qt::ItemFlags f = QAbstractListModel::flags(index);
        if (index.isValid() && !m_items.at(index.row()).valid)
            f &= ~Qt::ItemIsEnabled;
        return f;
    }

    void setItems(QList<ShapeIconListItem> items)
    {
        beginResetModel();
        m_items = std::move(items);
        endResetModel();
    }

    ShapeIconInfo iconAt(const QModelIndex& index) const
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
            return {};
        return m_items.at(index.row()).icon;
    }

    int rowForId(const QString& id) const
    {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).icon.id == id)
                return i;
        }
        return -1;
    }

private:
    QList<ShapeIconListItem> m_items;
};

class ShapeIconDelegate : public QStyledItemDelegate {
public:
    explicit ShapeIconDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(kCellWidth, kCellHeight);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto* t = ThemeManager::instance()->current();
        const bool selected = option.state & QStyle::State_Selected;
        const bool enabled = index.data(ValidRole).toBool();
        const QString name = index.data(Qt::DisplayRole).toString();
        const QImage thumb = index.data(ThumbnailRole).value<QImage>();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QRect r = option.rect.adjusted(3, 3, -3, -3);
        const QColor border = selected ? t->colorAccent : (enabled ? t->colorBorder : t->colorTextDisabled);
        const QColor background = selected ? t->colorSurfaceSelected : t->colorSurface;
        painter->setPen(QPen(border, 1));
        painter->setBrush(background);
        painter->drawRoundedRect(r, t->radiusMD, t->radiusMD);

        QRect iconRect(r.center().x() - kIconSize / 2, r.top() + 7, kIconSize, kIconSize);
        if (enabled && !thumb.isNull()) {
            painter->drawImage(iconRect, thumb);
        } else {
            painter->setPen(t->colorTextDisabled);
            painter->drawText(iconRect, Qt::AlignCenter, QStringLiteral("!"));
        }

        QFont textFont = option.font;
        textFont.setPixelSize(t->fontSizeXS);
        painter->setFont(textFont);
        painter->setPen(enabled ? t->colorTextPrimary : t->colorTextDisabled);
        const QString elided = option.fontMetrics.elidedText(name, Qt::ElideRight, r.width() - 8);
        painter->drawText(QRect(r.left() + 4, iconRect.bottom() + 4, r.width() - 8, 16),
                          Qt::AlignCenter, elided);

        painter->restore();
    }
};

CustomShapesPanel::CustomShapesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* t = ThemeManager::instance()->current();
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    root->setSpacing(t->spaceSM);

    auto* title = new QLabel(tr("Custom Shapes"), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600; color: %1;").arg(t->colorTextPrimary.name()));
    root->addWidget(title);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search shapes..."));
    m_search->setClearButtonEnabled(true);
    root->addWidget(m_search);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorBorder.name()));
    root->addWidget(sep);

    m_model = new ShapeIconListModel(this);
    m_delegate = new ShapeIconDelegate(this);

    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setWrapping(true);
    m_view->setUniformItemSizes(true);
    m_view->setSpacing(kCellSpacing);
    m_view->setGridSize(QSize(kCellWidth, kCellHeight));
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    root->addWidget(m_view, 1);

    m_emptyLabel = new QLabel(tr("No shapes found."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    root->addWidget(m_emptyLabel, 1);

    m_searchLoadingBar = new QProgressBar(this);
    m_searchLoadingBar->setRange(0, 0);
    m_searchLoadingBar->setTextVisible(false);
    m_searchLoadingBar->hide();
    root->addWidget(m_searchLoadingBar, 1);

    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(220);

    m_searchLoadingTimer = new QTimer(this);
    m_searchLoadingTimer->setSingleShot(true);
    m_searchLoadingTimer->setInterval(2000);

    connect(m_search, &QLineEdit::textChanged, this, &CustomShapesPanel::scheduleFilter);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &CustomShapesPanel::startAsyncFilter);
    connect(m_searchLoadingTimer, &QTimer::timeout, this, &CustomShapesPanel::showSearchLoading);
    connect(m_view, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || !index.data(ValidRole).toBool())
            return;
        const ShapeIconInfo icon = m_model->iconAt(index);
        setSelectedIcon(icon.id);
        emit iconSelected(icon);
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &CustomShapesPanel::applyTheme);

    applyTheme();
}

void CustomShapesPanel::reloadIcons()
{
    ++m_searchRevision;
    m_initialPreparationPending = true;
    m_searchLoadingTimer->start();

    const int revision = m_searchRevision;
    auto* watcher = new QFutureWatcher<QList<ShapeIconInfo>>(this);
    connect(watcher, &QFutureWatcher<QList<ShapeIconInfo>>::finished, this,
            [this, watcher, revision]() {
        if (revision == m_searchRevision)
            setIcons(watcher->result());
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([]() {
        ShapeIconLibrary library;
        return library.loadBuiltInIcons();
    }));
}

void CustomShapesPanel::setIcons(QList<ShapeIconInfo> icons, int)
{
    m_library.setIcons(std::move(icons));
    m_initialPreparationPending = true;
    ++m_searchRevision;
    startAsyncFilter();
}

void CustomShapesPanel::setSelectedIcon(const QString& id)
{
    if (m_selectedId == id)
        return;
    m_selectedId = id;
    applySelectionToView();
}

void CustomShapesPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("CustomShapesPanel { background: %1; color: %2; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));
    if (m_view)
        m_view->setStyleSheet(viewStyle(t));
    if (m_emptyLabel)
        m_emptyLabel->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    if (m_view && m_view->viewport())
        m_view->viewport()->update();
}

void CustomShapesPanel::scheduleFilter()
{
    ++m_searchRevision;
    m_initialPreparationPending = false;
    m_searchLoadingTimer->stop();
    hideSearchLoading();
    m_searchDebounceTimer->start();
}

void CustomShapesPanel::startAsyncFilter()
{
    const int revision = m_searchRevision;
    const QString query = m_search ? m_search->text() : QString();
    const QList<ShapeIconInfo> icons = m_library.icons();
    const QColor thumbnailColor = ThemeManager::instance()->current()->colorTextPrimary;

    m_searchLoadingTimer->start();

    auto* watcher = new QFutureWatcher<QList<ShapeIconListItem>>(this);
    connect(watcher, &QFutureWatcher<QList<ShapeIconListItem>>::finished, this,
            [this, watcher, revision, query]() {
        if (revision == m_searchRevision && (!m_search || m_search->text() == query)) {
            m_searchLoadingTimer->stop();
            hideSearchLoading();
            m_model->setItems(watcher->result());
            applySelectionToView();

            const bool empty = m_model->rowCount() == 0;
            if (m_view)
                m_view->setVisible(!empty);
            if (m_emptyLabel)
                m_emptyLabel->setVisible(empty);

            if (m_initialPreparationPending) {
                m_initialPreparationPending = false;
                emit iconsPrepared();
            }
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([icons, query, thumbnailColor]() {
        return prepareIconItems(icons, query, thumbnailColor);
    }));
}

void CustomShapesPanel::showSearchLoading()
{
    if (m_view)
        m_view->hide();
    if (m_emptyLabel)
        m_emptyLabel->hide();
    if (m_searchLoadingBar)
        m_searchLoadingBar->show();
}

void CustomShapesPanel::hideSearchLoading()
{
    if (m_searchLoadingBar)
        m_searchLoadingBar->hide();
}

void CustomShapesPanel::applySelectionToView()
{
    if (!m_view || !m_model)
        return;

    const int row = m_model->rowForId(m_selectedId);
    QItemSelectionModel* selection = m_view->selectionModel();
    if (!selection)
        return;

    if (row < 0) {
        selection->clearSelection();
        return;
    }

    const QModelIndex index = m_model->index(row, 0);
    selection->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_view->setCurrentIndex(index);
}
