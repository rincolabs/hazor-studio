#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class BoxBlurDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit BoxBlurDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_radius = nullptr;

    void onAnyChanged();
};
