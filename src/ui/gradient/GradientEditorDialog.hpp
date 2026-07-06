#pragma once

#include "gradient/GradientTypes.hpp"
#include "GradientStopEditor.hpp"

#include <QDialog>

class GradientPresetManager;
class GradientPreviewWidget;
class QListWidget;
class QLineEdit;
class QComboBox;
class QPushButton;
class QToolButton;
class ScrubbableValueInput;

class GradientEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit GradientEditorDialog(GradientPresetManager* presetManager,
                                  const GradientDefinition& initial,
                                  QWidget* parent = nullptr);

    GradientDefinition gradient() const;

private:
    void buildUi();
    void refreshPresetList();
    void setWorkingGradient(const GradientDefinition& definition);
    void syncControlsFromGradient();
    void updateStopControls();
    void updateColorButton();
    void addSettingsMenuActions(QToolButton* button);

    GradientPresetManager* m_presetManager = nullptr;
    GradientDefinition m_definition;

    QListWidget* m_presetList = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    ScrubbableValueInput* m_smoothnessInput = nullptr;
    GradientStopEditor* m_stopEditor = nullptr;
    QPushButton* m_colorButton = nullptr;
    ScrubbableValueInput* m_opacityInput = nullptr;
    ScrubbableValueInput* m_locationInput = nullptr;
    QPushButton* m_deleteStopButton = nullptr;
};

