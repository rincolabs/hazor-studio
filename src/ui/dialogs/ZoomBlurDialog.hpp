#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class ZoomBlurDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit ZoomBlurDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_amount = nullptr;
    ParamRow* m_samples = nullptr;
    ParamRow* m_centerX = nullptr;
    ParamRow* m_centerY = nullptr;

    void onAnyChanged();
};
