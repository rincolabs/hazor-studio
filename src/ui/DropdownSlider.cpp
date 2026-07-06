#include "DropdownSlider.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/Theme.hpp"

#include <QHBoxLayout>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>
#include <QWidgetAction>

DropdownSlider::DropdownSlider(const QString& label,
                               int minValue,
                               int maxValue,
                               int value,
                               const QString& suffix,
                               int singleStep,
                               int popupWidth,
                               QWidget* parent)
    : QToolButton(parent)
    , m_label(label)
    , m_suffix(suffix)
{
    auto* t = ThemeManager::instance()->current();
    setPopupMode(QToolButton::InstantPopup);
    setAutoRaise(false);
    setStyleSheet(QStringLiteral(
        "QToolButton { padding: %1px %2px; font-size: %3px; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }")
        .arg(t->spaceSM)
        .arg(t->spaceSM)
        .arg(t->fontSizeMD));

    m_menu = new QMenu(this);
    m_action = new QWidgetAction(m_menu);

    auto* popup = new QWidget(m_menu);
    popup->setMinimumWidth(popupWidth);
    auto* row = new QHBoxLayout(popup);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(4);

    m_slider = new QSlider(Qt::Horizontal, popup);
    m_slider->setRange(minValue, maxValue);
    m_slider->setSingleStep(singleStep);
    m_slider->setPageStep(std::max(singleStep, (maxValue - minValue) / 10));
    m_slider->setValue(value);

    m_spin = new QSpinBox(popup);
    m_spin->setRange(minValue, maxValue);
    m_spin->setSingleStep(singleStep);
    m_spin->setSuffix(m_suffix);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(72);
    m_spin->setValue(value);

    row->addWidget(m_slider, 1);
    row->addWidget(m_spin, 0);

    m_action->setDefaultWidget(popup);
    m_menu->addAction(m_action);
    setMenu(m_menu);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (m_spin->value() != v) {
            m_spin->blockSignals(true);
            m_spin->setValue(v);
            m_spin->blockSignals(false);
        }
        updateButtonText();
        emit valueChanged(v);
    });
    connect(m_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_slider->value() != v) {
            m_slider->blockSignals(true);
            m_slider->setValue(v);
            m_slider->blockSignals(false);
        }
        updateButtonText();
        emit valueChanged(v);
    });

    updateButtonText();
}

int DropdownSlider::value() const
{
    return m_slider ? m_slider->value() : 0;
}

void DropdownSlider::setValue(int value)
{
    if (!m_slider || !m_spin)
        return;
    if (m_slider->value() == value && m_spin->value() == value)
        return;
    m_slider->blockSignals(true);
    m_spin->blockSignals(true);
    m_slider->setValue(value);
    m_spin->setValue(value);
    m_spin->blockSignals(false);
    m_slider->blockSignals(false);
    updateButtonText();
}

void DropdownSlider::setRange(int minValue, int maxValue)
{
    if (!m_slider || !m_spin)
        return;
    m_slider->setRange(minValue, maxValue);
    m_spin->setRange(minValue, maxValue);
    updateButtonText();
}

void DropdownSlider::setSuffix(const QString& suffix)
{
    m_suffix = suffix;
    if (m_spin)
        m_spin->setSuffix(m_suffix);
    updateButtonText();
}

void DropdownSlider::setLabel(const QString& label)
{
    m_label = label;
    updateButtonText();
}

void DropdownSlider::updateButtonText()
{
    const QString valueText = QString::number(value()) + m_suffix;
    setText(QStringLiteral("%1: %2 ▼").arg(m_label, valueText));
}
