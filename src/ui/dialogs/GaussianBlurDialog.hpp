#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class GaussianBlurDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit GaussianBlurDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_radius = nullptr;

    void onAnyChanged();
};
