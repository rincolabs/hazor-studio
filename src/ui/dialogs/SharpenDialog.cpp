#include "SharpenDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>

SharpenDialog::SharpenDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Sharpen"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_strength = new ParamRow(tr("Strength"), 0.0, 5.0, 1.0, 1, false, this);

    content->addWidget(m_strength);

    connect(m_strength, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap SharpenDialog::collectParams() const
{
    return {{"strength", m_strength->value()}};
}

void SharpenDialog::onAnyChanged()
{
    markParamChanged();
}
