#include "MotionBlurDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>

MotionBlurDialog::MotionBlurDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Motion Blur"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_length = new ParamRow(tr("Length"), 1.0, 200.0, 15.0, 0, false, this);
    m_angle  = new ParamRow(tr("Angle"), 0.0, 360.0, 0.0, 0, false, this);

    content->addWidget(m_length);
    content->addWidget(m_angle);

    connect(m_length, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_angle,  &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap MotionBlurDialog::collectParams() const
{
    return {
        {"length", m_length->value()},
        {"angle", m_angle->value()},
    };
}

void MotionBlurDialog::onAnyChanged()
{
    markParamChanged();
}
