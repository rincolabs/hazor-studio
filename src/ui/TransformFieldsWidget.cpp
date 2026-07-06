#include "TransformFieldsWidget.hpp"

#include "IconUtils.hpp"
#include "ScrubbableValueInput.h"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDoubleSpinBox>
#include <QAbstractSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>

static QLabel* makeTransformLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    auto* t = ThemeManager::instance()->current();
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                             .arg(t->colorTextSecondary.name()));
    return label;
}

static QPushButton* makeLinkButton(QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setIcon(makeIcon(QStringLiteral(":/icons/constrain-proportions.png")));
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(28, 24);
    button->setCheckable(true);
    button->setChecked(true);
    button->setToolTip(QObject::tr("Link width and height (constrain proportions)"));
    return button;
}

TransformFieldsWidget::TransformFieldsWidget(TransformFieldsMode mode, QWidget* parent)
    : QWidget(parent)
    , m_mode(mode)
{
    auto* t = ThemeManager::instance()->current();

    m_wInput = makeInput(1.0, 100000.0, 2, tr(" px"), this);
    m_hInput = makeInput(1.0, 100000.0, 2, tr(" px"), this);
    m_xInput = makeInput(-100000.0, 100000.0, 2, tr(" px"), this);
    m_yInput = makeInput(-100000.0, 100000.0, 2, tr(" px"), this);
    m_rotInput = new ScrubbableValueInput(QString(), -360.0, 360.0, 0.0,
                                          QString::fromUtf8("°"), 1.0, this);
    m_rotInput->setDecimals(2);
    m_linkButton = makeLinkButton(this);

    if (m_mode == TransformFieldsMode::OptionsBar) {
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(5);

        row->addWidget(makeTransformLabel(tr("X:"), this));
        row->addWidget(m_xInput);
        row->addWidget(makeTransformLabel(tr("Y:"), this));
        row->addWidget(m_yInput);
        row->addSpacing(t->spaceSM);
        row->addWidget(makeTransformLabel(tr("W:"), this));
        row->addWidget(m_wInput);
        row->addWidget(m_linkButton);
        row->addWidget(makeTransformLabel(tr("H:"), this));
        row->addWidget(m_hInput);
        row->addSpacing(t->spaceSM);
        row->addWidget(makeTransformLabel(QString::fromUtf8("∠"), this));
        row->addWidget(m_rotInput);

        for (auto* input : { m_xInput, m_yInput, m_wInput, m_hInput })
            input->setFixedWidth(92);
        m_rotInput->setFixedWidth(92);
    } else {
        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(t->spaceSM);
        grid->setVerticalSpacing(t->spaceXS);
        grid->setSizeConstraint(QLayout::SetFixedSize);
        grid->addWidget(m_linkButton, 0, 0, 2, 1);
        grid->addWidget(makeTransformLabel(tr("W:"), this), 0, 1);
        grid->addWidget(m_wInput, 0, 2, Qt::AlignLeft);
        grid->addWidget(makeTransformLabel(tr("X:"), this), 0, 3);
        grid->addWidget(m_xInput, 0, 4, Qt::AlignLeft);
        grid->addWidget(makeTransformLabel(tr("H:"), this), 1, 1);
        grid->addWidget(m_hInput, 1, 2, Qt::AlignLeft);
        grid->addWidget(makeTransformLabel(tr("Y:"), this), 1, 3);
        grid->addWidget(m_yInput, 1, 4, Qt::AlignLeft);
        grid->addWidget(makeTransformLabel(QString::fromUtf8("∠"), this), 2, 1);
        grid->addWidget(m_rotInput, 2, 2, 1, 3, Qt::AlignLeft);
        for (auto* input : { m_wInput, m_hInput, m_xInput, m_yInput }) {
            input->setMaximumWidth(100);
            input->setMinimumWidth(100);
        }
        m_rotInput->setMaximumWidth(100);
        m_rotInput->setMinimumWidth(100);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(sizeHint());
    }

    connect(m_wInput, &QDoubleSpinBox::editingFinished, this, [this]() {
        emitCommitted(FieldWidth, m_wInput);
    });
    connect(m_hInput, &QDoubleSpinBox::editingFinished, this, [this]() {
        emitCommitted(FieldHeight, m_hInput);
    });
    connect(m_xInput, &QDoubleSpinBox::editingFinished, this, [this]() {
        emitCommitted(FieldX, m_xInput);
    });
    connect(m_yInput, &QDoubleSpinBox::editingFinished, this, [this]() {
        emitCommitted(FieldY, m_yInput);
    });
    connect(m_rotInput, &ScrubbableValueInput::editingFinished, this, [this](double value) {
        if (!m_rotInput || !m_rotInput->isEnabled())
            return;
        emit fieldEdited(FieldRotation, value);
    });
}

bool TransformFieldsWidget::proportionsLocked() const
{
    return m_linkButton && m_linkButton->isChecked();
}

void TransformFieldsWidget::setTransformValues(double widthPx, double heightPx,
                                               double posX, double posY,
                                               double rotationDeg)
{
    QSignalBlocker b1(m_wInput);
    QSignalBlocker b2(m_hInput);
    QSignalBlocker b3(m_xInput);
    QSignalBlocker b4(m_yInput);
    QSignalBlocker b5(m_rotInput);

    m_wInput->setValue(widthPx);
    m_hInput->setValue(heightPx);
    m_xInput->setValue(posX);
    m_yInput->setValue(posY);
    m_rotInput->setValue(rotationDeg);
}

void TransformFieldsWidget::setFieldsEnabled(bool enabled)
{
    m_wInput->setEnabled(enabled);
    m_hInput->setEnabled(enabled);
    m_xInput->setEnabled(enabled);
    m_yInput->setEnabled(enabled);
    m_rotInput->setEnabled(enabled);
    m_linkButton->setEnabled(enabled);
}

QDoubleSpinBox* TransformFieldsWidget::makeInput(double min, double max,
                                                 int decimals,
                                                 const QString& suffix,
                                                 QWidget* parent)
{
    auto* input = new QDoubleSpinBox(parent);
    input->setRange(min, max);
    input->setDecimals(decimals);
    input->setSingleStep(1.0);
    input->setSuffix(suffix);
    input->setButtonSymbols(QAbstractSpinBox::NoButtons);
    input->setKeyboardTracking(false);
    input->setAlignment(Qt::AlignRight);
    input->setFixedHeight(24);
    return input;
}

void TransformFieldsWidget::emitCommitted(int field, QDoubleSpinBox* input)
{
    if (!input || !input->isEnabled())
        return;
    emit fieldEdited(field, input->value());
}
