#pragma once

#include <QDialog>
#include <QSize>

#include "controller/ImageController.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDialogButtonBox;

struct ImageSizeSettings {
    QSize pixelSize;
    double resolutionDpi = 300.0;
    bool scaleStyles = true;
    bool constrainProportions = true;
    bool resampleImage = true;
    ResizeInterpolation interpolation = ResizeInterpolation::BicubicAutomatic;
};

class ImageSizeDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImageSizeDialog(const QSize& currentSize,
                             double currentResolutionDpi,
                             QWidget* parent = nullptr);

    ImageSizeSettings settings() const;

private slots:
    void onPixelWidthChanged(double value);
    void onPixelHeightChanged(double value);
    void onPixelUnitChanged(int index);
    void onDocWidthChanged(double value);
    void onDocHeightChanged(double value);
    void onDocUnitChanged(int index);
    void onResolutionChanged(double value);
    void onResolutionUnitChanged(int index);
    void onResampleToggled(bool checked);

private:
    enum class DocUnit {
        Inches,
        Centimeters,
        Millimeters,
        Points,
        Picas
    };

    double unitToInchesFactor(DocUnit unit) const;
    double currentResolutionDpi() const;
    void updatePixelSpinPresentation();
    void updatePixelSpinsFromTarget();
    void updateDocumentFieldsFromTarget();
    void updateTargetFromDocument(double documentValue, bool widthChanged);

    QSize m_originalSize;
    double m_originalResolutionDpi = 300.0;

    int m_targetWidthPx = 1;
    int m_targetHeightPx = 1;
    bool m_updating = false;

    QDoubleSpinBox* m_pixelWidthSpin = nullptr;
    QDoubleSpinBox* m_pixelHeightSpin = nullptr;
    QComboBox* m_pixelUnitCombo = nullptr;

    QDoubleSpinBox* m_docWidthSpin = nullptr;
    QDoubleSpinBox* m_docHeightSpin = nullptr;
    QComboBox* m_docUnitCombo = nullptr;

    QDoubleSpinBox* m_resolutionSpin = nullptr;
    QComboBox* m_resolutionUnitCombo = nullptr;

    QCheckBox* m_scaleStylesCheck = nullptr;
    QCheckBox* m_constrainCheck = nullptr;
    QCheckBox* m_resampleCheck = nullptr;
    QComboBox* m_interpolationCombo = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};
