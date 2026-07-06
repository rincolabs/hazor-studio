#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class BilateralBlurDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit BilateralBlurDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_diameter = nullptr;
    ParamRow* m_sigmaColor = nullptr;
    ParamRow* m_sigmaSpace = nullptr;

    void onAnyChanged();
};
