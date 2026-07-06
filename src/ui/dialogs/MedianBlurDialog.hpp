#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class MedianBlurDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit MedianBlurDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_kernelSize = nullptr;

    void onAnyChanged();
};
