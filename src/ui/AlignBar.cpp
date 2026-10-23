#include "AlignBar.hpp"
#include "FlowLayout.hpp"
#include "IconUtils.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

AlignBar::AlignBar(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(title);

    // The FlowLayout wraps; let the widget's height track its width so wrapped
    // rows aren't clipped by the enclosing vertical layout.
    QSizePolicy sp = sizePolicy();
    sp.setHeightForWidth(true);
    setSizePolicy(sp);

    auto* t = ThemeManager::instance()->current();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(t->spaceSM);

    // "Align:" label
    auto* label = new QLabel(tr("Align:"), this);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextBright.name()));
    root->addWidget(label);

    // Keep the controls in their own row, matching the Properties panel's
    // label-above-controls pattern. FlowLayout preserves wrapping in narrow docks.
    auto* buttons = new FlowLayout(0, 4, 4);
    // Align target combo
    m_alignTargetCombo = new QComboBox(this);
    m_alignTargetCombo->addItem(tr("Canvas"),    0);
    m_alignTargetCombo->addItem(tr("Selection"), 1);
    m_alignTargetCombo->setFixedWidth(100);

    auto makeBtn = [&](const QString& iconName, const QString& tooltip) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(":/icons/" + iconName + ".png"));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(28, 24);
        btn->setToolTip(tooltip);
        btn->setEnabled(false);
        return btn;
    };

    m_alignTop      = makeBtn("align-vertical-top",      tr("Align top edges"));
    m_alignMiddleV  = makeBtn("align-vertical-center",   tr("Align vertical centers"));
    m_alignBottom   = makeBtn("align-vertical-bottom",   tr("Align bottom edges"));
    m_alignLeft     = makeBtn("align-horizontal-left",   tr("Align left edges"));
    m_alignCenterH  = makeBtn("align-horizontal-center", tr("Align horizontal centers"));
    m_alignRight    = makeBtn("align-horizontal-right",  tr("Align right edges"));

    m_alignCenterBoth = new QPushButton(this);
    m_alignCenterBoth->setIcon(makeIcon(":/icons/align-center-center.png"));
    m_alignCenterBoth->setIconSize(QSize(24, 24));
    m_alignCenterBoth->setFixedSize(28, 24);
    m_alignCenterBoth->setToolTip(tr("Center horizontally and vertically"));
    m_alignCenterBoth->setEnabled(false);

    buttons->addWidget(m_alignTop);
    buttons->addWidget(m_alignMiddleV);
    buttons->addWidget(m_alignBottom);
    buttons->addWidget(m_alignLeft);
    buttons->addWidget(m_alignCenterH);
    buttons->addWidget(m_alignRight);
    buttons->addWidget(m_alignCenterBoth);

    m_resetTransformBtn = new QPushButton(this);
    m_resetTransformBtn->setIcon(makeIcon(":/icons/align-reset.png"));
    m_resetTransformBtn->setIconSize(QSize(24, 24));
    m_resetTransformBtn->setFixedSize(26, 26);
    m_resetTransformBtn->setToolTip(tr("Reset transform (position, scale and rotation)"));
    m_resetTransformBtn->setEnabled(false);
    buttons->addWidget(m_resetTransformBtn);
    root->addLayout(buttons);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::NoFrame);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: %1; border: none;")
                               .arg(t->colorBorder.name()));
    auto* dividerWrap = new QWidget(this);
    auto* dividerLayout = new QVBoxLayout(dividerWrap);
    dividerLayout->setContentsMargins(0, t->spaceSM, 0, t->spaceSM);
    dividerLayout->setSpacing(0);
    dividerLayout->addWidget(divider);
    root->addWidget(dividerWrap);

    auto* targetRow = new QHBoxLayout();
    targetRow->setContentsMargins(0, 0, 0, 0);
    targetRow->setSpacing(t->spaceSM);
    auto* targetLabel = new QLabel(tr("Align To:"), this);
    targetLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                   .arg(t->colorTextBright.name()));
    targetRow->addWidget(targetLabel);
    targetRow->addWidget(m_alignTargetCombo);
    targetRow->addStretch();
    root->addLayout(targetRow);

    // Connections
    connect(m_alignTop,        &QPushButton::clicked, this, &AlignBar::alignTopClicked);
    connect(m_alignMiddleV,    &QPushButton::clicked, this, &AlignBar::alignMiddleVClicked);
    connect(m_alignBottom,     &QPushButton::clicked, this, &AlignBar::alignBottomClicked);
    connect(m_alignLeft,       &QPushButton::clicked, this, &AlignBar::alignLeftClicked);
    connect(m_alignCenterH,    &QPushButton::clicked, this, &AlignBar::alignCenterHClicked);
    connect(m_alignRight,      &QPushButton::clicked, this, &AlignBar::alignRightClicked);
    connect(m_alignCenterBoth, &QPushButton::clicked, this, &AlignBar::alignCenterClicked);
    connect(m_resetTransformBtn, &QPushButton::clicked, this, &AlignBar::resetTransformClicked);
    connect(m_alignTargetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AlignBar::alignTargetChanged);
}

void AlignBar::updateButtons(bool hasLayer)
{
    m_alignTop->setEnabled(hasLayer);
    m_alignMiddleV->setEnabled(hasLayer);
    m_alignBottom->setEnabled(hasLayer);
    m_alignLeft->setEnabled(hasLayer);
    m_alignCenterH->setEnabled(hasLayer);
    m_alignRight->setEnabled(hasLayer);
    m_alignCenterBoth->setEnabled(hasLayer);
    m_resetTransformBtn->setEnabled(hasLayer);
}
