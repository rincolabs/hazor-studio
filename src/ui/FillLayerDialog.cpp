#include "FillLayerDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QSpinBox>
#include <QFrame>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>

FillLayerDialog::FillLayerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Fill Layer"));
    setFixedSize(360, 380);
    setupUi();
    updatePreview();
}

QColor FillLayerDialog::selectedColor() const
{
    return QColor(m_redSpin->value(), m_greenSpin->value(),
                  m_blueSpin->value(), m_alphaSpin->value());
}

void FillLayerDialog::setupUi()
{
    auto* t = ThemeManager::instance()->current();
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t->spaceLG);

    auto* colorGroup = new QGroupBox(tr("Color"), this);
    auto* formLayout = new QFormLayout(colorGroup);
    formLayout->setSpacing(t->spaceMD);

    auto makeSlider = [&](const QString& label, int defaultValue) -> QPair<QSlider*, QSpinBox*> {
        auto* row = new QHBoxLayout;
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, 255);
        slider->setValue(defaultValue);

        auto* spin = new QSpinBox(this);
        spin->setRange(0, 255);
        spin->setValue(defaultValue);

        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, this, &FillLayerDialog::updatePreview);

        row->addWidget(slider);
        row->addWidget(spin);
        formLayout->addRow(label, row);

        return {slider, spin};
    };

    auto [rs, rsp] = makeSlider(tr("Red:"), 255);
    auto [gs, gsp] = makeSlider(tr("Green:"), 255);
    auto [bs, bsp] = makeSlider(tr("Blue:"), 255);
    auto [as, asp] = makeSlider(tr("Alpha:"), 255);

    m_redSlider = rs; m_redSpin = rsp;
    m_greenSlider = gs; m_greenSpin = gsp;
    m_blueSlider = bs; m_blueSpin = bsp;
    m_alphaSlider = as; m_alphaSpin = asp;

    mainLayout->addWidget(colorGroup);

    auto* previewGroup = new QGroupBox(tr("Preview"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);

    m_preview = new QFrame(this);
    m_preview->setFixedSize(200, 60);
    m_preview->setFrameShape(QFrame::StyledPanel);
    previewLayout->addWidget(m_preview, 0, Qt::AlignCenter);

    mainLayout->addWidget(previewGroup);
    mainLayout->addStretch();

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* applyBtn = new QPushButton(tr("Apply"), this);
    applyBtn->setObjectName("okBtn");
    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);
}

void FillLayerDialog::updatePreview()
{
    QColor c = selectedColor();
    auto* t = ThemeManager::instance()->current();
    m_preview->setStyleSheet(
        QString("background: rgba(%1,%2,%3,%4); border: 1px solid %5; border-radius: %6px;")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
            .arg(t->colorBorder.name())
            .arg(t->radiusMD));
}
