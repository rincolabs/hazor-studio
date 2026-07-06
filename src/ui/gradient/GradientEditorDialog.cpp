#include "GradientEditorDialog.hpp"

#include "GradientPreviewWidget.hpp"
#include "gradient/GradientPresetManager.hpp"
#include "gradient/GradientRenderer.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/ScrubbableValueInput.h"
#include "ui/colorpicker/ColorPickerDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString colorButtonStyle(const QColor& color)
{
    auto* theme = ThemeManager::instance()->current();
    return QStringLiteral(
        "background-color:%1;"
        "border:1px solid %2;"
        "border-radius:%3px;")
        .arg(color.name(QColor::HexRgb))
        .arg(theme ? theme->colorBorder.name() : QStringLiteral("#555555"))
        .arg(theme ? theme->radiusSM : 3);
}

QListWidgetItem* makePresetItem(const GradientDefinition& definition)
{
    auto* item = new QListWidgetItem;
    item->setText(definition.name);
    item->setIcon(QPixmap::fromImage(
        GradientRenderer::generateThumbnail(definition, QSize(96, 34))));
    item->setSizeHint(QSize(128, 58));
    return item;
}

QString gradientPresetFilters()
{
    return QObject::tr("Hazor Studio Gradient Presets (*.hsgp);;JSON Gradient Presets (*.json);;All Files (*)");
}
}

GradientEditorDialog::GradientEditorDialog(GradientPresetManager* presetManager,
                                           const GradientDefinition& initial,
                                           QWidget* parent)
    : QDialog(parent)
    , m_presetManager(presetManager)
    , m_definition(initial)
{
    m_definition.normalize();
    setWindowTitle(tr("Gradient Editor"));
    setModal(true);
    resize(760, 560);
    buildUi();
    refreshPresetList();
    syncControlsFromGradient();
}

GradientDefinition GradientEditorDialog::gradient() const
{
    GradientDefinition definition = m_definition;
    definition.normalize();
    return definition;
}

void GradientEditorDialog::buildUi()
{
    auto* theme = ThemeManager::instance()->current();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(theme ? theme->spaceLG : 16,
                             theme ? theme->spaceLG : 16,
                             theme ? theme->spaceLG : 16,
                             theme ? theme->spaceLG : 16);
    root->setSpacing(theme ? theme->spaceMD : 12);

    auto* mainRow = new QHBoxLayout;
    mainRow->setSpacing(theme ? theme->spaceLG : 16);
    root->addLayout(mainRow, 1);

    auto* presetColumn = new QVBoxLayout;
    presetColumn->setSpacing(theme ? theme->spaceSM : 8);
    mainRow->addLayout(presetColumn, 0);

    auto* presetHeader = new QHBoxLayout;
    auto* presetLabel = new QLabel(tr("Presets"), this);
    presetHeader->addWidget(presetLabel);
    presetHeader->addStretch();
    auto* settingsButton = new QToolButton(this);
    settingsButton->setText(QStringLiteral("..."));
    settingsButton->setPopupMode(QToolButton::InstantPopup);
    addSettingsMenuActions(settingsButton);
    presetHeader->addWidget(settingsButton);
    presetColumn->addLayout(presetHeader);

    m_presetList = new QListWidget(this);
    m_presetList->setViewMode(QListView::IconMode);
    m_presetList->setResizeMode(QListView::Adjust);
    m_presetList->setMovement(QListView::Static);
    m_presetList->setIconSize(QSize(96, 34));
    m_presetList->setSpacing(6);
    m_presetList->setFixedWidth(210);
    presetColumn->addWidget(m_presetList, 1);

    auto* presetButtons = new QHBoxLayout;
    auto* newButton = new QPushButton(tr("New"), this);
    auto* saveButton = new QPushButton(tr("Save"), this);
    auto* loadButton = new QPushButton(tr("Load"), this);
    presetButtons->addWidget(newButton);
    presetButtons->addWidget(saveButton);
    presetButtons->addWidget(loadButton);
    presetColumn->addLayout(presetButtons);

    auto* editColumn = new QVBoxLayout;
    editColumn->setSpacing(theme ? theme->spaceMD : 12);
    mainRow->addLayout(editColumn, 1);

    auto* topForm = new QFormLayout;
    topForm->setLabelAlignment(Qt::AlignRight);
    m_nameEdit = new QLineEdit(this);
    topForm->addRow(tr("Name:"), m_nameEdit);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(gradientKindName(GradientKind::Linear), static_cast<int>(GradientKind::Linear));
    m_typeCombo->addItem(gradientKindName(GradientKind::Radial), static_cast<int>(GradientKind::Radial));
    m_typeCombo->addItem(gradientKindName(GradientKind::Angle), static_cast<int>(GradientKind::Angle));
    m_typeCombo->addItem(gradientKindName(GradientKind::Reflected), static_cast<int>(GradientKind::Reflected));
    m_typeCombo->addItem(gradientKindName(GradientKind::Diamond), static_cast<int>(GradientKind::Diamond));
    topForm->addRow(tr("Gradient Type:"), m_typeCombo);

    m_smoothnessInput = new ScrubbableValueInput(tr("Smoothness"), 0, 100, 100, tr("%"), 1, this);
    topForm->addRow(QString(), m_smoothnessInput);
    editColumn->addLayout(topForm);

    m_stopEditor = new GradientStopEditor(this);
    editColumn->addWidget(m_stopEditor);

    auto* properties = new QGridLayout;
    properties->setHorizontalSpacing(theme ? theme->spaceMD : 12);
    properties->setVerticalSpacing(theme ? theme->spaceSM : 8);

    properties->addWidget(new QLabel(tr("Color"), this), 0, 0);
    m_colorButton = new QPushButton(this);
    m_colorButton->setFixedSize(44, 24);
    properties->addWidget(m_colorButton, 0, 1);

    m_opacityInput = new ScrubbableValueInput(tr("Opacity"), 0, 100, 100, tr("%"), 1, this);
    properties->addWidget(m_opacityInput, 0, 2);

    m_locationInput = new ScrubbableValueInput(tr("Location"), 0, 100, 0, tr("%"), 1, this);
    properties->addWidget(m_locationInput, 1, 0, 1, 2);

    m_deleteStopButton = new QPushButton(tr("Delete"), this);
    properties->addWidget(m_deleteStopButton, 1, 2);
    editColumn->addLayout(properties);
    editColumn->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(m_presetList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_presetManager || row < 0 || row >= m_presetManager->presets().size())
            return;
        setWorkingGradient(m_presetManager->presets()[row]);
    });
    connect(m_presetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        accept();
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        if (!m_presetManager)
            return;
        GradientDefinition definition = gradient();
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Gradient Preset"), tr("Name:"),
            QLineEdit::Normal, definition.name, &ok);
        if (!ok)
            return;
        if (!name.trimmed().isEmpty())
            definition.name = name.trimmed();
        m_presetManager->addPreset(definition);
        refreshPresetList();
    });
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        if (!m_presetManager)
            return;
        QFileDialog dialog(this, tr("Save Gradient Presets"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setNameFilters(gradientPresetFilters().split(QStringLiteral(";;")));
        dialog.selectNameFilter(tr("Hazor Studio Gradient Presets (*.hsgp)"));
        dialog.setDefaultSuffix(QStringLiteral("hsgp"));
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
            m_presetManager->saveToFile(dialog.selectedFiles().first());
    });
    connect(loadButton, &QPushButton::clicked, this, [this]() {
        if (!m_presetManager)
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load Gradient Presets"), QString(), gradientPresetFilters());
        if (!path.isEmpty() && m_presetManager->loadFromFile(path)) {
            refreshPresetList();
            if (!m_presetManager->presets().isEmpty())
                setWorkingGradient(m_presetManager->presets().first());
        }
    });

    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_definition.name = text;
    });
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        m_definition.kind = static_cast<GradientKind>(m_typeCombo->currentData().toInt());
        if (m_stopEditor)
            m_stopEditor->setGradient(m_definition);
    });
    connect(m_smoothnessInput, &ScrubbableValueInput::valueChanged, this, [this](double value) {
        m_definition.smoothness = std::clamp(value / 100.0, 0.0, 1.0);
        if (m_stopEditor)
            m_stopEditor->setGradient(m_definition);
    });
    connect(m_stopEditor, &GradientStopEditor::gradientChanged, this, [this](const GradientDefinition& definition) {
        m_definition = definition;
        updateStopControls();
    });
    connect(m_stopEditor, &GradientStopEditor::activeStopChanged,
            this, [this](GradientStopEditor::StopType, int) {
        updateStopControls();
    });
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        if (!m_stopEditor || m_stopEditor->activeStopType() != GradientStopEditor::StopType::Color)
            return;
        auto* dialog = new ColorPickerDialog(m_stopEditor->activeColor(), ColorPickerMode::Foreground, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &ColorPickerDialog::colorAccepted, this, [this](const QColor& color) {
            m_stopEditor->setActiveColor(color);
            m_definition = m_stopEditor->gradient();
            updateStopControls();
        });
        dialog->open();
    });
    connect(m_opacityInput, &ScrubbableValueInput::valueChanged, this, [this](double value) {
        if (m_stopEditor && m_stopEditor->activeStopType() == GradientStopEditor::StopType::Opacity)
            m_stopEditor->setActiveOpacity(std::clamp(value / 100.0, 0.0, 1.0));
        m_definition = m_stopEditor->gradient();
    });
    connect(m_locationInput, &ScrubbableValueInput::valueChanged, this, [this](double value) {
        if (m_stopEditor)
            m_stopEditor->setActiveLocation(std::clamp(value / 100.0, 0.0, 1.0));
        m_definition = m_stopEditor->gradient();
    });
    connect(m_deleteStopButton, &QPushButton::clicked, this, [this]() {
        if (m_stopEditor) {
            m_stopEditor->deleteActiveStop();
            m_definition = m_stopEditor->gradient();
            updateStopControls();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_definition.name = m_nameEdit ? m_nameEdit->text() : m_definition.name;
        m_definition.normalize();
        if (m_presetManager)
            m_presetManager->setCurrentGradient(m_definition);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void GradientEditorDialog::refreshPresetList()
{
    if (!m_presetList)
        return;
    QSignalBlocker blocker(m_presetList);
    m_presetList->clear();
    if (!m_presetManager)
        return;
    for (const auto& preset : m_presetManager->presets())
        m_presetList->addItem(makePresetItem(preset));
}

void GradientEditorDialog::setWorkingGradient(const GradientDefinition& definition)
{
    m_definition = definition;
    m_definition.normalize();
    syncControlsFromGradient();
}

void GradientEditorDialog::syncControlsFromGradient()
{
    if (m_nameEdit) {
        QSignalBlocker blocker(m_nameEdit);
        m_nameEdit->setText(m_definition.name);
    }
    if (m_typeCombo) {
        QSignalBlocker blocker(m_typeCombo);
        const int idx = m_typeCombo->findData(static_cast<int>(m_definition.kind));
        m_typeCombo->setCurrentIndex(std::max(0, idx));
    }
    if (m_smoothnessInput) {
        QSignalBlocker blocker(m_smoothnessInput);
        m_smoothnessInput->setValue(m_definition.smoothness * 100.0);
    }
    if (m_stopEditor)
        m_stopEditor->setGradient(m_definition);
    updateStopControls();
}

void GradientEditorDialog::updateStopControls()
{
    if (!m_stopEditor)
        return;

    const bool isColor = m_stopEditor->activeStopType() == GradientStopEditor::StopType::Color;
    if (m_colorButton)
        m_colorButton->setEnabled(isColor);
    if (m_opacityInput)
        m_opacityInput->setEnabled(!isColor);

    if (m_opacityInput) {
        QSignalBlocker blocker(m_opacityInput);
        m_opacityInput->setValue(m_stopEditor->activeOpacity() * 100.0);
    }
    if (m_locationInput) {
        QSignalBlocker blocker(m_locationInput);
        m_locationInput->setValue(m_stopEditor->activeLocation() * 100.0);
    }
    if (m_deleteStopButton) {
        const bool canDelete = isColor
            ? m_definition.colorStops.size() > 2
            : m_definition.opacityStops.size() > 2;
        m_deleteStopButton->setEnabled(canDelete);
    }
    updateColorButton();
}

void GradientEditorDialog::updateColorButton()
{
    if (!m_colorButton || !m_stopEditor)
        return;
    const QColor color = m_stopEditor->activeColor();
    m_colorButton->setStyleSheet(colorButtonStyle(color));
    m_colorButton->setToolTip(color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toUpper());
}

void GradientEditorDialog::addSettingsMenuActions(QToolButton* button)
{
    auto* menu = new QMenu(button);
    auto* rename = menu->addAction(tr("Rename preset"));
    auto* remove = menu->addAction(tr("Delete preset"));
    menu->addSeparator();
    auto* reset = menu->addAction(tr("Reset presets"));
    menu->addSeparator();
    auto* import = menu->addAction(tr("Import presets"));
    auto* exportAction = menu->addAction(tr("Export presets"));

    rename->setEnabled(false);
    import->setEnabled(false);
    exportAction->setEnabled(false);

    connect(remove, &QAction::triggered, this, [this]() {
        if (!m_presetManager || !m_presetList)
            return;
        m_presetManager->removePreset(m_presetList->currentRow());
        refreshPresetList();
    });
    connect(reset, &QAction::triggered, this, [this]() {
        if (!m_presetManager)
            return;
        m_presetManager->resetToDefaults();
        m_presetManager->savePresets();
        refreshPresetList();
    });
    button->setMenu(menu);
}
