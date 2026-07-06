#include "PosterizeDialog.hpp"
#include "ParamRow.hpp"

#include <QVBoxLayout>
#include <QLabel>

PosterizeDialog::PosterizeDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Posterize"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    m_levels = new ParamRow(tr("Levels"), 2.0, 32.0, 8.0, 0, false, this);

    auto* hint = new QLabel(tr("Lower values = more posterization"), this);
    hint->setObjectName("infoLabel");

    content->addWidget(m_levels);
    content->addWidget(hint);

    connect(m_levels, &ParamRow::valueChanged, this, [this]() { onAnyChanged(); });

    setDefaults(collectParams());
}

QVariantMap PosterizeDialog::collectParams() const
{
    return {{"levels", m_levels->value()}};
}

void PosterizeDialog::onAnyChanged()
{
    markParamChanged();
}
