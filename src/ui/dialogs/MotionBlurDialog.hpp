#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class MotionBlurDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit MotionBlurDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_length = nullptr;
    ParamRow* m_angle = nullptr;

    void onAnyChanged();
};
