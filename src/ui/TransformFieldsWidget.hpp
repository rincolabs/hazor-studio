#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QPushButton;
class ScrubbableValueInput;

enum class TransformFieldsMode {
    PropertiesPanel,
    OptionsBar
};

class TransformFieldsWidget : public QWidget {
    Q_OBJECT

public:
    enum TransformField {
        FieldWidth = 0,
        FieldHeight,
        FieldX,
        FieldY,
        FieldRotation
    };

    explicit TransformFieldsWidget(TransformFieldsMode mode, QWidget* parent = nullptr);

    bool proportionsLocked() const;
    void setTransformValues(double widthPx, double heightPx, double posX,
                            double posY, double rotationDeg);
    void setFieldsEnabled(bool enabled);

signals:
    void fieldEdited(int field, double value);

private:
    QDoubleSpinBox* makeInput(double min, double max, int decimals,
                              const QString& suffix, QWidget* parent);
    void emitCommitted(int field, QDoubleSpinBox* input);

    TransformFieldsMode m_mode;
    QPushButton* m_linkButton = nullptr;
    QDoubleSpinBox* m_wInput = nullptr;
    QDoubleSpinBox* m_hInput = nullptr;
    QDoubleSpinBox* m_xInput = nullptr;
    QDoubleSpinBox* m_yInput = nullptr;
    ScrubbableValueInput* m_rotInput = nullptr;
};
