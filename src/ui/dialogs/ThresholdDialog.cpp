#include "ThresholdDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include "ui/AppCheckBox.hpp"
#include <QLabel>

ThresholdDialog::ThresholdDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Threshold"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_value = new ParamRow(tr("Value"), 0.0, 255.0, 128.0, 0, false, this);
    m_adaptive = new AppCheckBox(tr("Adaptive (future)"), this);
    m_adaptive->setEnabled(false);

    auto* hint = new QLabel(tr("Pixels above value become white, below become black"), this);
    hint->setObjectName("infoLabel");

    content->addWidget(m_value);
    content->addWidget(m_adaptive);
    content->addWidget(hint);

    connect(m_value, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap ThresholdDialog::collectParams() const
{
    return {
        {"value", m_value->value()},
        {"adaptive", m_adaptive->isChecked() ? 1.0 : 0.0}
    };
}

void ThresholdDialog::onAnyChanged()
{
    markParamChanged();
}
