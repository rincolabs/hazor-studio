#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class NoiseReduceDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit NoiseReduceDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_strength = nullptr;
    ParamRow* m_preserveEdges = nullptr;

    void onAnyChanged();
};
