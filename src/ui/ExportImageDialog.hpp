#pragma once

#include <QDialog>
#include <QString>
#include <QSize>
#include <QImage>
#include <QPixmap>
#include <QVector>
#include <QSettings>
#include <QFutureWatcher>

#include "color/ColorManagementService.hpp"

class Document;
class QComboBox;
class QStackedWidget;
class QSlider;
class QLabel;
class QCheckBox;
class QSpinBox;
class QTimer;
class QPushButton;

// Result of the off-thread preview encode: the encoded byte size plus the
// image decoded back from those bytes (so lossy quality shows in the preview).
struct ExportPreviewResult {
    quint64 generation = 0;
    qint64 byteSize = -1;
    QImage image;
    bool hasImage = false;
};

struct ExportPreset {
    QString name;
    QString format = "png";
    int quality = 92;
    int compression = 6;
    bool progressive = false;
    bool transparency = true;
    bool resizeEnabled = false;
    int resizeW = 0;
    int resizeH = 0;
    int resampleMode = 1;

    QVariantMap toVariant() const;
    static ExportPreset fromVariant(const QVariantMap& v);
};

class ExportImageDialog : public QDialog {
    Q_OBJECT
public:
    struct Options {
        QString format = "png";
        int quality = 92;
        int compression = 6;
        bool progressive = false;
        bool transparency = true;
        bool resizeEnabled = false;
        QSize targetSize;
        Qt::TransformationMode resampleMode = Qt::SmoothTransformation;
        ExportColorMode colorMode = ExportColorMode::ConvertToSRgbAndEmbed;
        ColorProfile colorSelectedProfile;
    };

    explicit ExportImageDialog(Document* doc, QWidget* parent = nullptr);
    Options options() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onFormatChanged(int index);
    void onQualityChanged(int value);
    void onCompressionChanged(int value);
    void onProgressiveToggled(bool checked);
    void onTransparencyToggled(bool checked);
    void onResizeToggled(bool checked);
    void onWidthChanged(int value);
    void onHeightChanged(int value);
    void onConstrainToggled(bool checked);
    void onResampleChanged(int index);
    void onColorModeChanged(int index);
    void onColorProfileChanged(int index);
    void onPresetSelected(int index);
    void onSavePreset();
    void onDeletePreset();
    void updatePreview();
    void onPreviewComputed();

private:
    void buildUi();
    void rescalePreviewPixmap();
    QWidget* createPngPage();
    QWidget* createJpegPage();
    QWidget* createGenericPage();
    void loadPresets();
    void savePresets();
    void populatePresetCombo();
    void applyCurrentAsPresetValue();
    void syncSizeLink();
    void updateColorWarning();
    QString formatFileSize(qint64 bytes) const;

    Document* m_doc = nullptr;
    Options m_opts;
    QImage m_fullImage;
    QImage m_previewSourceImage;
    QVector<ExportPreset> m_presets;

    // UI
    QComboBox* m_formatCombo = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QStackedWidget* m_optionsStack = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_fileSizeLabel = nullptr;
    QLabel* m_dimensionsLabel = nullptr;
    QSlider* m_qualitySlider = nullptr;
    QLabel* m_qualityValueLabel = nullptr;
    QSlider* m_compressionSlider = nullptr;
    QLabel* m_compressionValueLabel = nullptr;
    QCheckBox* m_progressiveCheck = nullptr;
    QCheckBox* m_transparencyCheck = nullptr;
    QCheckBox* m_resizeCheck = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QPushButton* m_constrainCheck = nullptr;
    QComboBox* m_resampleCombo = nullptr;
    QComboBox* m_colorModeCombo = nullptr;
    QComboBox* m_colorProfileCombo = nullptr;
    QLabel* m_colorWarningLabel = nullptr;
    QTimer* m_previewTimer = nullptr;
    QPushButton* m_deletePresetBtn = nullptr;

    // Off-thread preview encoding (debounced by m_previewTimer; only the latest
    // generation's result is applied so a fast slider drag never blocks the UI).
    QFutureWatcher<ExportPreviewResult>* m_previewWatcher = nullptr;
    quint64 m_previewGeneration = 0;

    bool m_updating = false;
};
