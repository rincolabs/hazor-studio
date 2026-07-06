#include "AdjustColorDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include "ui/AppCheckBox.hpp"

AdjustColorDialog::AdjustColorDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Color Adjustments"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) {
        content = new QVBoxLayout;
    }

    auto onChanged = [this]() { onAnyChanged(); };

    m_brightness = new ParamRow(tr("Brightness"), -1.0, 1.0, 0.0, 2, false, this);
    m_contrast = new ParamRow(tr("Contrast"), -1.0, 1.0, 0.0, 2, false, this);
    m_saturation = new ParamRow(tr("Saturation"), -1.0, 1.0, 0.0, 2, false, this);
    m_hue = new ParamRow(tr("Hue"), -1.0, 1.0, 0.0, 2, false, this);

    m_autoContrast = new AppCheckBox(tr("Auto Contrast"), this);

    content->addWidget(m_brightness);
    content->addWidget(m_contrast);
    content->addWidget(m_saturation);
    content->addWidget(m_hue);
    content->addWidget(m_autoContrast);

    connect(m_brightness, &ParamRow::valueChanged, this, onChanged);
    connect(m_contrast, &ParamRow::valueChanged, this, onChanged);
    connect(m_saturation, &ParamRow::valueChanged, this, onChanged);
    connect(m_hue, &ParamRow::valueChanged, this, onChanged);
    connect(m_autoContrast, &QCheckBox::toggled, this, onChanged);

    setDefaults(collectParams());
}

QVariantMap AdjustColorDialog::collectParams() const
{
    return {
        {"brightness", m_brightness->value()},
        {"contrast", m_contrast->value()},
        {"saturation", m_saturation->value()},
        {"hue", m_hue->value()},
        {"auto_contrast", m_autoContrast->isChecked() ? 1.0 : 0.0}
    };
}

void AdjustColorDialog::resetToDefaults()
{
    ToolDialog::resetToDefaults();
    m_autoContrast->setChecked(false);
}

void AdjustColorDialog::onAnyChanged()
{
    markParamChanged();
}
