#pragma once

#include <QWidget>

class ImageController;
class AgentPresetManager;
class QComboBox;
class QPlainTextEdit;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;

// Panel driving a Generative Fill request: prompt + negative prompt + target
// mode + a picker for Generative presets. Cloud providers require an explicit
// opt-in (the image leaves the machine); local SD does not. The request runs
// asynchronously via ImageController::generativeFill().
class GenerativeFillDialog : public QWidget {
    Q_OBJECT

public:
    GenerativeFillDialog(ImageController* controller,
                         AgentPresetManager* presets,
                         QWidget* parent = nullptr);
    void setController(ImageController* controller);
    void reloadPresets();

signals:
    void closeRequested();

private slots:
    void onPresetChanged();
    void onAccept();

private:
    bool selectedIsCloud() const;
    void applyCurrentPresetSettings();

    ImageController* m_controller = nullptr;
    AgentPresetManager* m_presets = nullptr;

    QComboBox* m_presetCombo = nullptr;
    QPlainTextEdit* m_promptEdit = nullptr;
    QLineEdit* m_negativeEdit = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QDoubleSpinBox* m_strengthSpin = nullptr;
    QSpinBox* m_stepsSpin = nullptr;
    QSpinBox* m_seedSpin = nullptr;
    QCheckBox* m_cloudOptIn = nullptr;
    QLabel* m_cloudWarning = nullptr;
    QPushButton* m_runBtn = nullptr;
};
