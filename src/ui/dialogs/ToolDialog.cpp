#include "ToolDialog.hpp"
#include "ParamRow.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ui/AppCheckBox.hpp"
#include <QPushButton>
#include <QLabel>

ToolDialog::ToolDialog(const QString& title, Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
{
    setWindowTitle(title);
    setMinimumWidth(360);

    auto* t = ThemeManager::instance()->current();
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t->spaceLG);

    auto* contentArea = new QVBoxLayout;
    contentArea->setObjectName("contentArea");
    mainLayout->addLayout(contentArea);

    auto* previewRow = new QHBoxLayout;
    m_previewToggle = new AppCheckBox(tr("Preview"), this);
    m_previewToggle->setChecked(true);
    previewRow->addWidget(m_previewToggle);

    previewRow->addStretch();

    m_resetBtn = new QPushButton(tr("Reset"), this);
    m_resetBtn->setObjectName("cancelBtn");
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolDialog::onReset);
    previewRow->addWidget(m_resetBtn);

    mainLayout->addLayout(previewRow);

    if (m_mode == Mode::Adjustment) {
        auto* modeLabel = new QLabel(tr("Mode: Adjustment Layer"), this);
        modeLabel->setObjectName("infoLabel");
        modeLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(modeLabel);
    }

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setObjectName("cancelBtn");
    connect(m_cancelBtn, &QPushButton::clicked, this, &ToolDialog::onCancel);

    auto applyText = (m_mode == Mode::Adjustment) ? tr("Add Adjustment") : tr("Apply");
    m_applyBtn = new QPushButton(applyText, this);
    m_applyBtn->setObjectName("okBtn");

    connect(m_applyBtn, &QPushButton::clicked, this, &ToolDialog::onApply);

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_applyBtn);
    mainLayout->addLayout(btnLayout);
}

void ToolDialog::setDefaults(const QVariantMap& defaults)
{
    m_defaults = defaults;
}

void ToolDialog::markParamChanged()
{
    if (m_previewToggle->isChecked())
        emit previewChanged(collectParams());
}

void ToolDialog::onApply()
{
    emit confirmed(collectParams());
    accept();
}

void ToolDialog::onCancel()
{
    emit cancelled();
    reject();
}

void ToolDialog::resetToDefaults()
{
    const auto rows = findChildren<ParamRow*>();
    for (auto* row : rows)
        row->resetToDefault();
}

void ToolDialog::onReset()
{
    resetToDefaults();
    if (m_previewToggle->isChecked() && !m_defaults.isEmpty())
        emit previewChanged(m_defaults);
}
