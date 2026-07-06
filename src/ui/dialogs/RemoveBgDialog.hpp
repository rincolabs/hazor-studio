#pragma once

#include "ToolDialog.hpp"

class ParamRow;
class QComboBox;

class RemoveBgDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit RemoveBgDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;
    void resetToDefaults() override;

private:
    QComboBox* m_modeCombo = nullptr;
    ParamRow* m_threshold = nullptr;
    ParamRow* m_feather = nullptr;

    void onAnyChanged();
};
