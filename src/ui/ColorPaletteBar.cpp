#include "ColorPaletteBar.hpp"

#include "core/SwatchManager.hpp"
#include "ui/AppComboBox.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/ScrollBarStyle.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>

ColorPaletteBar::ColorPaletteBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("colorPaletteBar"));
    setFixedHeight(44);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 2, 2, 2);
    root->setSpacing(6);

    m_combo = new AppComboBox(this);
    m_combo->setFixedWidth(150);
    m_combo->setToolTip(tr("Active swatch collection"));
    root->addWidget(m_combo, 0, Qt::AlignVCenter);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("colorPaletteScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_strip = new QWidget(m_scrollArea);
    m_strip->setObjectName(QStringLiteral("colorPaletteStrip"));
    m_strip->setAttribute(Qt::WA_StyledBackground, true);
    m_stripLayout = new QHBoxLayout(m_strip);
    m_stripLayout->setContentsMargins(0, 0, 0, 0);
    m_stripLayout->setSpacing(2);
    m_strip->setLayout(m_stripLayout);
    m_scrollArea->setWidget(m_strip);
    root->addWidget(m_scrollArea, 1);

    connect(m_combo, qOverload<int>(&AppComboBox::currentIndexChanged),
            this, &ColorPaletteBar::onCollectionChosen);

    auto* mgr = SwatchManager::instance();
    connect(mgr, &SwatchManager::structureChanged, this, &ColorPaletteBar::refreshCollections);
    connect(mgr, &SwatchManager::collectionColorsChanged,
            this, &ColorPaletteBar::onCollectionColorsChanged);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ColorPaletteBar::applyTheme);

    // Restore the previously shown collection (falls back to the first one).
    QSettings settings;
    m_currentCollectionId = settings.value(QStringLiteral("swatches/barCollection")).toString();
    if (!mgr->findCollection(m_currentCollectionId))
        m_currentCollectionId = mgr->firstCollectionId();

    refreshCollections();
    applyTheme();
}

void ColorPaletteBar::refreshCollections()
{
    auto* mgr = SwatchManager::instance();
    const auto collections = mgr->allCollections();

    if (!mgr->findCollection(m_currentCollectionId))
        m_currentCollectionId = mgr->firstCollectionId();

    {
        QSignalBlocker block(m_combo);
        m_updatingCombo = true;
        m_combo->clear();
        int selectIndex = -1;
        for (int i = 0; i < collections.size(); ++i) {
            m_combo->addItem(collections[i].path, collections[i].id);
            if (collections[i].id == m_currentCollectionId)
                selectIndex = i;
        }
        if (selectIndex >= 0)
            m_combo->setCurrentIndex(selectIndex);
        m_updatingCombo = false;
    }

    rebuildButtons();
}

void ColorPaletteBar::onCollectionChosen(int index)
{
    if (m_updatingCombo || index < 0)
        return;
    setCurrentCollection(m_combo->itemData(index).toString());
}

void ColorPaletteBar::onCollectionColorsChanged(const QString& collectionId)
{
    if (collectionId == m_currentCollectionId)
        rebuildButtons();
}

void ColorPaletteBar::setCurrentCollection(const QString& id, bool persist)
{
    m_currentCollectionId = id;
    if (persist) {
        QSettings settings;
        settings.setValue(QStringLiteral("swatches/barCollection"), id);
    }
    rebuildButtons();
}

void ColorPaletteBar::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    // Scope the background to this widget (#id) so it does NOT cascade into the
    // child combo — letting the combo inherit the global theme like other combos.
    setStyleSheet(QStringLiteral(
        "#colorPaletteBar { background: %1; border-top: 1px solid %2; }")
        .arg(t->colorSurface.name(), t->colorBorder.name()));

    if (m_scrollArea) {
        ScrollBarQssOptions sb;
        sb.minLength = 30;
        sb.borderRadius = 4;
        sb.handleMargin = 2;
        sb.vertical = false;
        m_scrollArea->setStyleSheet(
            QStringLiteral(
                "#colorPaletteScrollArea { background: %1; border: none; }"
                "#colorPaletteScrollArea > QWidget > QWidget { background: %1; }")
                .arg(t->colorSurface.name())
            + scrollBarQss(t->colorSurfaceHover, t->colorBackgroundTertiary, sb));
    }

    if (m_strip) {
        m_strip->setStyleSheet(QStringLiteral("#colorPaletteStrip { background: %1; }")
            .arg(t->colorSurface.name()));
    }

    if (m_stripLayout)
        rebuildButtons();
}

QString ColorPaletteBar::swatchStyle(const QColor& color)
{
    auto* t = ThemeManager::instance()->current();
    return QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: %4px; }"
        "QPushButton:hover { border: 1px solid %3; }")
        .arg(color.name(QColor::HexArgb))
        .arg(t->colorBorder.name())
        .arg(t->colorAccentHover.name())
        .arg(t->radiusSM);
}

QString ColorPaletteBar::tooltipForColor(const QColor& color)
{
    return QStringLiteral("%1\nRGB(%2, %3, %4)")
        .arg(color.name(QColor::HexRgb).toUpper())
        .arg(color.red()).arg(color.green()).arg(color.blue());
}

void ColorPaletteBar::rebuildButtons()
{
    while (QLayoutItem* item = m_stripLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    SwatchNode* col = SwatchManager::instance()->findCollection(m_currentCollectionId);
    if (col) {
        for (const SwatchColor& sc : col->colors) {
            const QColor c = sc.color;
            auto* btn = new QPushButton(m_strip);
            btn->setFixedSize(24, 24);
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setToolTip(tooltipForColor(c));
            btn->setStyleSheet(swatchStyle(c));
            btn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(btn, &QPushButton::clicked, this, [this, c]() {
                const bool background =
                    QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::MetaModifier);
                if (background) emit backgroundColorSelected(c);
                else emit foregroundColorSelected(c);
            });
            connect(btn, &QPushButton::customContextMenuRequested, this, [this, c](const QPoint&) {
                emit backgroundColorSelected(c);
            });
            m_stripLayout->addWidget(btn);
        }
    }
    m_stripLayout->addStretch();
}
