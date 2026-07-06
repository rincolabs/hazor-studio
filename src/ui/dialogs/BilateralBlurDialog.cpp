#include "BilateralBlurDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>

BilateralBlurDialog::BilateralBlurDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Bilateral Blur"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_diameter   = new ParamRow(tr("Diameter"), 1.0, 50.0, 9.0, 0, false, this);
    m_sigmaColor = new ParamRow(tr("Sigma Color"), 1.0, 200.0, 75.0, 0, false, this);
    m_sigmaSpace = new ParamRow(tr("Sigma Space"), 1.0, 200.0, 75.0, 0, false, this);

    content->addWidget(m_diameter);
    content->addWidget(m_sigmaColor);
    content->addWidget(m_sigmaSpace);

    connect(m_diameter,   &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_sigmaColor, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_sigmaSpace, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap BilateralBlurDialog::collectParams() const
{
    return {
        {"diameter", m_diameter->value()},
        {"sigma_color", m_sigmaColor->value()},
        {"sigma_space", m_sigmaSpace->value()},
    };
}

void BilateralBlurDialog::onAnyChanged()
{
    markParamChanged();
}
