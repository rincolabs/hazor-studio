#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QPixmap>
#include <QMainWindow>
#include <QRect>
#include <QSize>
#include <QString>
#include <array>
#include <functional>
#include <memory>

#include "raw/RawDevelopParams.hpp"
#include "raw/RawLayerData.hpp"
#include "raw/RawPreviewCapabilities.hpp"
#include "raw/RawSession.hpp"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QToolButton;
class QThreadPool;
class QTimer;
class QWidget;
class QResizeEvent;
class QCloseEvent;

class AppCheckBox;
class HistogramCacheManager;
class HistogramWidget;
class CurvesEditorWidget;
class RawDevelopSlider;
class RawPreviewView;
class TitleBar;
class ZoomComboBox;
namespace curves { struct CurvesData; }

// Independent, non-modal RAW development window. It owns only a working copy
// of the PP3 and a display preview; ImageController remains the sole owner of
// the document mutation and undo command when Apply is confirmed.
class RawDevelopWindow : public QMainWindow {
    Q_OBJECT
public:
    // Deliberately carries no flat index: this window is independent, so the
    // layer it edits can be reordered or deleted while it is open. Resolving
    // the target is the owner's job (MainWindow::openHazorDevelop), keyed by
    // the layer's node.
    // `session`, when non-null, is an already-decoded RawSession for the same
    // file, adopted as this window's preview session instead of decoding it
    // again. MainWindow::openRawFileAsNewTab() hands over the session it just
    // used for the initial develop: opening a RAW used to cost two full decodes
    // because that one was dropped on the floor and this window immediately
    // opened its own.
    RawDevelopWindow(const RawLayerData& rawData,
                     const QImage& currentImage, QWidget* parent = nullptr,
                     std::unique_ptr<RawSession> session = nullptr);
    ~RawDevelopWindow() override;

    void setApplyInProgress(bool busy);
    void developFinished(bool ok, const QString& errorMessage);

    // Gives up the decoded preview session so Apply can render from it instead
    // of decoding the file a third time (RawDevelopService::adoptSession()).
    // Returns null when a preview render is in flight: waiting for it would
    // freeze the UI at the exact moment the user pressed Apply, and the only
    // cost of declining is the decode we were trying to skip. `tempPath`
    // receives, and transfers ownership of, an Embedded source's materialized
    // temp file when this window owned one.
    std::unique_ptr<RawSession> takeIdleSession(QString* tempPath);

signals:
    void applyRequested(RawDevelopParams params);

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
        // The processor's effective parameters after the render. Carries the
        // white balance the engine resolved for *every* method, and (when
        // hasResolvedToneCurve) an auto-exposure / Auto-Matched Curve result.
        RawDevelopParams resolvedParams;
        bool hasResolvedParams = false;
        bool hasResolvedToneCurve = false;
        // The 1:1 slice the engine's detail pipeline produced, and where it
        // sits in full-resolution coordinates. Null/empty unless this render
        // asked for one — see detailRegionRequest().
        QImage detailImage;
        QRect detailRect;
        // What was asked for, before the engine clamped it at the frame
        // edge. Kept separate from detailRect because comparing the wanted
        // region against the *delivered* one would re-request forever
        // wherever the two legitimately differ.
        QRect detailRequested;
        // The engine's own 1:1 frame size for this render. All the region
        // and zoom arithmetic lives in that space, never in the (budget-
        // capped) preview buffer's.
        QSize fullSize;
    };

    void buildUi();
    void buildTitleBar();
    QWidget* buildPreviewToolbar(QWidget* parent);
    QWidget* buildPreviewStatusBar(QWidget* parent);
    void applyTheme();
    QWidget* buildLightPanel();
    QWidget* buildColorPanel();
    QWidget* buildEffectsPanel();
    QWidget* buildCurvePanel();
    QWidget* buildColorMixerPanel();
    QWidget* buildColorGradingPanel();
    QWidget* buildDetailPanel();
    QWidget* buildOpticsPanel();

    RawDevelopSlider* addParamRow(class QVBoxLayout* layout, const QString& label,
                           double min, double max, double defaultValue,
                           int decimals, const QString& toolTip,
                           const std::function<double()>& currentValue,
                           const std::function<void(double)>& setValue,
                           RawPreviewUpdate previewUpdate = RawPreviewUpdate::Full,
                           bool logarithmic = false,
                           bool centerZeroLog = false, double logBase = 2.0);
    void addSection(class QVBoxLayout* layout, const QString& title,
                    QWidget* contents, const std::function<void()>& reset,
                    bool initiallyOpen = false);
    void syncControlsFromParams();
    void applyParamsChanged(RawPreviewUpdate update = RawPreviewUpdate::Full);
    // True when the PP3 has one of the four stages that exist only in the
    // engine's 1:1 pipeline turned on: sharpening, noise reduction, texture
    // (micro-contrast) or defringe.
    bool detailStagesActive() const;
    // The visible part of the frame, in full-resolution coordinates, when a
    // 1:1 slice is worth rendering; empty otherwise. Empty is the cheap
    // answer: a region forces the full-quality demosaic on the whole preview.
    QRect detailRegionRequest() const;
    // Re-renders when the wanted region moved (zoom, scroll, resize) without
    // any parameter having changed.
    void maybeRequestDetail();
    // Zoom is held as "fit" plus an explicit factor; effectiveZoom() is what
    // is actually on screen, which is what both the engine arithmetic and the
    // zoom readout need.
    double effectiveZoom() const;
    QSize frameSize() const;
    void setZoom(double zoom);
    void setFitToWindow();
    void zoomStep(int direction);
    void updateZoomControls();
    // Single place where the status line, the alerts and the transient
    // viewport overlay are derived from the engine's real capabilities.
    rawpreview::State previewState() const;
    void refreshPreviewStatus();
    void requestPreview();
    void startPreview();
    void previewFinished();
    void updatePreviewImage(const QImage& image);
    void updateDisplayedImage();
    void updateHistogram(const QImage& image);
    void reloadPresets();
    void applyPreset(int index);
    void savePreset();
    void loadCustomProfile();
    void updateProfileSummary();
    void resetAll();
    curves::CurvesData curveDataFromParams() const;
    void setCurvesFromEditor();

    RawLayerData m_rawData;
    RawDevelopParams m_params;
    QImage m_beforeImage;
    QImage m_previewImage;
    QImage m_detailImage;
    QRect m_detailRect;
    QRect m_detailRequested;
    QSize m_previewFullSize;
    QString m_previewSourcePath;
    bool m_ownsPreviewSourcePath = false;
    bool m_showBefore = false;
    bool m_splitView = false;
    bool m_clipShadows = false;
    bool m_clipHighlights = false;
    bool m_fitToWindow = true;
    double m_zoom = 1.0;
    bool m_syncing = false;
    bool m_applyInProgress = false;
    bool m_previewPending = false;
    quint64 m_previewGeneration = 0;
    RawPreviewUpdate m_pendingPreviewUpdates = RawPreviewUpdate::Full;
    quint64 m_histogramGeneration = 0;
    // Latest engine/source failure, cleared by the next successful render.
    QString m_previewError;
    // What the status bar last reported, so the viewport overlay only
    // repaints when the condition itself changes.
    QString m_lastNotice;
    // The split view's comparison half. The before frame never changes, so
    // the cache spares a full-resolution crop-and-scale on every render.
    QPixmap m_splitTile;
    // Geometry of the tile currently on the view, so a pan that stays inside
    // it does not trigger a recompose.
    QRect m_tileRect;
    QSize m_tileContent;
    QSize m_splitTileContent;
    QRect m_splitTileRect;
    bool m_splitTileClipShadows = false;
    bool m_splitTileClipHighlights = false;

    QFutureWatcher<PreviewResult>* m_previewWatcher = nullptr;
    QThreadPool* m_previewPool = nullptr;
    QTimer* m_previewDebounce = nullptr;
    std::unique_ptr<RawSession> m_previewSession;
    HistogramCacheManager* m_histogramCache = nullptr;

    TitleBar* m_titleBar = nullptr;
    RawPreviewView* m_preview = nullptr;
    HistogramWidget* m_histogram = nullptr;
    QLabel* m_metadataLabel = nullptr;
    QLabel* m_profileSummary = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_noticeIcon = nullptr;
    QLabel* m_noticeLabel = nullptr;
    QToolButton* m_noticeAction = nullptr;
    QProgressBar* m_previewProgress = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_presetCombo = nullptr;
    ZoomComboBox* m_zoomBox = nullptr;
    QToolButton* m_fitButton = nullptr;
    QToolButton* m_hundredButton = nullptr;
    QToolButton* m_beforeAfterButton = nullptr;
    QToolButton* m_splitButton = nullptr;
    QToolButton* m_shadowClipButton = nullptr;
    QToolButton* m_highlightClipButton = nullptr;
    QPushButton* m_applyButton = nullptr;

    RawDevelopSlider* m_temperatureSlider = nullptr;
    RawDevelopSlider* m_tintRow = nullptr;
    QComboBox* m_whiteBalanceCombo = nullptr;
    AppCheckBox* m_blackWhiteCheck = nullptr;

    std::array<std::array<RawDevelopSlider*, 8>, 3> m_mixerRows{};
    CurvesEditorWidget* m_curveEditor = nullptr;
    std::vector<std::function<void()>> m_syncControls;
};
