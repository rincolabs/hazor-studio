#include "HistoryPanel.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QFrame>

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("historyPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi();
}

void HistoryPanel::setupUi()
{
    auto* t = ThemeManager::instance()->current();
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    mainLay->setSpacing(t->spaceSM);

    // ── Header ──
    auto* headerRow = new QHBoxLayout();

    auto* titleLabel = new QLabel(tr("<b>History</b>"), this);
    headerRow->addWidget(titleLabel);
    headerRow->addStretch();

    m_clearBtn = new QPushButton(tr("Clear"), this);
    m_clearBtn->setToolTip(tr("Clear the entire history"));
    m_clearBtn->setEnabled(false);
    m_clearBtn->setStyleSheet(t->historyPanelStyleSheet());
    headerRow->addWidget(m_clearBtn);

    mainLay->addLayout(headerRow);

    // ── List ──
    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName(QStringLiteral("historyPanelList"));
    m_listWidget->setAlternatingRowColors(false);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->setSpacing(1);
    mainLay->addWidget(m_listWidget, 1);

    // ── Separator ──
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorSurface.name()));
    mainLay->addWidget(sep);

    // ── Footer ──
    auto* footerRow = new QHBoxLayout();
    footerRow->setSpacing(t->spaceSM);

    m_undoBtn = new QPushButton(QString::fromUtf8("\342\206\251 Undo"), this);
    m_undoBtn->setToolTip(tr("Undo last action (Ctrl+Z)"));
    m_undoBtn->setEnabled(false);
    footerRow->addWidget(m_undoBtn);

    m_redoBtn = new QPushButton(QString::fromUtf8("\342\206\252 Redo"), this);
    m_redoBtn->setToolTip(tr("Redo last undone action (Ctrl+Y)"));
    m_redoBtn->setEnabled(false);
    footerRow->addWidget(m_redoBtn);

    footerRow->addStretch();

    m_countLabel = new QLabel(tr("0 of 0"), this);
    m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(t->colorTextSecondary.name())
        .arg(t->fontSizeSM));
    footerRow->addWidget(m_countLabel);

    mainLay->addLayout(footerRow);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyTheme();
        applyItemStyles();
    });
    applyTheme();

    // ── Connections ──
    connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int idx = m_listWidget->row(item);
        if (idx != m_currentIndex)
            emit jumpRequested(idx);
    });

    connect(m_undoBtn, &QPushButton::clicked, this, &HistoryPanel::undoRequested);
    connect(m_redoBtn, &QPushButton::clicked, this, &HistoryPanel::redoRequested);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        // Simple confirm dialog
        emit clearRequested();
    });
}

void HistoryPanel::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("HistoryPanel { background: %1; color: %2; }")
        .arg(th->colorSurface.name(), th->colorTextPrimary.name()));
    if (m_clearBtn)
        m_clearBtn->setStyleSheet(th->historyPanelStyleSheet());
    if (m_countLabel) {
        m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
            .arg(th->colorTextSecondary.name())
            .arg(th->fontSizeSM));
    }
}

void HistoryPanel::refresh(const QStringList& stateNames, int currentIndex)
{
    m_currentIndex = currentIndex;
    m_listWidget->clear();

    for (int i = 0; i < stateNames.size(); ++i) {
        QString displayName = formatDisplayName(stateNames[i]);
        auto* item = new QListWidgetItem(
            QString("%1. %2").arg(i + 1).arg(displayName),
            m_listWidget);
        m_listWidget->addItem(item);
    }

    applyItemStyles();

    // Scroll to current item
    if (currentIndex >= 0 && currentIndex < m_listWidget->count()) {
        m_listWidget->scrollToItem(m_listWidget->item(currentIndex),
                                   QAbstractItemView::EnsureVisible);
    }

    // Update footer
    int total = stateNames.size();
    bool hasHistory = total > 0;
    m_countLabel->setText(
        hasHistory
            ? tr("%1 of %2").arg(currentIndex + 1).arg(total)
            : tr("0 of 0"));

    m_undoBtn->setEnabled(currentIndex >= 0 && hasHistory);
    m_redoBtn->setEnabled(currentIndex + 1 < total);
    m_clearBtn->setEnabled(hasHistory);
}

void HistoryPanel::clear()
{
    m_listWidget->clear();
    m_currentIndex = -1;
    m_countLabel->setText(tr("0 of 0"));
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);
}

void HistoryPanel::applyItemStyles()
{
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto* item = m_listWidget->item(i);
        if (!item) continue;

        QFont f = item->font();
        QColor fg;
        QBrush bg(Qt::NoBrush);

        auto* th = ThemeManager::instance()->current();
        if (i < m_currentIndex) {
            fg = th->colorTextPrimary;
            f.setBold(false);
            f.setItalic(false);
        } else if (i == m_currentIndex) {
            fg = th->colorTextInverted;
            bg = QBrush(th->colorAccent);
            f.setBold(true);
            f.setItalic(false);
        } else {
            fg = th->colorTextDisabled;
            f.setBold(false);
            f.setItalic(true);
        }

        item->setForeground(fg);
        item->setBackground(bg);
        item->setFont(f);

        // Increase height for readability
        item->setSizeHint(QSize(0, 24));
    }
}

QString HistoryPanel::formatDisplayName(const QString& rawName)
{
    if (rawName.isEmpty()) return rawName;

    // Replace underscores with spaces
    QString name = rawName;
    name.replace('_', ' ');

    // Capitalize first letter of each word
    QStringList words = name.split(' ', Qt::SkipEmptyParts);
    for (auto& w : words) {
        if (!w.isEmpty()) {
            w[0] = w[0].toUpper();
        }
    }
    return words.join(' ');
}
