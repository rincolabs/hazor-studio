#include "FillLayerDialog.hpp"
#include "ParamRow.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/Theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>

FillLayerDialog::FillLayerDialog(ToolDialog::Mode mode, QWidget* parent)
    : ToolDialog(tr("Fill Layer"), mode, parent)
{
    auto* content = findChild<QVBoxLayout*>("contentArea");
    if (!content) content = new QVBoxLayout;

    auto* colorGroup = new QGroupBox(tr("Color"), this);
    auto* groupLayout = new QVBoxLayout(colorGroup);

    m_red = new ParamRow(tr("Red"), 0.0, 255.0, 255.0, 0, false, this);
    m_green = new ParamRow(tr("Green"), 0.0, 255.0, 255.0, 0, false, this);
    m_blue = new ParamRow(tr("Blue"), 0.0, 255.0, 255.0, 0, false, this);
    m_alpha = new ParamRow(tr("Alpha"), 0.0, 255.0, 255.0, 0, false, this);

    groupLayout->addWidget(m_red);
    groupLayout->addWidget(m_green);
    groupLayout->addWidget(m_blue);
    groupLayout->addWidget(m_alpha);

    content->addWidget(colorGroup);

    auto* previewGroup = new QGroupBox(tr("Preview"), this);
    auto* pLayout = new QVBoxLayout(previewGroup);
    m_preview = new QFrame(this);
    m_preview->setFixedSize(200, 60);
    m_preview->setFrameShape(QFrame::StyledPanel);
    pLayout->addWidget(m_preview, 0, Qt::AlignCenter);
    content->addWidget(previewGroup);

    auto onChanged = [this]() { onAnyChanged(); };
    connect(m_red, &ParamRow::valueChanged, this, onChanged);
    connect(m_green, &ParamRow::valueChanged, this, onChanged);
    connect(m_blue, &ParamRow::valueChanged, this, onChanged);
    connect(m_alpha, &ParamRow::valueChanged, this, onChanged);

    setDefaults(collectParams());
    updateColorPreview();
}

QVariantMap FillLayerDialog::collectParams() const
{
    return {
        {"red", m_red->value()},
        {"green", m_green->value()},
        {"blue", m_blue->value()},
        {"alpha", m_alpha->value()}
    };
}

QColor FillLayerDialog::selectedColor() const
{
    return QColor(
        static_cast<int>(m_red->value()),
        static_cast<int>(m_green->value()),
        static_cast<int>(m_blue->value()),
        static_cast<int>(m_alpha->value())
    );
}

void FillLayerDialog::updateColorPreview()
{
    QColor c = selectedColor();
    auto* t = ThemeManager::instance()->current();
    m_preview->setStyleSheet(
        QString("background: rgba(%1,%2,%3,%4); border: 1px solid %5; border-radius: %6px;")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
            .arg(t->colorBorder.name())
            .arg(t->radiusMD));
}

void FillLayerDialog::onAnyChanged()
{
    updateColorPreview();
    markParamChanged();
}
