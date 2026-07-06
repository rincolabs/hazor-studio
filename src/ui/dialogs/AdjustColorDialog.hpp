#pragma once

#include "ToolDialog.hpp"

class ParamRow;
class QCheckBox;

class AdjustColorDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit AdjustColorDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;
    void resetToDefaults() override;

private:
    ParamRow* m_brightness = nullptr;
    ParamRow* m_contrast = nullptr;
    ParamRow* m_saturation = nullptr;
    ParamRow* m_hue = nullptr;
    QCheckBox* m_autoContrast = nullptr;

    void onAnyChanged();
};
