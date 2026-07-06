#include "GaussianBlurDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>

GaussianBlurDialog::GaussianBlurDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Gaussian Blur"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_radius = new ParamRow(tr("Radius"), 0.0, 100.0, 3.0, 1, true, this);

    content->addWidget(m_radius);

    connect(m_radius, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap GaussianBlurDialog::collectParams() const
{
    return {{"radius", m_radius->value()}};
}

void GaussianBlurDialog::onAnyChanged()
{
    markParamChanged();
}
