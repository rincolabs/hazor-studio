#include "RadialBlurDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>

RadialBlurDialog::RadialBlurDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Radial Blur"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_amount  = new ParamRow(tr("Amount"), 0.0, 1.0, 0.25, 2, false, this);
    m_samples = new ParamRow(tr("Samples"), 2.0, 64.0, 12.0, 0, false, this);
    m_centerX = new ParamRow(tr("Center X"), 0.0, 1.0, 0.5, 2, false, this);
    m_centerY = new ParamRow(tr("Center Y"), 0.0, 1.0, 0.5, 2, false, this);

    content->addWidget(m_amount);
    content->addWidget(m_samples);
    content->addWidget(m_centerX);
    content->addWidget(m_centerY);

    connect(m_amount,  &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_samples, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_centerX, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_centerY, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap RadialBlurDialog::collectParams() const
{
    return {
        {"amount", m_amount->value()},
        {"samples", m_samples->value()},
        {"center_x", m_centerX->value()},
        {"center_y", m_centerY->value()},
    };
}

void RadialBlurDialog::onAnyChanged()
{
    markParamChanged();
}
