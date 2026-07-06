#pragma once

#include "ToolDialog.hpp"

class ParamRow;

class PosterizeDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit PosterizeDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;

private:
    ParamRow* m_levels = nullptr;

    void onAnyChanged();
};
