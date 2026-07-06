#include "BrushResourceBrowser.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QPixmap>
#include <QIcon>

BrushResourceBrowser::BrushResourceBrowser(BrushResourceBrowserMode mode,
                                           QWidget* parent)
    : QWidget(parent), m_mode(mode)
{
    const bool texMode = (m_mode == BrushResourceBrowserMode::BrushTexture);
    const Theme* t = ThemeManager::instance()->current();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    lay->setSpacing(t->spaceXS);

    // Name/size label — only the texture page shows it (no preview, no tag for tips).
    if (texMode) {
        m_nameLabel = new QLabel(tr("No texture selected"), this);
        m_nameLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;")
                                       .arg(t->colorTextBright.name()));
        lay->addWidget(m_nameLabel);
    }

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(texMode ? tr("Search textures...") : tr("Search tips..."));
    lay->addWidget(m_search);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { rebuildGrid(); });

    auto* body = new QHBoxLayout;
    body->setSpacing(t->spaceSM);

    m_grid = new QListWidget(this);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(QSize(56, 56));
    m_grid->setGridSize(QSize(64, 64));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);
    m_grid->setSpacing(2);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setMinimumWidth(150);
    connect(m_grid, &QListWidget::itemSelectionChanged, this, [this]() {
        if (m_loading) return;
        const auto items = m_grid->selectedItems();
        if (items.isEmpty()) return;
        const QString id = items.first()->data(Qt::UserRole).toString();
        if (id == m_selectedId) return;
        m_selectedId = id;
        updatePreview();
        emit selected(id);
    });
    body->addWidget(m_grid, 1);

    // Large side preview — texture page only.
    if (texMode) {
        m_previewScroll = new QScrollArea(this);
        m_previewScroll->setWidgetResizable(false);   // real size + scroll when larger
        m_previewScroll->setAlignment(Qt::AlignCenter);
        m_previewScroll->setMinimumWidth(150);
        m_preview = new QLabel(m_previewScroll);
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setStyleSheet(QStringLiteral("background: %1;")
                                     .arg(t->colorSurfaceDark.name()));
        m_previewScroll->setWidget(m_preview);
        body->addWidget(m_previewScroll, 1);
    }

    lay->addLayout(body, 1);

    auto* footer = new QHBoxLayout;
    footer->setSpacing(t->spaceXS);
    m_importBtn = new QPushButton(tr("Import..."), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    footer->addStretch();
    footer->addWidget(m_importBtn);
    footer->addWidget(m_removeBtn);
    lay->addLayout(footer);

    connect(m_importBtn, &QPushButton::clicked, this, [this]() { emit importRequested(); });
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_selectedId.isEmpty())
            emit removeRequested(m_selectedId);
    });
}

const BrushResourceBrowser::Item* BrushResourceBrowser::itemForId(const QString& id) const
{
    if (id.isEmpty())
        return nullptr;
    for (const Item& it : m_items)
        if (it.id == id)
            return &it;
    return nullptr;
}

void BrushResourceBrowser::setItems(const QVector<Item>& items)
{
    m_items = items;
    // Drop a selection that no longer exists.
    if (!m_selectedId.isEmpty() && !itemForId(m_selectedId))
        m_selectedId.clear();
    rebuildGrid();
}

void BrushResourceBrowser::setSelectedId(const QString& id)
{
    m_selectedId = itemForId(id) ? id : QString();
    if (!m_grid) return;
    QSignalBlocker b(m_grid);
    m_grid->clearSelection();
    for (int i = 0; i < m_grid->count(); ++i) {
        auto* it = m_grid->item(i);
        if (it->data(Qt::UserRole).toString() == m_selectedId) {
            it->setSelected(true);
            m_grid->setCurrentItem(it);
            break;
        }
    }
    updatePreview();
}

void BrushResourceBrowser::rebuildGrid()
{
    if (!m_grid) return;
    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();

    m_loading = true;
    m_grid->clear();
    for (const Item& it : m_items) {
        if (!filter.isEmpty() && !it.name.toLower().contains(filter))
            continue;
        auto* row = new QListWidgetItem(m_grid);
        row->setData(Qt::UserRole, it.id);
        row->setToolTip(it.name);
        row->setIcon(QIcon(QPixmap::fromImage(
            it.image.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
        if (it.id == m_selectedId) {
            row->setSelected(true);
            m_grid->setCurrentItem(row);
        }
    }
    m_loading = false;
    updatePreview();
}

void BrushResourceBrowser::updatePreview()
{
    const Item* it = itemForId(m_selectedId);

    if (m_nameLabel) {
        if (!it || it->image.isNull())
            m_nameLabel->setText(tr("No texture selected"));
        else
            m_nameLabel->setText(QStringLiteral("%1 (%2 x %3)")
                .arg(it->name).arg(it->image.width()).arg(it->image.height()));
    }
    if (m_preview) {
        if (!it || it->image.isNull()) {
            m_preview->setPixmap(QPixmap());
        } else {
            m_preview->setPixmap(QPixmap::fromImage(it->image));
            m_preview->resize(it->image.size());
        }
    }
}
