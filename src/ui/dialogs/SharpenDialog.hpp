#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class SharpenDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit SharpenDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_strength = nullptr;

    void onAnyChanged();
};
