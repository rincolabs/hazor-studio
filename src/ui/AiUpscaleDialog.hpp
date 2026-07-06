#pragma once

#include "ai/upscale/UpscaleTypes.hpp"

#include <QDialog>

class AppCheckBox;
class AppComboBox;
class QLabel;
class QPushButton;
class QRadioButton;

class AiUpscaleDialog : public QDialog {
    Q_OBJECT
public:
    explicit AiUpscaleDialog(const UpscaleBackendStatus& backendStatus,
                             UpscaleTarget initialTarget,
                             QWidget* parent = nullptr);

    UpscaleOptions options() const;

signals:
    void openModelManagerRequested();
    void backendPathChanged();

private:
    void populateModels();
    void updateUiState();
    void applyTheme();

    UpscaleBackendStatus m_backendStatus;

    QRadioButton* m_layerTarget = nullptr;
    QRadioButton* m_documentTarget = nullptr;
    QRadioButton* m_newLayer = nullptr;
    QRadioButton* m_replaceLayer = nullptr;
    QRadioButton* m_newDocument = nullptr;
    QRadioButton* m_replaceDocument = nullptr;
    AppComboBox* m_modelCombo = nullptr;
    AppComboBox* m_scaleCombo = nullptr;
    AppComboBox* m_tileCombo = nullptr;
    AppCheckBox* m_preserveAlpha = nullptr;
    AppCheckBox* m_preserveMask = nullptr;
    AppCheckBox* m_preserveProfile = nullptr;
    QLabel* m_backendName = nullptr;
    QLabel* m_backendStatusLabel = nullptr;
    QLabel* m_detailLabel = nullptr;
    QPushButton* m_openModelsButton = nullptr;
    QPushButton* m_locateBackendButton = nullptr;
    QPushButton* m_upscaleButton = nullptr;
};
