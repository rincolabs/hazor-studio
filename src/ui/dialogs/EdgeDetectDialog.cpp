#include "EdgeDetectDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include "ui/AppCheckBox.hpp"

EdgeDetectDialog::EdgeDetectDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Edge Detection (Canny)"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_threshold1 = new ParamRow(tr("Threshold 1"), 0.0, 500.0, 50.0, 0, false, this);
    m_threshold2 = new ParamRow(tr("Threshold 2"), 0.0, 500.0, 150.0, 0, false, this);

    m_linkThresholds = new AppCheckBox(tr("Link thresholds (T2 = T1 × 2)"), this);

    content->addWidget(m_threshold1);
    content->addWidget(m_threshold2);
    content->addWidget(m_linkThresholds);

    connect(m_threshold1, &ParamRow::valueChanged, this, &EdgeDetectDialog::onThreshold1Changed);
    connect(m_threshold2, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_linkThresholds, &QCheckBox::toggled, this, [this]() {
        if (m_linkThresholds->isChecked())
            onThreshold1Changed(m_threshold1->value());
        else
            onAnyChanged();
    });

    setDefaults(collectParams());
}

QVariantMap EdgeDetectDialog::collectParams() const
{
    return {
        {"threshold1", m_threshold1->value()},
        {"threshold2", m_threshold2->value()}
    };
}

void EdgeDetectDialog::onThreshold1Changed(double val)
{
    if (m_linkThresholds->isChecked()) {
        double linked = std::min(val * 2.0, 500.0);
        m_threshold2->setValue(linked);
    }
    onAnyChanged();
}

void EdgeDetectDialog::resetToDefaults()
{
    ToolDialog::resetToDefaults();
    m_linkThresholds->setChecked(false);
}

void EdgeDetectDialog::onAnyChanged()
{
    markParamChanged();
}
