#include "MedianBlurDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include <QLabel>

MedianBlurDialog::MedianBlurDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Median Blur"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_kernelSize = new ParamRow(tr("Kernel (odd)"), 1.0, 31.0, 5.0, 0, false, this);

    auto* hint = new QLabel(tr("Automatically rounded to nearest odd number"), this);
    hint->setObjectName("infoLabel");

    content->addWidget(m_kernelSize);
    content->addWidget(hint);

    connect(m_kernelSize, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap MedianBlurDialog::collectParams() const
{
    int ks = static_cast<int>(m_kernelSize->value());
    if (ks % 2 == 0) ks = std::clamp(ks + 1, 1, 31);
    return {{"kernel_size", static_cast<double>(ks)}};
}

void MedianBlurDialog::onAnyChanged()
{
    markParamChanged();
}
