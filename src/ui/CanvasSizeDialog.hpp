#pragma once

#include <QColor>
#include <QDialog>
#include <QSize>

#include "controller/ImageController.hpp"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

struct CanvasSizeSettings {
    QSize targetSize;
    CanvasAnchor anchor = CanvasAnchor::Center;
    bool fillExtension = false;
    QColor extensionColor = Qt::transparent;
};

class CanvasSizeDialog : public QDialog {
    Q_OBJECT

public:
    explicit CanvasSizeDialog(const QSize& currentSize,
                              double resolutionDpi,
                              const QColor& backgroundColor,
                              QWidget* parent = nullptr);

    CanvasSizeSettings settings() const;

private slots:
    void onUnitChanged(int index);
    void onRelativeToggled(bool checked);
    void onExtensionModeChanged(int index);
    void pickCustomColor();

private:
    enum class SizeUnit {
        Pixels,
        Percent,
        Inches,
        Centimeters,
        Millimeters,
        Points,
        Picas
    };

    double unitToPixels(double value, SizeUnit unit, int basePixels) const;
    double pixelsToUnit(double pixels, SizeUnit unit, int basePixels) const;
    void refreshSpinPresentation();
    CanvasAnchor selectedAnchor() const;

    QSize m_currentSize;
    double m_resolutionDpi = 300.0;
    QColor m_backgroundColor = Qt::white;
    QColor m_customColor = Qt::white;

    QLabel* m_currentWidthLabel = nullptr;
    QLabel* m_currentHeightLabel = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QDoubleSpinBox* m_heightSpin = nullptr;
    QComboBox* m_unitCombo = nullptr;
    QCheckBox* m_relativeCheck = nullptr;
    QButtonGroup* m_anchorButtons = nullptr;
    QComboBox* m_extensionModeCombo = nullptr;
    QPushButton* m_colorButton = nullptr;
};
