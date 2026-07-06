#pragma once

#include "ToolDialog.hpp"
#include <QColor>

class ParamRow;
class QFrame;

class FillLayerDialog : public ToolDialog {
    Q_OBJECT
public:
    explicit FillLayerDialog(ToolDialog::Mode mode, QWidget* parent = nullptr);

    QVariantMap collectParams() const override;
    QColor selectedColor() const;

private:
    ParamRow* m_red = nullptr;
    ParamRow* m_green = nullptr;
    ParamRow* m_blue = nullptr;
    ParamRow* m_alpha = nullptr;
    QFrame* m_preview = nullptr;

    void updateColorPreview();
    void onAnyChanged();
};
