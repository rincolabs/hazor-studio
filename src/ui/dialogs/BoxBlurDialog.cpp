#include "BoxBlurDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>

BoxBlurDialog::BoxBlurDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Box Blur"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_radius = new ParamRow(tr("Radius"), 0.0, 100.0, 3.0, 0, false, this);

    content->addWidget(m_radius);

    connect(m_radius, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap BoxBlurDialog::collectParams() const
{
    return {{"radius", m_radius->value()}};
}

void BoxBlurDialog::onAnyChanged()
{
    markParamChanged();
}
