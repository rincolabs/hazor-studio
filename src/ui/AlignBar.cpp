#include "AlignBar.hpp"
#include "FlowLayout.hpp"
#include "IconUtils.hpp"
#include "ToolBarSeparator.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QFrame>

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
    // FlowLayout so the row wraps to the next line on narrow panels instead of
    // forcing the panel wider (QHBoxLayout never wraps).
    auto* lay = new FlowLayout(this, 0, 4, 4);

    // "Align:" label
    auto* label = new QLabel(tr("Align:"), this);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextBright.name()));
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

    lay->addWidget(m_alignTop);
    lay->addWidget(m_alignMiddleV);
    lay->addWidget(m_alignBottom);

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(m_alignLeft);
    lay->addWidget(m_alignCenterH);
    lay->addWidget(m_alignRight);

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(m_alignCenterBoth);

    // Separator before Reset
    auto* sepReset = new QFrame(this);
    sepReset->setFrameShape(QFrame::VLine);
    sepReset->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorBorder.name()));
    sepReset->setFixedWidth(2);
    lay->addWidget(sepReset);

    m_resetTransformBtn = new QPushButton(this);
    m_resetTransformBtn->setIcon(makeIcon(":/icons/align-reset.png"));
    m_resetTransformBtn->setIconSize(QSize(24, 24));
    m_resetTransformBtn->setFixedSize(26, 26);
    m_resetTransformBtn->setToolTip(tr("Reset transform (position, scale and rotation)"));
    m_resetTransformBtn->setEnabled(false);
    lay->addWidget(m_resetTransformBtn);

    lay->addWidget(new ToolBarSeparator(this));
    lay->addWidget(label);
    lay->addWidget(m_alignTargetCombo);

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
