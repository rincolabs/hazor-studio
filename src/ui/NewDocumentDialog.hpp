#pragma once

#include <QDialog>
#include <QColor>
#include <QString>
#include <vector>

#include "color/ColorProfile.hpp"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;

struct DocumentSettings {
    QString name = "My new document";
    QString documentType = "Photo";
    double width = 10.0;
    double height = 8.0;
    QString unit = "in";
    int resolution = 300;
    QString resolutionUnit = "px/in";
    QString colorMode = "RGB Color, 8 bit";
    QString background = "White";
    QColor backgroundColor = Qt::white;
    QString colorProfile = "Working RGB: sRGB IEC61966-2.1";
    // The document's colour profile (working space). colorManaged == false means
    // "Don't Color Manage this Document" (untagged).
    ColorProfileKind colorProfileKind = ColorProfileKind::SRgb;
    bool colorManaged = true;
    QString pixelAspect = "Square Pixels";
};

class NewDocumentDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewDocumentDialog(QWidget* parent = nullptr);

    void setSettings(const DocumentSettings& s);
    DocumentSettings settings() const;

private slots:
    void onSizePresetChanged(int index);
    void onWidthChanged(double v);
    void onHeightChanged(double v);
    void onWidthUnitChanged(int index);
    void onHeightUnitChanged(int index);
    void onResolutionChanged(int v);
    void onResolutionUnitChanged(int index);
    void onBackgroundChanged(int index);
    void pickBackgroundColor();
    void updateImageSizeDisplay();

private:
    QWidget* createFormSection();
    QWidget* createButtonSection();
    void setupPresets();
    double valueToInches(double v, const QString& unit) const;
    double inchesToValue(double inches, const QString& unit) const;

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_docTypeCombo = nullptr;
    QComboBox* m_sizePresetCombo = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QDoubleSpinBox* m_heightSpin = nullptr;
    QComboBox* m_widthUnitCombo = nullptr;
    QComboBox* m_heightUnitCombo = nullptr;
    QSpinBox* m_resolutionSpin = nullptr;
    QComboBox* m_resolutionUnitCombo = nullptr;
    QComboBox* m_colorModeCombo = nullptr;
    QComboBox* m_colorProfileCombo = nullptr;
    QComboBox* m_backgroundCombo = nullptr;
    QPushButton* m_bgColorBtn = nullptr;
    QLabel* m_imageSizeLabel = nullptr;
    QLabel* m_imageSizeDetailLabel = nullptr;
    QColor m_bgColor = Qt::white;

    QPushButton* m_okBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_savePresetBtn = nullptr;
    QPushButton* m_deletePresetBtn = nullptr;

    struct SizePreset {
        QString label;
        double w;
        double h;
        QString unit;
    };
    std::vector<SizePreset> m_presets;
};
