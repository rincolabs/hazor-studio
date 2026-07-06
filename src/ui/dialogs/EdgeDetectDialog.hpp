#pragma once

#include "ToolDialog.hpp"

class ParamRow;
class QCheckBox;

class EdgeDetectDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit EdgeDetectDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;
    void resetToDefaults() override;

private:
    ParamRow* m_threshold1 = nullptr;
    ParamRow* m_threshold2 = nullptr;
    QCheckBox* m_linkThresholds = nullptr;

    void onAnyChanged();
    void onThreshold1Changed(double val);
};
