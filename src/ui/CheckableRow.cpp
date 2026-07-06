#include "CheckableRow.hpp"
#include "AppCheckBox.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>

CheckableRow::CheckableRow(const QString& text, QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);

    // Match the Layer Styles dialog's check rows: snug padding, a little breathing
    // room around the label (spaceSM/spaceXS), tight spacing.
    const Theme* t = ThemeManager::instance()->current();
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(t->spaceSM, t->spaceXS, t->spaceSM, t->spaceXS);
    lay->setSpacing(2);
    lay->setAlignment(Qt::AlignVCenter);

    m_check = new AppCheckBox(text, this);
    m_check->setLabelTogglesCheck(false);   // only the box toggles; label selects
    m_check->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    lay->addWidget(m_check);
    lay->addStretch();

    connect(m_check, &QCheckBox::toggled, this, [this](bool on) {
        applyVisualState();
        emit toggled(on);
    });
    // Clicking the indicator also selects the row (you are acting on this item).
    connect(m_check, &QCheckBox::clicked, this, [this](bool) { emit selected(); });

    applyVisualState();
}

void CheckableRow::setChecked(bool on)
{
    QSignalBlocker b(m_check);
    m_check->setChecked(on);
    applyVisualState();
}

bool CheckableRow::isChecked() const
{
    return m_check->isChecked();
}

void CheckableRow::setText(const QString& text)
{
    m_check->setText(text);
}

void CheckableRow::setSelected(bool on)
{
    if (m_selected == on)
        return;
    m_selected = on;
    applyVisualState();
}

void CheckableRow::setToggleable(bool on)
{
    // A non-toggleable row keeps its box checked + disabled, so mouse presses fall
    // through to this widget and still select the row.
    m_check->setEnabled(on);
}

void CheckableRow::applyVisualState()
{
    // The checkbox label turns white when the row is selected (same as the Layer
    // Styles dialog); otherwise it falls back to the themed text colour.
    if (m_check) {
        m_check->colors.textColor = m_selected ? QColor(Qt::white) : QColor();
        m_check->refreshStyle();
    }
    update();
}

void CheckableRow::mousePressEvent(QMouseEvent* e)
{
    // Reached when the press was on the row/label area: the AppCheckBox ignores
    // those (label-toggle is off) or is disabled, so select without toggling.
    emit selected();
    e->accept();
}

void CheckableRow::paintEvent(QPaintEvent*)
{
    const Theme* t = ThemeManager::instance()->current();
    if (!t)
        return;

    // Same palette as the Layer Styles check rows: a "pressed" fill once enabled,
    // a plain surface fill otherwise, and an accent border when selected.
    const QColor bg = isChecked() ? t->colorSurfacePressed : t->colorSurface;
    const QColor border = m_selected ? t->colorAccent : t->colorSurfacePressed;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                      t->radiusSM, t->radiusSM);
}
