#pragma once

#include <QDialog>
#include <QColor>

class QSlider;
class QSpinBox;
class QFrame;

class FillLayerDialog : public QDialog {
    Q_OBJECT
public:
    explicit FillLayerDialog(QWidget* parent = nullptr);

    QColor selectedColor() const;

private slots:
    void updatePreview();

private:
    void setupUi();

    QSlider* m_redSlider = nullptr;
    QSlider* m_greenSlider = nullptr;
    QSlider* m_blueSlider = nullptr;
    QSlider* m_alphaSlider = nullptr;

    QSpinBox* m_redSpin = nullptr;
    QSpinBox* m_greenSpin = nullptr;
    QSpinBox* m_blueSpin = nullptr;
    QSpinBox* m_alphaSpin = nullptr;

    QFrame* m_preview = nullptr;
};
