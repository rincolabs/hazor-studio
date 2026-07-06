#pragma once

#include "ToolDialog.hpp"

class ParamRow;
class QCheckBox;

class ThresholdDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit ThresholdDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_value = nullptr;
    QCheckBox* m_adaptive = nullptr;

    void onAnyChanged();
};
