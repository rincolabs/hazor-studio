#include "ResizeLayerDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include "ui/AppCheckBox.hpp"
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>

ResizeLayerDialog::ResizeLayerDialog(int origWidth, int origHeight,
                                     QWidget* parent)
    : QDialog(parent)
    , m_origWidth(origWidth)
    , m_origHeight(origHeight)
{
    setWindowTitle(tr("Resize Layer"));
    setFixedSize(320, 280);
    setupUi(origWidth, origHeight);
}

int ResizeLayerDialog::width() const { return m_widthSpin->value(); }
int ResizeLayerDialog::height() const { return m_heightSpin->value(); }
bool ResizeLayerDialog::keepAspectRatio() const { return m_keepRatio->isChecked(); }

void ResizeLayerDialog::setupUi(int origWidth, int origHeight)
{
    auto* t = ThemeManager::instance()->current();
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t->spaceLG);

    m_currentSizeLabel = new QLabel(
        tr("Current: %1 x %2").arg(origWidth).arg(origHeight), this);
    m_currentSizeLabel->setObjectName("infoLabel");
    mainLayout->addWidget(m_currentSizeLabel);

    auto* sizeGroup = new QGroupBox(tr("New Size"), this);
    auto* form = new QFormLayout(sizeGroup);
    form->setSpacing(t->spaceMD);

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(1, 10000);
    m_widthSpin->setValue(origWidth);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ResizeLayerDialog::onWidthChanged);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(1, 10000);
    m_heightSpin->setValue(origHeight);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ResizeLayerDialog::onHeightChanged);

    form->addRow(tr("Width:"), m_widthSpin);
    form->addRow(tr("Height:"), m_heightSpin);

    m_keepRatio = new AppCheckBox(tr("Keep aspect ratio"), this);
    m_keepRatio->setChecked(true);
    form->addRow(m_keepRatio);

    mainLayout->addWidget(sizeGroup);

    m_newSizeLabel = new QLabel(
        tr("New: %1 x %2").arg(origWidth).arg(origHeight), this);
    m_newSizeLabel->setObjectName("footerLabel");
    mainLayout->addWidget(m_newSizeLabel);

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

void ResizeLayerDialog::onWidthChanged(int value)
{
    if (m_keepRatio->isChecked() && m_origWidth > 0) {
        m_heightSpin->blockSignals(true);
        int newH = qMax(1, static_cast<int>(std::round(
            static_cast<double>(value) * m_origHeight / m_origWidth)));
        m_heightSpin->setValue(newH);
        m_heightSpin->blockSignals(false);
    }
    m_newSizeLabel->setText(
        tr("New: %1 x %2").arg(value).arg(m_heightSpin->value()));
}

void ResizeLayerDialog::onHeightChanged(int value)
{
    if (m_keepRatio->isChecked() && m_origHeight > 0) {
        m_widthSpin->blockSignals(true);
        int newW = qMax(1, static_cast<int>(std::round(
            static_cast<double>(value) * m_origWidth / m_origHeight)));
        m_widthSpin->setValue(newW);
        m_widthSpin->blockSignals(false);
    }
    m_newSizeLabel->setText(
        tr("New: %1 x %2").arg(m_widthSpin->value()).arg(value));
}
