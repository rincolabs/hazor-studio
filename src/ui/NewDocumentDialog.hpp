#pragma once

#include <QDialog>
#include <QColor>
#include <QString>
#include <QVector>

#include "color/ColorProfile.hpp"
#include "ui/CanvasPresets.hpp"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QTreeWidget;

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
    void onWidthChanged(double v);
    void onHeightChanged(double v);
    void onWidthUnitChanged(int index);
    void onHeightUnitChanged(int index);
    void onResolutionChanged(int v);
    void onResolutionUnitChanged(int index);
    void onBackgroundChanged(int index);
    void pickBackgroundColor();
    void updateImageSizeDisplay();

    void onPresetSelectionChanged();
    void onSearchChanged(const QString& text);
    void onAddPreset();
    void onRemovePreset();

private:
    QWidget* createPresetPanel();
    QWidget* createFormSection();
    void populatePresetTree();
    void applyPreset(const canvaspresets::CanvasPreset& preset);
    void loadCustomPresets();
    void saveCustomPresets();
    double valueToInches(double v, const QString& unit) const;

    // ── Preset sidebar ──
    QLineEdit* m_searchEdit = nullptr;
    QTreeWidget* m_presetTree = nullptr;
    QPushButton* m_addPresetBtn = nullptr;
    QPushButton* m_removePresetBtn = nullptr;

    // ── Form ──
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_docTypeCombo = nullptr;
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
    QColor m_bgColor = Qt::white;

    QPushButton* m_okBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    QVector<canvaspresets::CanvasPreset> m_customPresets;
    bool m_updating = false;
};
