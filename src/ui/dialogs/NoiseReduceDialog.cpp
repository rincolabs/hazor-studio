#include "NoiseReduceDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include <QLabel>

NoiseReduceDialog::NoiseReduceDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Noise Reduce"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_strength = new ParamRow(tr("Strength"), 0.0, 10.0, 2.0, 1, false, this);
    m_preserveEdges = new ParamRow(tr("Preserve Edges"), 0.0, 1.0, 0.5, 2, false, this);

    auto* hint = new QLabel(tr("Higher strength removes more noise but may blur details"), this);
    hint->setObjectName("infoLabel");

    content->addWidget(m_strength);
    content->addWidget(m_preserveEdges);
    content->addWidget(hint);

    connect(m_strength, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_preserveEdges, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap NoiseReduceDialog::collectParams() const
{
    return {
        {"strength", m_strength->value()},
        {"preserve_edges", m_preserveEdges->value()}
    };
}

void NoiseReduceDialog::onAnyChanged()
{
    markParamChanged();
}
