#pragma once

#include <QColor>
#include <QDialog>
#include <QSize>
#include <QString>
#include <QVector>

#include "animation/AnimationExportService.hpp"

class Document;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class AppCheckBox;

// A saved bundle of parameters the preset tree can apply. Built-in presets live
// under named categories; user presets live under "Custom" and persist in
// QSettings. A preset only *fills* the dialog — every value stays editable.
struct AnimExportPreset {
    QString name;
    QString category;
    bool builtin = true;

    anim::ExportFormat format = anim::ExportFormat::Mp4;
    // 0 means "use the document's original canvas size".
    int width = 0;
    int height = 0;
    int crf = 20;               // MP4 quality
    int gifColors = 256;

    QVariantMap toVariant() const;
    static AnimExportPreset fromVariant(const QVariantMap& v);
};

// File → Export Animation As… — a preset-driven exporter for the timeline
// animation. Left: a searchable preset tree. Right: format + Output / Video-GIF
// / Image Sequence / Advanced tabs, a live summary and a static preview. The
// dialog produces an anim::AnimationExportRequest; MainWindow runs it off-thread.
class ExportAnimationDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportAnimationDialog(Document* doc, QWidget* parent = nullptr);

    // Valid only after exec() returns Accepted.
    anim::AnimationExportRequest request() const { return m_request; }

    void accept() override;

private slots:
    void onFormatChanged();
    void onPresetActivated();
    void onAddPreset();
    void onRemovePreset();
    void onSearchChanged(const QString& text);
    void onBrowseOutput();
    void onRangeModeChanged();
    void onRangeSpinChanged();
    void onFpsChanged();
    void onOriginalSizeToggled(bool on);
    void onWidthChanged();
    void onHeightChanged();
    void onScaleChanged();
    void onAspectChanged();
    void onMp4QualityChanged();
    void onGifLoopChanged();
    void onPickBackground();
    void onResetClicked();

private:
    void buildUi();
    QWidget* buildLeftPanel();
    QWidget* buildOutputTab();
    QWidget* buildVideoTab();
    QWidget* buildSequenceTab();
    QWidget* buildAdvancedTab();

    void populatePresetTree();
    QVector<AnimExportPreset> builtinPresets() const;
    void loadCustomPresets();
    void saveCustomPresets();
    void applyPreset(const AnimExportPreset& preset);

    void seedFromDocument();
    void updateFormatVisibility();
    void updateRangeInfo();
    void updateFpsControls();
    void updateSizeFromScale();
    void setSize(int w, int h);
    void updateNamePreview();
    void updateSummary();
    void updatePreview();
    void updateBackgroundSwatch();
    void buildRequest();

    QSize canvasSize() const;
    anim::ExportFormat currentFormat() const;
    double currentFps() const;
    QSize currentTargetSize() const;

    Document* m_doc = nullptr;
    anim::AnimationExportRequest m_request;
    QImage m_canvasFrame;                 // live current frame, composited once
    QVector<AnimExportPreset> m_customPresets;
    bool m_updating = false;
    QColor m_backgroundColor = QColor(0, 0, 0);
    bool m_ffmpegAvailable = false;

    // Left panel
    QLineEdit* m_searchEdit = nullptr;
    QTreeWidget* m_presetTree = nullptr;
    QPushButton* m_addPresetBtn = nullptr;
    QPushButton* m_removePresetBtn = nullptr;

    // Format + output
    QComboBox* m_formatCombo = nullptr;
    QLabel* m_outputLabel = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QLabel* m_ffmpegWarning = nullptr;

    QTabWidget* m_tabs = nullptr;

    // Output tab — range
    QComboBox* m_rangeModeCombo = nullptr;
    QSpinBox* m_startSpin = nullptr;
    QSpinBox* m_endSpin = nullptr;
    QLabel* m_totalFramesLabel = nullptr;
    QLabel* m_durationLabel = nullptr;

    // Output tab — fps
    QComboBox* m_fpsCombo = nullptr;
    QDoubleSpinBox* m_fpsCustomSpin = nullptr;
    QLabel* m_fpsMismatchLabel = nullptr;

    // Output tab — size
    AppCheckBox* m_originalSizeCheck = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QPushButton* m_constrainBtn = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QComboBox* m_aspectCombo = nullptr;
    QComboBox* m_fitCombo = nullptr;
    AppCheckBox* m_transparencyCheck = nullptr;
    QPushButton* m_bgColorBtn = nullptr;
    QLabel* m_aspectWarnLabel = nullptr;

    // Video / GIF tab
    QWidget* m_mp4Group = nullptr;
    QComboBox* m_mp4QualityCombo = nullptr;
    QComboBox* m_codecCombo = nullptr;
    QSpinBox* m_crfSpin = nullptr;
    QComboBox* m_encoderPresetCombo = nullptr;
    QComboBox* m_pixFmtCombo = nullptr;
    AppCheckBox* m_hwCheck = nullptr;
    QWidget* m_gifGroup = nullptr;
    QSpinBox* m_gifColorsSpin = nullptr;
    AppCheckBox* m_gifDitherCheck = nullptr;
    QComboBox* m_gifLoopCombo = nullptr;
    QSpinBox* m_gifLoopSpin = nullptr;

    // Image Sequence tab
    QLineEdit* m_prefixEdit = nullptr;
    QSpinBox* m_paddingSpin = nullptr;
    QLabel* m_namePreviewLabel = nullptr;
    QSlider* m_pngCompressionSlider = nullptr;
    QWidget* m_pngRow = nullptr;
    QSlider* m_jpegQualitySlider = nullptr;
    QWidget* m_jpegRow = nullptr;

    // Advanced tab
    QComboBox* m_interpCombo = nullptr;

    // Summary / preview
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_previewLabel = nullptr;
};
