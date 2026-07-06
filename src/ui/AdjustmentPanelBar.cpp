#include "AdjustmentPanelBar.hpp"

#include "ui/IconUtils.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QToolButton>

namespace {

QToolButton* makeBarBtn(const QString& icon, const QString& tip, bool checkable)
{
    auto* b = new QToolButton();
    b->setIcon(makeIcon(icon));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setIconSize(QSize(24, 24));
    return b;
}

} // namespace

AdjustmentPanelBar::AdjustmentPanelBar(QWidget* parent)
    : QWidget(parent)
{
    auto* bottom = new QHBoxLayout(this);
    bottom->setContentsMargins(0, 0, 0, 0);

    m_clipBtn = makeBarBtn(QStringLiteral(":/icons/curves-clip.png"),
                           tr("Clip to layer (Single Layer Mode)"), true);
    m_previewBtn = makeBarBtn(QStringLiteral(":/icons/curves-preview.png"),
                              tr("Preview"), true);
    m_previewBtn->setChecked(true);
    m_resetBtn = makeBarBtn(QStringLiteral(":/icons/curves-reset.png"),
                            tr("Reset (Shift: reset all)"), false);
    m_visBtn = makeBarBtn(QStringLiteral(":/icons/curves-visibility.png"),
                          tr("Toggle layer visibility"), true);
    m_visBtn->setChecked(true);
    m_deleteBtn = makeBarBtn(QStringLiteral(":/icons/curves-delete.png"),
                             tr("Delete adjustment layer"), false);

    bottom->addWidget(m_clipBtn);
    bottom->addWidget(m_previewBtn);
    bottom->addStretch();
    bottom->addWidget(m_resetBtn);
    bottom->addWidget(m_visBtn);
    bottom->addWidget(m_deleteBtn);

    connect(m_clipBtn, &QToolButton::toggled,
            this, &AdjustmentPanelBar::clipToggled);
    connect(m_previewBtn, &QToolButton::toggled,
            this, &AdjustmentPanelBar::previewToggled);
    connect(m_visBtn, &QToolButton::toggled,
            this, &AdjustmentPanelBar::visibilityToggled);
    connect(m_deleteBtn, &QToolButton::clicked,
            this, &AdjustmentPanelBar::deleteClicked);
    connect(m_resetBtn, &QToolButton::clicked, this, [this]() {
        emit resetClicked(
            QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
    });
}

void AdjustmentPanelBar::setClipChecked(bool single)
{
    m_clipBtn->blockSignals(true);
    m_clipBtn->setChecked(single);
    m_clipBtn->blockSignals(false);
}

void AdjustmentPanelBar::setPreviewChecked(bool on)
{
    m_previewBtn->blockSignals(true);
    m_previewBtn->setChecked(on);
    m_previewBtn->blockSignals(false);
}

void AdjustmentPanelBar::setVisibilityChecked(bool visible)
{
    m_visBtn->blockSignals(true);
    m_visBtn->setChecked(visible);
    m_visBtn->blockSignals(false);
}

bool AdjustmentPanelBar::isClipChecked() const { return m_clipBtn->isChecked(); }
bool AdjustmentPanelBar::isPreviewChecked() const { return m_previewBtn->isChecked(); }
bool AdjustmentPanelBar::isVisibilityChecked() const { return m_visBtn->isChecked(); }
