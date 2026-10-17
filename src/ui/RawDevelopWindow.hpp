#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <array>
#include <functional>

#include "raw/RawDevelopParams.hpp"
#include "raw/RawLayerData.hpp"

class QComboBox;
class QLabel;
class QScrollArea;
class QToolButton;
class QTimer;
class QWidget;
class QResizeEvent;
class QCloseEvent;

class AppCheckBox;
class HistogramWidget;
class ParamRow;
class CurveGraphWidget;

// Independent, non-modal RAW development window. It owns only a working copy
// of the PP3 and a display preview; ImageController remains the sole owner of
// the document mutation and undo command when Apply is confirmed.
class RawDevelopWindow : public QMainWindow {
    Q_OBJECT
public:
    RawDevelopWindow(int flatIndex, const RawLayerData& rawData,
                     const QImage& currentImage, QWidget* parent = nullptr);
    ~RawDevelopWindow() override;

    void setApplyInProgress(bool busy);
    void developFinished(bool ok, const QString& errorMessage);

signals:
    void applyRequested(int flatIndex, RawDevelopParams params);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    struct PreviewResult {
        quint64 generation = 0;
        QImage image;
        QString errorMessage;
        // Set only the first time a preview job had to materialize an
        // Embedded source (see startPreview()); empty on every later job,
        // which reuses the path already cached on m_previewSourcePath.
        QString sourcePath;
        bool ownsSourcePath = false;
    };

    void buildUi();
    void applyTheme();
    QWidget* buildLightPanel();
    QWidget* buildColorPanel();
    QWidget* buildEffectsPanel();
    QWidget* buildCurvePanel();
    QWidget* buildColorMixerPanel();
    QWidget* buildColorGradingPanel();
    QWidget* buildDetailPanel();
    QWidget* buildOpticsPanel();

    ParamRow* addParamRow(class QVBoxLayout* layout, const QString& label,
                           double min, double max, double defaultValue,
                           int decimals, const QString& toolTip,
                           const std::function<double()>& currentValue,
                           const std::function<void(double)>& setValue,
                           bool logarithmic = false,
                           bool centerZeroLog = false, double logBase = 2.0);
    void addSection(class QVBoxLayout* layout, const QString& title,
                    QWidget* contents, const std::function<void()>& reset,
                    bool initiallyOpen = false);
    void syncControlsFromParams();
    void applyParamsChanged();
    void requestPreview();
    void startPreview();
    void previewFinished();
    void updatePreviewImage(const QImage& image);
    void updateDisplayedImage();
    void updateHistogram(const QImage& image, bool stale = false);
    void reloadPresets();
    void applyPreset(int index);
    void savePreset();
    void loadCustomProfile();
    void updateProfileSummary();
    void resetAll();
    void setCurveFromGraph(CurveGraphWidget* graph, RawDevelopParams::CurveChannel channel);
    void syncCurveGraph(CurveGraphWidget* graph, RawDevelopParams::CurveChannel channel);

    int m_flatIndex = -1;
    RawLayerData m_rawData;
    RawDevelopParams m_params;
    QImage m_beforeImage;
    QImage m_previewImage;
    QString m_previewSourcePath;
    bool m_ownsPreviewSourcePath = false;
    bool m_showBefore = false;
    bool m_clipShadows = false;
    bool m_clipHighlights = false;
    bool m_fitToWindow = true;
    double m_zoom = 1.0;
    bool m_syncing = false;
    bool m_applyInProgress = false;
    bool m_previewPending = false;
    quint64 m_previewGeneration = 0;

    QFutureWatcher<PreviewResult>* m_previewWatcher = nullptr;
    QTimer* m_previewDebounce = nullptr;

    QLabel* m_imageLabel = nullptr;
    QScrollArea* m_previewScroll = nullptr;
    HistogramWidget* m_histogram = nullptr;
    QLabel* m_metadataLabel = nullptr;
    QLabel* m_profileSummary = nullptr;
    QLabel* m_previewStatus = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_zoomCombo = nullptr;
    QToolButton* m_beforeAfterButton = nullptr;
    QToolButton* m_shadowClipButton = nullptr;
    QToolButton* m_highlightClipButton = nullptr;
    QToolButton* m_applyButton = nullptr;

    class GradientSlider* m_temperatureSlider = nullptr;
    ParamRow* m_tintRow = nullptr;
    QComboBox* m_whiteBalanceCombo = nullptr;
    AppCheckBox* m_blackWhiteCheck = nullptr;

    std::array<std::array<ParamRow*, 8>, 3> m_mixerRows{};
    std::array<CurveGraphWidget*, 4> m_curveGraphs{};
    std::vector<std::function<void()>> m_syncControls;
};
