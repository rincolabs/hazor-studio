#pragma once

#include <QWidget>

class AppCheckBox;
class QMouseEvent;
class QPaintEvent;

// A selectable list row that wraps an AppCheckBox. The indicator box toggles the
// check (e.g. enable a feature); clicking anywhere else on the row selects it (to
// open its page for editing) WITHOUT toggling the check. Emits toggled() for the
// indicator and selected() for the row, and paints a highlight when selected.
//
// The split is what lets the brush panel's Funcionalidades / Sensores columns act
// like list rows: pick a row to edit its props without changing whether it is on.
class CheckableRow : public QWidget {
    Q_OBJECT
public:
    explicit CheckableRow(const QString& text, QWidget* parent = nullptr);

    void setChecked(bool on);              // no signal
    bool isChecked() const;
    void setText(const QString& text);
    void setSelected(bool on);             // visual highlight only
    bool isSelected() const { return m_selected; }

    // Some rows are always-on (no enable concept): the indicator is shown checked
    // and disabled, but the row stays selectable.
    void setToggleable(bool on);

    AppCheckBox* checkBox() const { return m_check; }

signals:
    void toggled(bool on);   // indicator toggled (feature/sensor enable)
    void selected();         // row clicked (activate / edit this row)

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    void applyVisualState();   // row fill/border + label colour for checked/selected

    AppCheckBox* m_check = nullptr;
    bool m_selected = false;
};
