#include "RemoveBgDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QFormLayout>

RemoveBgDialog::RemoveBgDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Remove Background"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    auto* form = new QFormLayout;
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Simple (dominant color)"), QString("simple"));
    m_modeCombo->addItem(tr("AI (future)"), QString("ai"));
    m_modeCombo->setCurrentIndex(0);
    form->addRow(tr("Mode:"), m_modeCombo);

    content->addLayout(form);

    m_threshold = new ParamRow(tr("Threshold"), 0.0, 100.0, 20.0, 0, false, this);
    m_feather = new ParamRow(tr("Feather"), 0.0, 50.0, 5.0, 0, false, this);

    content->addWidget(m_threshold);
    content->addWidget(m_feather);

    auto* hint = new QLabel(tr("Lower threshold = more aggressive removal"), this);
    hint->setObjectName("infoLabel");
    content->addWidget(hint);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { onAnyChanged(); });
    connect(m_threshold, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });
    connect(m_feather, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap RemoveBgDialog::collectParams() const
{
    return {
        {"mode", static_cast<double>(m_modeCombo->currentIndex())},
        {"threshold", m_threshold->value()},
        {"feather", m_feather->value()}
    };
}

void RemoveBgDialog::resetToDefaults()
{
    ToolDialog::resetToDefaults();
    m_modeCombo->setCurrentIndex(0);
}

void RemoveBgDialog::onAnyChanged()
{
    markParamChanged();
}
