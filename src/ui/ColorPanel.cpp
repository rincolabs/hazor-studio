#include "ColorPanel.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ColorPanel::ColorPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* t = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("colorPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    root->setSpacing(t->spaceSM);

    auto* title = new QLabel(tr("Swatches"), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600; color: %1;").arg(t->colorTextPrimary.name()));
    root->addWidget(title);

    auto* activeRow = new QHBoxLayout();
    activeRow->setSpacing(t->spaceSM);
    auto* fgCol = new QVBoxLayout();
    auto* fgLabel = new QLabel(tr("Foreground"), this);
    m_foregroundBtn = makeColorButton(QSize(52, 42), tr("Edit foreground color"));
    fgCol->addWidget(fgLabel);
    fgCol->addWidget(m_foregroundBtn);
    activeRow->addLayout(fgCol);

    auto* bgCol = new QVBoxLayout();
    auto* bgLabel = new QLabel(tr("Background"), this);
    m_backgroundBtn = makeColorButton(QSize(52, 42), tr("Edit background color"));
    bgCol->addWidget(bgLabel);
    bgCol->addWidget(m_backgroundBtn);
    activeRow->addLayout(bgCol);

    auto* actions = new QVBoxLayout();
    m_swapBtn = new QPushButton(tr("Swap"), this);
    m_swapBtn->setToolTip(tr("Swap foreground/background (X)"));
    m_resetBtn = new QPushButton(tr("Reset"), this);
    m_resetBtn->setToolTip(tr("Reset to black/white (D)"));
    actions->addWidget(m_swapBtn);
    actions->addWidget(m_resetBtn);
    activeRow->addLayout(actions);
    activeRow->addStretch();
    root->addLayout(activeRow);

    auto* recentLabel = new QLabel(tr("Recent Colors"), this);
    recentLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: %1;").arg(t->colorTextPrimary.name()));
    root->addWidget(recentLabel);
    auto* recentGrid = new QGridLayout();
    recentGrid->setSpacing(4);
    for (int i = 0; i < 16; ++i) {
        auto* btn = makeColorButton(QSize(22, 22), tr("Recent color"));
        btn->setVisible(false);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        m_recentButtons.push_back(btn);
        recentGrid->addWidget(btn, i / 8, i % 8);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (i >= m_recentColors.size()) return;
            if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::MetaModifier))
                emit backgroundColorChanged(m_recentColors[i]);
            else
                emit foregroundColorChanged(m_recentColors[i]);
        });
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, i](const QPoint&) {
            if (i >= m_recentColors.size()) return;
            emit backgroundColorChanged(m_recentColors[i]);
        });
    }
    root->addLayout(recentGrid);

    root->addStretch();

    connect(m_foregroundBtn, &QPushButton::clicked, this, &ColorPanel::foregroundColorRequested);
    connect(m_backgroundBtn, &QPushButton::clicked, this, &ColorPanel::backgroundColorRequested);
    connect(m_swapBtn, &QPushButton::clicked, this, &ColorPanel::swapColorsRequested);
    connect(m_resetBtn, &QPushButton::clicked, this, &ColorPanel::resetColorsRequested);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ColorPanel::applyTheme);

    applyTheme();
    setForegroundColor(m_foreground);
    setBackgroundColor(m_background);
}

QString ColorPanel::colorButtonStyle(const QColor& color, bool active)
{
    auto* t = ThemeManager::instance()->current();
    QColor border = active ? t->colorAccent : t->colorBorder;
    return QStringLiteral("QPushButton { background-color: %1; border: %2px solid %3; border-radius: %4px; }")
        .arg(color.name(QColor::HexArgb))
        .arg(active ? 2 : 1)
        .arg(border.name())
        .arg(t->radiusSM);
}

QPushButton* ColorPanel::makeColorButton(const QSize& size, const QString& tooltip)
{
    auto* btn = new QPushButton(this);
    btn->setFixedSize(size);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

void ColorPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("ColorPanel { background: %1; color: %2; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));
    setForegroundColor(m_foreground);
    setBackgroundColor(m_background);
    setRecentColors(m_recentColors);
}

void ColorPanel::setForegroundColor(const QColor& color)
{
    m_foreground = color;
    if (m_foregroundBtn)
        m_foregroundBtn->setStyleSheet(colorButtonStyle(color, true));
}

void ColorPanel::setBackgroundColor(const QColor& color)
{
    m_background = color;
    if (m_backgroundBtn)
        m_backgroundBtn->setStyleSheet(colorButtonStyle(color));
}

void ColorPanel::setRecentColors(const QVector<QColor>& colors)
{
    m_recentColors = colors;
    for (int i = 0; i < m_recentButtons.size(); ++i) {
        bool visible = i < colors.size();
        m_recentButtons[i]->setVisible(visible);
        if (visible)
            m_recentButtons[i]->setStyleSheet(colorButtonStyle(colors[i]));
    }
}
