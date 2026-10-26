#include "RawDevelopWindow.hpp"

#include "core/AppPaths.hpp"
#include "ui/AppCheckBox.hpp"
#include "histogram/HistogramCacheManager.hpp"
#include "histogram/HistogramGenerator.hpp"
#include "raw/RawDevelopService.hpp"
#include "raw/RawEngine.hpp"
#include "raw/RawPreviewCapabilities.hpp"
#include "raw/RawSession.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/ColorTrack.hpp"
#include "ui/CurvesEditorWidget.hpp"
#include "ui/FramelessResizer.hpp"
#include "ui/HistogramWidget.hpp"
#include "ui/IconUtils.hpp"
#include "ui/RawDevelopSlider.hpp"
#include "ui/RawPreviewView.hpp"
#include "ui/TitleBar.hpp"
#include "ui/ZoomComboBox.hpp"

#include <QtConcurrent/QtConcurrent>

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QRadialGradient>
#include <QTabWidget>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// The two clipping markers. Functional signal colours rather than theme ones
// — they have to stay readable over any image — and the toolbar icons paint
// the same values the overlay does, so a button always matches its overlay.
const QColor kClipShadowMarker(30, 105, 255);
const QColor kClipHighlightMarker(255, 55, 35);

// Paints the clipping indicators in place. Shared because the 1:1 detail
// slice is a second image that has to carry the same markers as the preview
// it is drawn over, or toggling them would appear to skip the viewport.
void markClipping(QImage& image, bool shadows, bool highlights)
{
    if (!shadows && !highlights) {
        return;
    }
    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    for (int y = 0; y < image.height(); ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            uchar* px = row + x * 4;
            const int lo = std::min({px[0], px[1], px[2]});
            const int hi = std::max({px[0], px[1], px[2]});
            if (shadows && lo <= 3) {
                px[0] = kClipShadowMarker.red();
                px[1] = kClipShadowMarker.green();
                px[2] = kClipShadowMarker.blue();
            }
            if (highlights && hi >= 252) {
                px[0] = kClipHighlightMarker.red();
                px[1] = kClipHighlightMarker.green();
                px[2] = kClipHighlightMarker.blue();
            }
        }
    }
}

constexpr int kPreviewDebounceMs = 80;
constexpr int kPreviewMinEdge = 768;
constexpr int kPreviewMaxEdge = 1600;
// Zoom bounds and the stops the −/+ buttons walk. Narrower than the main
// canvas's: beyond these the engine preview carries no more detail.
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 8.0;
constexpr double kZoomStops[] = {0.0625, 0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0,
                                 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0};
// Margin kept between a fitted frame and the viewport edge, so the dotted
// pasteboard stays visible all around it.
constexpr int kFitMargin = 12;

// A tonal bar with the clipping marker capping the end it reports on: dark
// end for shadows, bright end for highlights.
QIcon clippingIcon(const QColor& marker, bool shadowEnd)
{
    const auto* theme = ThemeManager::instance()->current();
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient gradient(0, 0, 18, 0);
    gradient.setColorAt(0.0, theme->colorSurfaceDark);
    gradient.setColorAt(1.0, theme->colorTextBright);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(1, 3.5, 16, 11), 2, 2);
    painter.setBrush(marker);
    painter.drawRoundedRect(shadowEnd ? QRectF(1, 3.5, 4.5, 11) : QRectF(12.5, 3.5, 4.5, 11), 2, 2);
    return QIcon(pixmap);
}

// A frame split down the middle: the Before/After divider this button turns on.
QIcon splitViewIcon()
{
    const auto* theme = ThemeManager::instance()->current();
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(theme->colorTextPrimary, 1.2));
    painter.setBrush(theme->colorSurfaceDark);
    painter.drawRoundedRect(QRectF(1.5, 3.5, 15, 11), 2, 2);
    painter.setBrush(theme->colorTextPrimary);
    painter.setPen(Qt::NoPen);
    painter.drawRect(QRectF(9, 4.1, 7, 9.8));
    return QIcon(pixmap);
}

// Severity badge for the status bar: a warning triangle for what the engine
// cannot show, a dot for what it shows only approximately.
QPixmap noticeBadge(rawpreview::Severity severity)
{
    const auto* theme = ThemeManager::instance()->current();
    QColor color = theme->colorTextSecondary;
    if (severity == rawpreview::Severity::Unavailable) color = theme->colorWarning;
    if (severity == rawpreview::Severity::Error)       color = theme->colorDanger;

    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (severity == rawpreview::Severity::Approximate) {
        painter.drawEllipse(QPointF(7, 7), 4.0, 4.0);
        painter.setBrush(theme->colorSurface);
        painter.drawEllipse(QPointF(7, 7), 1.5, 1.5);
    } else {
        const QPointF triangle[3] = {QPointF(7, 1.5), QPointF(13, 12.5), QPointF(1, 12.5)};
        painter.drawPolygon(triangle, 3);
        painter.setPen(QPen(theme->colorSurface, 1.4));
        painter.drawLine(QPointF(7, 5), QPointF(7, 9));
        painter.drawPoint(QPointF(7, 11));
    }
    return pixmap;
}

QColor noticeAccent(rawpreview::Severity severity)
{
    const auto* theme = ThemeManager::instance()->current();
    switch (severity) {
        case rawpreview::Severity::Unavailable: return theme->colorWarning;
        case rawpreview::Severity::Error:       return theme->colorDanger;
        default:                                return theme->colorAccent;
    }
}

class DevelopSection final : public QWidget {
public:
    DevelopSection(const QString& title, QWidget* contents,
                   const std::function<void()>& reset, bool initiallyOpen,
                   QWidget* parent = nullptr)
        : QWidget(parent)
        , m_contents(contents)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* header = new QWidget(this);
        header->setObjectName(QStringLiteral("rawDevelopSectionHeader"));
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(8, 5, 6, 5);
        headerLayout->setSpacing(4);

        m_toggle = new QToolButton(header);
        m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_toggle->setText(title);
        m_toggle->setCheckable(true);
        m_toggle->setChecked(initiallyOpen);
        m_toggle->setArrowType(initiallyOpen ? Qt::DownArrow : Qt::RightArrow);
        m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        headerLayout->addWidget(m_toggle, 1);

        auto* resetButton = new QToolButton(header);
        resetButton->setText(QString::fromUtf8("↺"));
        resetButton->setToolTip(QObject::tr("Reset this panel"));
        headerLayout->addWidget(resetButton);

        layout->addWidget(header);
        layout->addWidget(contents);
        contents->setObjectName(QStringLiteral("rawDevelopSectionContents"));
        contents->setVisible(initiallyOpen);

        connect(m_toggle, &QToolButton::toggled, this, [this](bool open) {
            m_contents->setVisible(open);
            m_toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        connect(resetButton, &QToolButton::clicked, this, reset);
    }

private:
    QWidget* m_contents = nullptr;
    QToolButton* m_toggle = nullptr;
};

class RawColorWheel final : public QWidget {
public:
    explicit RawColorWheel(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(76, 76);
        setMaximumSize(104, 104);
        setCursor(Qt::CrossCursor);
    }

    std::array<double, 3> values() const { return m_values; }
    void setValues(const std::array<double, 3>& values)
    {
        m_values = values;
        update();
    }
    std::function<void()> changed;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF circle = QRectF(rect()).adjusted(4, 4, -4, -4);
        const QPointF center = circle.center();
        const double radius = std::min(circle.width(), circle.height()) / 2.0;

        // The interaction uses red at twelve o'clock and increases hue
        // clockwise. Qt's pie angles start at three o'clock and increase in
        // the opposite direction, so translate both origin and direction.
        for (int degrees = 0; degrees < 360; ++degrees) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor::fromHsv(degrees, 255, 255));
            painter.drawPie(circle, (90 - degrees) * 16, -2 * 16);
        }
        QRadialGradient fade(center, radius);
        fade.setColorAt(0.0, QColor(255, 255, 255, 255));
        fade.setColorAt(0.72, QColor(255, 255, 255, 0));
        fade.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.setBrush(fade);
        painter.drawEllipse(circle);
        painter.setPen(QPen(ThemeManager::instance()->current()->colorBorder, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circle);

        const auto [minimum, maximum] = std::minmax_element(m_values.begin(), m_values.end());
        const double range = *maximum - *minimum;
        double hue = 0.0;
        const double saturation = std::clamp(range / 200.0, 0.0, 1.0);
        if (range > 0.001) {
            QColor color;
            color.setRgbF((m_values[0] - *minimum) / range,
                          (m_values[1] - *minimum) / range,
                          (m_values[2] - *minimum) / range);
            // getHsvF() bails out without writing anything if *any* of its
            // three out-parameters is null, so saturation/value have to be
            // taken even though only the hue is wanted here.
            float colorHue = 0.0f;
            float unusedSaturation = 0.0f;
            float unusedValue = 0.0f;
            color.getHsvF(&colorHue, &unusedSaturation, &unusedValue);
            // Hue is -1 for an achromatic colour, which the `range` guard
            // above already excludes; clamp anyway rather than wrap to 1.0.
            hue = colorHue < 0.0f ? 0.0 : colorHue;
        }
        constexpr double kPi = 3.14159265358979323846;
        const double angle = hue * 2.0 * kPi - kPi / 2.0;
        const QPointF handle = center + QPointF(std::cos(angle), std::sin(angle)) * (saturation * radius);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(30, 30, 30));
        painter.drawEllipse(handle, 4, 4);
    }

    void mousePressEvent(QMouseEvent* event) override { setFromPosition(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton)
            setFromPosition(event->position());
    }

private:
    void setFromPosition(const QPointF& point)
    {
        const QRectF circle = QRectF(rect()).adjusted(4, 4, -4, -4);
        const QPointF delta = point - circle.center();
        const double radius = std::min(circle.width(), circle.height()) / 2.0;
        const double distance = std::min(1.0, std::hypot(delta.x(), delta.y()) / radius);
        constexpr double kPi = 3.14159265358979323846;
        double hue = std::atan2(delta.y(), delta.x()) / (2.0 * kPi) + 0.25;
        if (hue < 0.0) hue += 1.0;
        const QColor color = QColor::fromHsvF(hue, 1.0, 1.0);
        // Color Toning's RGB mixer is neutral at 0/0/0. Scale a fully
        // saturated signed RGB direction by the distance from the centre;
        // mapping HSV white to +100/+100/+100 would make the centre non-neutral.
        m_values = {distance * (color.redF() * 2.0 - 1.0) * 100.0,
                    distance * (color.greenF() * 2.0 - 1.0) * 100.0,
                    distance * (color.blueF() * 2.0 - 1.0) * 100.0};
        update();
        if (changed) changed();
    }

    std::array<double, 3> m_values{};
};

QString shutterText(double seconds)
{
    if (seconds <= 0.0) return QObject::tr("—");
    if (seconds >= 1.0) return QObject::tr("%1 s").arg(seconds, 0, 'f', seconds < 10.0 ? 1 : 0);
    return QObject::tr("1/%1 s").arg(qRound(1.0 / seconds));
}

} // namespace

RawDevelopWindow::RawDevelopWindow(const RawLayerData& rawData,
                                   const QImage& currentImage, QWidget* parent,
                                   std::unique_ptr<RawSession> session)
    : QMainWindow(parent)
    , m_rawData(rawData)
    , m_params(rawData.state.pp3.isEmpty()
        ? RawDevelopParams::cameraDefaults()
        : RawDevelopParams::fromPP3String(rawData.state.pp3.toStdString()))
    , m_beforeImage(currentImage)
    , m_previewImage(currentImage)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Window, true);
    setWindowTitle(tr("Hazor Develop"));
    resize(1480, 920);
    setMinimumSize(980, 620);
    // Independent top-level window: Qt never centers it on its parent on its
    // own, so without this it lands wherever the window manager defaults to
    // (typically the screen's top-left corner).
    const QRect anchor = parent ? parent->frameGeometry()
        : (screen() ? screen()->availableGeometry() : QRect());
    if (!anchor.isEmpty())
        move(anchor.center() - QPoint(width() / 2, height() / 2));
    buildTitleBar();

    // RawSession and its StagedImageProcessor are serialized on one persistent
    // worker. Keeping the thread alive also keeps the decoded RAW and rtengine
    // caches warm across slider changes.
    m_previewPool = new QThreadPool(this);
    m_previewPool->setMaxThreadCount(1);
    m_previewPool->setExpiryTimeout(-1);

    // Adopted before the first requestPreview() below, so startPreview()'s
    // worker finds m_previewSession already set and never opens the file. Safe
    // to hand a session created on another thread to this pool's thread: nothing
    // in rtengine's ImageSource is bound to the thread that decoded it, only to
    // being driven by one pipeline at a time (RawSession.hpp).
    m_previewSession = std::move(session);

    buildUi();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &RawDevelopWindow::applyTheme);
    syncControlsFromParams();
    updatePreviewImage(m_previewImage);
    updateProfileSummary();
    requestPreview();
}

std::unique_ptr<RawSession> RawDevelopWindow::takeIdleSession(QString* tempPath)
{
    if (!m_previewSession)
        return nullptr;
    // A preview in flight is driving this very session on the pool thread;
    // handing it over would break RawSession's "externally serialized" contract,
    // and blocking until it finishes would stall the UI thread. Decline instead.
    if ((m_previewWatcher && m_previewWatcher->isRunning())
        || m_previewPool->activeThreadCount() > 0) {
        return nullptr;
    }
    if (tempPath && m_ownsPreviewSourcePath) {
        // Ownership moves with the session: ~RawDevelopWindow() must stop
        // deleting a file the new owner may still need to re-open from. This
        // window also FORGETS the path, not just its ownership — Apply can still
        // be refused after this point (locked layer), leaving the window open
        // with neither a session nor a file it is allowed to assume still
        // exists. Empty is the one state startPreview() handles correctly: it
        // materializes the embedded bytes again.
        *tempPath = m_previewSourcePath;
        m_ownsPreviewSourcePath = false;
        m_previewSourcePath.clear();
    }
    // A queued render would re-open the file we just gave away, on a window that
    // is about to close. Nothing sensible is left to preview without a session.
    if (m_previewDebounce)
        m_previewDebounce->stop();
    m_previewPending = false;
    return std::move(m_previewSession);
}

RawDevelopWindow::~RawDevelopWindow()
{
    if (m_previewWatcher && m_previewWatcher->isRunning())
        m_previewWatcher->waitForFinished();
    if (m_previewPool)
        m_previewPool->waitForDone();
    m_previewSession.reset();
    if (m_ownsPreviewSourcePath && !m_previewSourcePath.isEmpty())
        QFile::remove(m_previewSourcePath);
}

void RawDevelopWindow::buildUi()
{
    auto* theme = ThemeManager::instance()->current();
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("rawDevelopRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(theme->spaceMD, theme->spaceMD, theme->spaceMD, theme->spaceMD);
    rootLayout->setSpacing(theme->spaceMD);
    setCentralWidget(root);
    // The root layout's own margin is the strip the resizer turns back into
    // window edges; no child reaches into it.
    if (m_titleBar)
        FramelessResizer::install(root);

    auto* body = new QHBoxLayout;
    body->setSpacing(theme->spaceMD);
    rootLayout->addLayout(body, 1);

    auto* previewPane = new QWidget(root);
    previewPane->setObjectName(QStringLiteral("rawDevelopPreviewPane"));
    auto* previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(theme->spaceSM);

    m_preview = new RawPreviewView(previewPane);
    m_preview->setObjectName(QStringLiteral("rawDevelopPreview"));
    m_preview->setPlaceholderText(tr("Preparing RAW preview…"));
    previewLayout->addWidget(m_preview, 1);
    previewLayout->addWidget(buildPreviewToolbar(previewPane));
    previewLayout->addWidget(buildPreviewStatusBar(previewPane));
    body->addWidget(previewPane, 1);

    auto* side = new QWidget(root);
    side->setObjectName(QStringLiteral("rawDevelopPanel"));
    side->setMinimumWidth(380);
    side->setMaximumWidth(480);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(theme->spaceMD, theme->spaceSM,
                                   theme->spaceMD, theme->spaceSM);
    sideLayout->setSpacing(theme->spaceSM);

    m_histogram = new HistogramWidget(side);
    m_histogram->setHasDocument(true);
    m_histogram->setChannel(HistogramChannel::Colors);
    sideLayout->addWidget(m_histogram);

    m_histogramCache = new HistogramCacheManager(this);
    connect(m_histogramCache, &HistogramCacheManager::ready, this, [this]() {
        if (!m_histogram)
            return;
        const HistogramData& data = m_histogramCache->data();
        m_histogram->setData(data);
        if (m_curveEditor)
            m_curveEditor->setHistogramData(data);
        m_histogram->setStale(false);
    });
    connect(m_histogramCache, &HistogramCacheManager::computingChanged,
            m_histogram, &HistogramWidget::setStale);

    m_metadataLabel = new QLabel(side);
    m_metadataLabel->setWordWrap(true);
    if (m_rawData.source) {
        const auto& meta = m_rawData.source->meta;
        m_metadataLabel->setText(tr("ISO %1   %2 mm   f/%3   %4")
            .arg(meta.isoSpeed > 0 ? QString::number(meta.isoSpeed) : tr("—"))
            .arg(meta.focalLengthMm > 0 ? QString::number(meta.focalLengthMm, 'f', 0) : tr("—"))
            .arg(meta.fNumber > 0 ? QString::number(meta.fNumber, 'f', 1) : tr("—"))
            .arg(shutterText(meta.shutterSpeedSeconds)));
    }
    sideLayout->addWidget(m_metadataLabel);

    auto* headActions = new QHBoxLayout;
    auto* autoButton = new QToolButton(side);
    autoButton->setText(tr("Auto"));
    autoButton->setToolTip(tr("Use automatic exposure and white balance"));
    headActions->addWidget(autoButton);
    m_blackWhiteCheck = new AppCheckBox(tr("B&W"), side);
    headActions->addWidget(m_blackWhiteCheck);
    headActions->addStretch();
    sideLayout->addLayout(headActions);

    auto* profileRow = new QHBoxLayout;
    profileRow->addWidget(new QLabel(tr("Camera profile"), side));
    m_profileCombo = new QComboBox(side);
    m_profileCombo->addItem(tr("Camera matched"), static_cast<int>(RawDevelopParams::InputProfileMode::CameraMatched));
    m_profileCombo->addItem(tr("Embedded"), static_cast<int>(RawDevelopParams::InputProfileMode::Embedded));
    m_profileCombo->addItem(tr("Camera matrix"), static_cast<int>(RawDevelopParams::InputProfileMode::CameraMatrix));
    m_profileCombo->addItem(tr("None"), static_cast<int>(RawDevelopParams::InputProfileMode::None));
    m_profileCombo->addItem(tr("Custom profile…"), -1);
    profileRow->addWidget(m_profileCombo, 1);
    auto* browseProfile = new QToolButton(side);
    browseProfile->setText(QString::fromUtf8("…"));
    browseProfile->setToolTip(tr("Choose a DCP or ICC input profile"));
    profileRow->addWidget(browseProfile);
    sideLayout->addLayout(profileRow);
    m_profileSummary = new QLabel(side);
    m_profileSummary->setWordWrap(true);
    m_profileSummary->setStyleSheet(QStringLiteral("color: %1;").arg(theme->colorTextSecondary.name()));
    sideLayout->addWidget(m_profileSummary);

    auto* presets = new QHBoxLayout;
    presets->addWidget(new QLabel(tr("Preset"), side));
    m_presetCombo = new QComboBox(side);
    presets->addWidget(m_presetCombo, 1);
    auto* savePresetButton = new QToolButton(side);
    savePresetButton->setText(QString::fromUtf8("＋"));
    savePresetButton->setToolTip(tr("Save current develop settings as a preset"));
    presets->addWidget(savePresetButton);
    sideLayout->addLayout(presets);
    reloadPresets();

    auto* adjustmentScroll = new QScrollArea(side);
    adjustmentScroll->setObjectName(QStringLiteral("rawDevelopAdjustments"));
    adjustmentScroll->setWidgetResizable(true);
    adjustmentScroll->setFrameShape(QFrame::NoFrame);
    adjustmentScroll->viewport()->setObjectName(QStringLiteral("rawDevelopAdjustmentsViewport"));
    auto* adjustments = new QWidget(adjustmentScroll);
    adjustments->setObjectName(QStringLiteral("rawDevelopAdjustmentsContent"));
    auto* adjustmentsLayout = new QVBoxLayout(adjustments);
    adjustmentsLayout->setContentsMargins(0, 0, 0, 0);
    adjustmentsLayout->setSpacing(1);
    addSection(adjustmentsLayout, tr("Light"), buildLightPanel(), [this]() {
        m_params.setLight(RawDevelopParams::cameraDefaults().light());
        syncControlsFromParams(); applyParamsChanged();
    }, true);
    addSection(adjustmentsLayout, tr("Color"), buildColorPanel(), [this]() {
        auto defaults = RawDevelopParams::cameraDefaults();
        m_params.setWhiteBalance(defaults.whiteBalance());
        m_params.setColor(defaults.color());
        syncControlsFromParams(); applyParamsChanged();
    }, true);
    addSection(adjustmentsLayout, tr("Effects"), buildEffectsPanel(), [this]() {
        m_params.setEffects(RawDevelopParams::cameraDefaults().effects());
        syncControlsFromParams(); applyParamsChanged();
    });
    addSection(adjustmentsLayout, tr("Curve"), buildCurvePanel(), [this]() {
        const auto defaults = RawDevelopParams::cameraDefaults();
        for (int i = 0; i < 4; ++i)
            m_params.setCurve(static_cast<RawDevelopParams::CurveChannel>(i),
                              defaults.curve(static_cast<RawDevelopParams::CurveChannel>(i)));
        syncControlsFromParams(); applyParamsChanged();
    });
    addSection(adjustmentsLayout, tr("Color Mixer"), buildColorMixerPanel(), [this]() {
        m_params.setColorMixer(RawDevelopParams::cameraDefaults().colorMixer());
        syncControlsFromParams(); applyParamsChanged();
    });
    addSection(adjustmentsLayout, tr("Color Grading"), buildColorGradingPanel(), [this]() {
        m_params.setColorGrading(RawDevelopParams::cameraDefaults().colorGrading());
        syncControlsFromParams(); applyParamsChanged();
    });
    addSection(adjustmentsLayout, tr("Detail"), buildDetailPanel(), [this]() {
        m_params.setDetail(RawDevelopParams::cameraDefaults().detail());
        syncControlsFromParams(); applyParamsChanged();
    });
    addSection(adjustmentsLayout, tr("Optics"), buildOpticsPanel(), [this]() {
        m_params.setOptics(RawDevelopParams::cameraDefaults().optics());
        syncControlsFromParams(); applyParamsChanged();
    });
    adjustmentsLayout->addStretch();
    adjustmentScroll->setWidget(adjustments);
    sideLayout->addWidget(adjustmentScroll, 1);
    body->addWidget(side);

    // The window actions are separated from the preview toolbar and its
    // alerts by a rule, so the three bands read as three different things.
    auto* actionsSeparator = new QFrame(root);
    actionsSeparator->setObjectName(QStringLiteral("rawDevelopSeparator"));
    actionsSeparator->setFrameShape(QFrame::HLine);
    actionsSeparator->setFrameShadow(QFrame::Plain);
    actionsSeparator->setFixedHeight(theme->borderThinWidth);
    rootLayout->addWidget(actionsSeparator);

    // Same roles, names and therefore the same metrics the dialogs use:
    // #cancelBtn for the secondary actions, #okBtn for the primary one.
    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(theme->spaceSM);
    auto* resetAllButton = new QPushButton(tr("Restore Defaults"), root);
    resetAllButton->setObjectName(QStringLiteral("cancelBtn"));
    bottom->addWidget(resetAllButton);
    bottom->addStretch();
    auto* cancelButton = new QPushButton(tr("Cancel"), root);
    cancelButton->setObjectName(QStringLiteral("cancelBtn"));
    bottom->addWidget(cancelButton);
    m_applyButton = new QPushButton(tr("Apply"), root);
    m_applyButton->setObjectName(QStringLiteral("okBtn"));
    m_applyButton->setDefault(true);
    bottom->addWidget(m_applyButton);
    rootLayout->addLayout(bottom);

    m_previewDebounce = new QTimer(this);
    m_previewDebounce->setSingleShot(true);
    m_previewDebounce->setInterval(kPreviewDebounceMs);
    m_previewWatcher = new QFutureWatcher<PreviewResult>(this);
    connect(m_previewDebounce, &QTimer::timeout, this, &RawDevelopWindow::startPreview);
    connect(m_previewWatcher, &QFutureWatcher<PreviewResult>::finished,
            this, &RawDevelopWindow::previewFinished);
    connect(m_zoomBox, &ZoomComboBox::zoomRequested, this,
            [this](float zoom) { setZoom(zoom); });
    connect(m_fitButton, &QToolButton::clicked, this, &RawDevelopWindow::setFitToWindow);
    connect(m_hundredButton, &QToolButton::clicked, this, [this]() { setZoom(1.0); });
    // Panning moves which part of the frame the 1:1 slice has to cover. The
    // preview debounce is what keeps a drag from queueing a render per pixel.
    connect(m_preview, &RawPreviewView::scrolled, this, [this]() {
        // The composed tile carries a margin around the viewport, so a short
        // pan is already painted and only the engine's 1:1 slice has to
        // follow it.
        const QRect visible = QRect(m_preview->scrollOffset(), m_preview->size())
            .intersected(QRect(QPoint(0, 0), m_preview->contentSize()));
        if (m_tileContent != m_preview->contentSize() || !m_tileRect.contains(visible))
            updateDisplayedImage();
        maybeRequestDetail();
    });
    connect(m_preview, &RawPreviewView::zoomStepRequested, this,
            [this](int steps) { zoomStep(steps > 0 ? 1 : -1); });
    connect(m_beforeAfterButton, &QToolButton::toggled, this, [this](bool before) {
        m_showBefore = before;
        // Before/After and Split are two ways of looking at the same pair of
        // frames; showing both at once would leave the button lying about
        // which half is which.
        if (before && m_splitButton->isChecked())
            m_splitButton->setChecked(false);
        updateDisplayedImage();
        updateHistogram(before ? m_beforeImage : m_previewImage);
        refreshPreviewStatus();
    });
    connect(m_splitButton, &QToolButton::toggled, this, [this](bool split) {
        m_splitView = split;
        if (split && m_beforeAfterButton->isChecked())
            m_beforeAfterButton->setChecked(false);
        m_preview->setSplitEnabled(split);
        updateDisplayedImage();
        refreshPreviewStatus();
    });
    connect(m_shadowClipButton, &QToolButton::toggled, this, [this](bool on) {
        m_clipShadows = on; updateDisplayedImage();
    });
    connect(m_highlightClipButton, &QToolButton::toggled, this, [this](bool on) {
        m_clipHighlights = on; updateDisplayedImage();
    });
    connect(autoButton, &QToolButton::clicked, this, [this]() {
        auto light = m_params.light(); light.autoExposure = true; m_params.setLight(light);
        // "autitcgreen" is the engine's temperature-correlation auto white
        // balance. There is no method literally called "Auto": a string the
        // method cascade does not know silently degrades to the generic
        // temperature path, i.e. it would pin the shot to whatever Kelvin
        // value the sliders happened to carry.
        auto wb = m_params.whiteBalance(); wb.method = "autitcgreen"; m_params.setWhiteBalance(wb);
        syncControlsFromParams();
        applyParamsChanged(RawPreviewUpdate::Exposure | RawPreviewUpdate::WhiteBalance);
    });
    connect(m_blackWhiteCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) return;
        auto color = m_params.color(); color.blackAndWhite = enabled; m_params.setColor(color); applyParamsChanged(RawPreviewUpdate::Color);
    });
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) return;
        const int mode = m_profileCombo->itemData(index).toInt();
        if (mode < 0) {
            loadCustomProfile();
            return;
        }
        m_params.setInputProfileMode(static_cast<RawDevelopParams::InputProfileMode>(mode));
        updateProfileSummary(); applyParamsChanged();
    });
    connect(browseProfile, &QToolButton::clicked, this, &RawDevelopWindow::loadCustomProfile);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::activated), this, &RawDevelopWindow::applyPreset);
    connect(savePresetButton, &QToolButton::clicked, this, &RawDevelopWindow::savePreset);
    connect(resetAllButton, &QPushButton::clicked, this, &RawDevelopWindow::resetAll);
    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        if (!m_applyInProgress) emit applyRequested(m_params);
    });

    applyTheme();
    updateZoomControls();
    refreshPreviewStatus();
}

void RawDevelopWindow::buildTitleBar()
{
    // Same bar the main window wears, so this window follows the theme
    // instead of the desktop's own decoration. Where the platform cannot
    // support a frameless window (see TitleBar::isSupportedPlatform) the
    // native title bar is kept rather than drawing a second, dead one.
    if (!TitleBar::isSupportedPlatform())
        return;

    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    m_titleBar = new TitleBar(this);
    m_titleBar->setTitleText(windowTitle());
    setMenuWidget(m_titleBar);

    connect(m_titleBar, &TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeClicked, this, [this]() {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
        m_titleBar->updateMaximizeButton(isMaximized());
    });
    connect(m_titleBar, &TitleBar::closeClicked, this, &QWidget::close);

    // A frameless window loses the invisible resize border with the frame;
    // both widgets that reach a window edge hand those pixels back.
    FramelessResizer::install(m_titleBar);
}

QWidget* RawDevelopWindow::buildPreviewToolbar(QWidget* parent)
{
    const auto* theme = ThemeManager::instance()->current();
    auto* bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("rawDevelopPreviewTools"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme->spaceXS);

    auto makeButton = [bar](const QString& toolTip, bool checkable) {
        auto* button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("rawDevelopToolButton"));
        button->setToolTip(toolTip);
        button->setCheckable(checkable);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    // ── Zoom group ──
    auto* zoomOut = makeButton(tr("Zoom out"), false);
    zoomOut->setText(QString::fromUtf8("−"));
    layout->addWidget(zoomOut);

    m_zoomBox = new ZoomComboBox(bar);
    m_zoomBox->setZoomRange(static_cast<float>(kMinZoom), static_cast<float>(kMaxZoom));
    m_zoomBox->setToolTip(tr("Preview zoom"));
    layout->addWidget(m_zoomBox);

    auto* zoomIn = makeButton(tr("Zoom in"), false);
    zoomIn->setText(QStringLiteral("+"));
    layout->addWidget(zoomIn);

    m_fitButton = makeButton(tr("Fit preview to window"), true);
    m_fitButton->setIcon(makeIcon(QStringLiteral(":/icons/zoom-fit.png")));
    m_fitButton->setIconSize(QSize(18, 18));
    layout->addWidget(m_fitButton);

    m_hundredButton = makeButton(tr("Show at 100%"), true);
    m_hundredButton->setIcon(makeIcon(QStringLiteral(":/icons/zoom-1-1.png")));
    m_hundredButton->setIconSize(QSize(18, 18));
    layout->addWidget(m_hundredButton);

    layout->addStretch();

    // ── View group ──
    m_beforeAfterButton = makeButton(tr("Show the image as it stands, without the current edits"), true);
    m_beforeAfterButton->setText(tr("Before"));
    layout->addWidget(m_beforeAfterButton);

    m_splitButton = makeButton(tr("Split view — drag the divider to compare before and after"), true);
    m_splitButton->setIcon(splitViewIcon());
    m_splitButton->setIconSize(QSize(18, 18));
    layout->addWidget(m_splitButton);

    m_shadowClipButton = makeButton(tr("Shadow clipping — clipped shadows are marked in blue"), true);
    m_shadowClipButton->setIcon(clippingIcon(kClipShadowMarker, true));
    m_shadowClipButton->setIconSize(QSize(18, 18));
    layout->addWidget(m_shadowClipButton);

    m_highlightClipButton = makeButton(tr("Highlight clipping — clipped highlights are marked in red"), true);
    m_highlightClipButton->setIcon(clippingIcon(kClipHighlightMarker, false));
    m_highlightClipButton->setIconSize(QSize(18, 18));
    layout->addWidget(m_highlightClipButton);

    connect(zoomOut, &QToolButton::clicked, this, [this]() { zoomStep(-1); });
    connect(zoomIn, &QToolButton::clicked, this, [this]() { zoomStep(1); });
    return bar;
}

QWidget* RawDevelopWindow::buildPreviewStatusBar(QWidget* parent)
{
    const auto* theme = ThemeManager::instance()->current();
    auto* bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("rawDevelopStatusBar"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme->spaceSM);

    m_statusLabel = new QLabel(bar);
    m_statusLabel->setObjectName(QStringLiteral("rawDevelopStatus"));
    // Neither band may push the window wider than the panels want it; the
    // full text of an alert is always on its tooltip and in the overlay.
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setMinimumWidth(0);
    layout->addWidget(m_statusLabel);

    // Indeterminate: the engine reports no progress fraction for a preview
    // render. It is only ever visible while one is actually running.
    m_previewProgress = new QProgressBar(bar);
    m_previewProgress->setObjectName(QStringLiteral("rawDevelopProgress"));
    m_previewProgress->setRange(0, 0);
    m_previewProgress->setTextVisible(false);
    m_previewProgress->setFixedSize(110, 4);
    m_previewProgress->setVisible(false);
    layout->addWidget(m_previewProgress);

    layout->addStretch();

    m_noticeIcon = new QLabel(bar);
    m_noticeIcon->setFixedSize(14, 14);
    m_noticeIcon->setVisible(false);
    layout->addWidget(m_noticeIcon);

    m_noticeLabel = new QLabel(bar);
    m_noticeLabel->setObjectName(QStringLiteral("rawDevelopNotice"));
    m_noticeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_noticeLabel->setMinimumWidth(0);
    m_noticeLabel->setVisible(false);
    layout->addWidget(m_noticeLabel);

    m_noticeAction = new QToolButton(bar);
    m_noticeAction->setObjectName(QStringLiteral("rawDevelopNoticeAction"));
    m_noticeAction->setText(tr("View at 100%"));
    m_noticeAction->setAutoRaise(true);
    m_noticeAction->setFocusPolicy(Qt::NoFocus);
    m_noticeAction->setVisible(false);
    layout->addWidget(m_noticeAction);

    connect(m_noticeAction, &QToolButton::clicked, this, [this]() { setZoom(1.0); });
    return bar;
}

void RawDevelopWindow::applyTheme()
{
    const auto* theme = ThemeManager::instance()->current();
    if (!theme)
        return;

    // Hazor Develop uses one continuous surface for the whole side panel.
    // Keeping every structural container on that same color prevents
    // transparent labels and row widgets from revealing alternating bands;
    // editable controls still retain the global theme's own contrast.
    if (auto* root = centralWidget()) {
        root->setStyleSheet(QStringLiteral(
            // The window chrome sits on the same surface the dialogs and the
            // side panel use; only the image viewport is darker. A rule
            // between the two columns is what keeps them legible apart.
            "QWidget#rawDevelopRoot { background: %1; color: %2; }"
            "QWidget#rawDevelopPreviewPane { background: %1; }"
            "QWidget#rawDevelopPanel { background: %1; border-left: %3px solid %4; }"
            "QWidget#rawDevelopSectionHeader { background: %1; border-bottom: %3px solid %4; }"
            "QWidget#rawDevelopSectionContents { background: %1; }"
            "QWidget#rawDevelopPanel QLabel { background: transparent; border: none; }")
            .arg(theme->colorSurface.name(), theme->colorTextPrimary.name())
            .arg(theme->borderThinWidth)
            .arg(theme->colorBorder.name()));
    }

    if (auto* tools = findChild<QWidget*>(QStringLiteral("rawDevelopPreviewTools"))) {
        tools->setStyleSheet(QStringLiteral(
            "QToolButton#rawDevelopToolButton {"
            "  background: transparent; color: %1; border: %2px solid transparent;"
            "  border-radius: %3px; padding: 2px 6px; }"
            "QToolButton#rawDevelopToolButton:hover { background: %4; color: %5; }"
            "QToolButton#rawDevelopToolButton:checked {"
            "  background: %6; border-color: %7; color: %5; }")
            .arg(theme->colorTextSecondary.name())
            .arg(theme->borderThinWidth)
            .arg(theme->radiusSM)
            .arg(theme->colorSurfaceHover.name(), theme->colorTextBright.name(),
                 theme->colorSurfaceSelected.name(), theme->colorBorderFocus.name()));
    }

    if (m_shadowClipButton)
        m_shadowClipButton->setIcon(clippingIcon(kClipShadowMarker, true));
    if (m_highlightClipButton)
        m_highlightClipButton->setIcon(clippingIcon(kClipHighlightMarker, false));
    if (m_splitButton)
        m_splitButton->setIcon(splitViewIcon());

    if (auto* separator = findChild<QFrame*>(QStringLiteral("rawDevelopSeparator"))) {
        separator->setStyleSheet(QStringLiteral("QFrame#rawDevelopSeparator { background: %1; border: none; }")
            .arg(theme->colorBorder.name()));
    }

    if (m_previewProgress) {
        m_previewProgress->setStyleSheet(QStringLiteral(
            "QProgressBar#rawDevelopProgress { background: %1; border: none; border-radius: %2px; }"
            "QProgressBar#rawDevelopProgress::chunk { background: %3; border-radius: %2px; }")
            .arg(theme->colorSurfaceDark.name())
            .arg(theme->radiusSM)
            .arg(theme->colorAccent.name()));
    }

    if (m_noticeAction) {
        m_noticeAction->setStyleSheet(QStringLiteral(
            "QToolButton#rawDevelopNoticeAction { background: transparent; color: %1;"
            "  border: %2px solid %3; border-radius: %4px; padding: 1px 6px; }"
            "QToolButton#rawDevelopNoticeAction:hover { background: %5; color: %6; }")
            .arg(theme->colorAccent.name())
            .arg(theme->borderThinWidth)
            .arg(theme->colorBorder.name())
            .arg(theme->radiusSM)
            .arg(theme->colorSurfaceHover.name(), theme->colorTextBright.name()));
    }

    if (auto* adjustmentScroll = findChild<QScrollArea*>(QStringLiteral("rawDevelopAdjustments"))) {
        adjustmentScroll->setStyleSheet(QStringLiteral(
            "QScrollArea#rawDevelopAdjustments { background: %1; border: none; }"
            "QWidget#rawDevelopAdjustmentsViewport { background: %1; }"
            "QWidget#rawDevelopAdjustmentsContent { background: %1; }")
            .arg(theme->colorSurface.name()));
    }

    const QString secondary = QStringLiteral("color: %1;").arg(theme->colorTextSecondary.name());
    if (m_statusLabel)
        m_statusLabel->setStyleSheet(secondary);
    if (m_noticeLabel)
        m_noticeLabel->setStyleSheet(secondary);
    if (m_profileSummary)
        m_profileSummary->setStyleSheet(secondary);
    // Severity badges and the overlay accent are theme colours too.
    refreshPreviewStatus();
}

RawDevelopSlider* RawDevelopWindow::addParamRow(QVBoxLayout* layout, const QString& label,
                                        double min, double max, double defaultValue,
                                        int decimals, const QString& toolTip,
                                        const std::function<double()>& currentValue,
                                        const std::function<void(double)>& setValue,
                                        RawPreviewUpdate previewUpdate,
                                        bool logarithmic, bool centerZeroLog, double logBase)
{
    auto* row = new RawDevelopSlider(label, min, max, defaultValue, decimals,
                                     logarithmic, centerZeroLog, logBase);
    row->setToolTip(toolTip);
    layout->addWidget(row);
    connect(row, &RawDevelopSlider::valueChanged, this, [this, setValue, previewUpdate](double value) {
        if (m_syncing) return;
        setValue(value); applyParamsChanged(previewUpdate);
    });
    m_syncControls.push_back([row, currentValue]() { row->setValue(currentValue()); });
    return row;
}

void RawDevelopWindow::addSection(QVBoxLayout* layout, const QString& title, QWidget* contents,
                                  const std::function<void()>& reset, bool initiallyOpen)
{
    layout->addWidget(new DevelopSection(title, contents, reset, initiallyOpen));
}

QWidget* RawDevelopWindow::buildLightPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 6);
    layout->setSpacing(1);
    // Exposure, Contrast, Whites and Blacks are the exact fields the engine's
    // auto-exposure pass (getAutoExp(), see Auto button below) recomputes and
    // overrides on every render while autoExposure stays set. Editing any of
    // them manually has to clear autoExposure, or the slider would look live
    // but keep losing to the auto-computed value on the next render.
    // The engine's own Exposure/Black adjusters use a log scale anchored at
    // zero: 0 sits at the exact slider middle despite the asymmetric range,
    // with each half log-curved. Replicate both the range and that
    // centering (RawDevelopSlider's centerZeroLog), not just the range.
    addParamRow(layout, tr("Exposure"), -5, 12, 0, 2, tr("Exposure compensation, in EV."),
        [this] { return m_params.light().exposureEv; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.exposureEv = value; m_params.setLight(s); },
        RawPreviewUpdate::Exposure, /*logarithmic=*/false, /*centerZeroLog=*/true, /*logBase=*/2.0);
    addParamRow(layout, tr("Contrast"), -100, 100, 0, 0, tr("Tone curve contrast."),
        [this] { return m_params.light().contrast; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.contrast = qRound(value); m_params.setLight(s); }, RawPreviewUpdate::Exposure);
    addParamRow(layout, tr("Highlights"), 0, 100, 0, 0, tr("Highlight recovery strength."),
        [this] { return m_params.light().highlights; }, [this](double value) { auto s = m_params.light(); s.highlights = qRound(value); m_params.setLight(s); }, RawPreviewUpdate::ShadowsHighlights);
    addParamRow(layout, tr("Shadows"), 0, 100, 0, 0, tr("Shadow recovery strength."),
        [this] { return m_params.light().shadows; }, [this](double value) { auto s = m_params.light(); s.shadows = qRound(value); m_params.setLight(s); }, RawPreviewUpdate::ShadowsHighlights);
    addParamRow(layout, tr("Whites"), 0, 500, 0, 0, tr("Engine mapping: Highlight Compression. Positive values recover, rather than raise, white detail."),
        [this] { return m_params.light().whiteRecovery; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.whiteRecovery = qRound(value); m_params.setLight(s); }, RawPreviewUpdate::Exposure);
    addParamRow(layout, tr("Blacks"), -16384, 32768, 0, 0, tr("Engine mapping: Black Level; this is not Adobe's normalized Blacks control."),
        [this] { return m_params.light().blackPoint; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.blackPoint = qRound(value); m_params.setLight(s); },
        RawPreviewUpdate::Exposure, /*logarithmic=*/false, /*centerZeroLog=*/true, /*logBase=*/10.0);
    return panel;
}

QWidget* RawDevelopWindow::buildColorPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 6);
    layout->setSpacing(1);
    auto* wbRow = new QHBoxLayout;
    wbRow->setContentsMargins(8, 1, 8, 3);
    wbRow->addWidget(new QLabel(tr("White balance"), panel));
    m_whiteBalanceCombo = new QComboBox(panel);
    // Every value here has to be one of WBParams::getWbEntries()' own names:
    // the engine recognizes nothing else. An unknown string is not rejected,
    // it just falls through to the generic temperature+green path, which
    // makes a preset look applied while changing nothing — which is why
    // "Fluorescent" and "Flash" name the specific illuminants the engine
    // actually carries spectral data for (ColorTemp::spectMap) instead of the
    // generic Camera Raw labels.
    const QList<QPair<QString, QString>> methods = {
        {tr("As Shot"), QStringLiteral("Camera")},
        {tr("Auto"), QStringLiteral("autitcgreen")},
        {tr("Auto (RGB grey)"), QStringLiteral("autold")},
        {tr("Daylight"), QStringLiteral("Daylight")}, {tr("Cloudy"), QStringLiteral("Cloudy")},
        {tr("Shade"), QStringLiteral("Shade")}, {tr("Tungsten"), QStringLiteral("Tungsten")},
        {tr("Fluorescent (F2)"), QStringLiteral("Fluo F2")},
        {tr("Flash (5500 K)"), QStringLiteral("Flash 5500K")},
        {tr("Custom"), QStringLiteral("Custom")}
    };
    for (const auto& method : methods) m_whiteBalanceCombo->addItem(method.first, method.second);
    wbRow->addWidget(m_whiteBalanceCombo, 1);
    auto* eyeDropper = new QToolButton(panel);
    eyeDropper->setText(tr("Eyedropper"));
    eyeDropper->setEnabled(false);
    eyeDropper->setToolTip(tr("White-balance sampling needs a RAW sensor-coordinate API, which the embedded engine does not expose yet."));
    wbRow->addWidget(eyeDropper);
    layout->addLayout(wbRow);

    // Reads and writes through m_params, which previewFinished() keeps topped
    // up with the temperature/green the engine actually resolved for the
    // render (adoptResolvedWhiteBalance). Under "As Shot" and the automatic
    // methods those two PP3 fields are unused placeholders, so without that
    // read-back this slider would show ProcParams' hand-wired 6504 K and the
    // first drag would jump the image from the shot's own white balance to
    // generic daylight.
    m_temperatureSlider = addParamRow(layout, tr("Temperature"), 1500, 60000, 6504, 0,
        tr("White-balance color temperature, in Kelvin."),
        [this] { return m_params.whiteBalance().temperatureKelvin; }, [this](double value) {
            auto wb = m_params.whiteBalance(); wb.method = "Custom"; wb.temperatureKelvin = qRound(value); m_params.setWhiteBalance(wb);
            QSignalBlocker blocker(m_whiteBalanceCombo);
            m_whiteBalanceCombo->setCurrentIndex(m_whiteBalanceCombo->findData(QStringLiteral("Custom")));
        }, RawPreviewUpdate::WhiteBalance);
    const auto* theme = ThemeManager::instance()->current();
    // Stop positions are fractions of the engine's own 1500..60000 K range,
    // so the neutral stop has to sit where daylight actually falls on this
    // track — (6504 - 1500) / 58500 — not at an eyeballed middle. The upper
    // half of the range is the engine's underwater/extreme-temperature tail;
    // it stays warm rather than getting its own stops.
    m_temperatureSlider->setStops({
        {0.0, theme->colorAccentPressed},
        {0.086, theme->colorTextSecondary},
        {0.22, theme->colorWarning},
        {1.0, theme->colorDanger}
    });
    // The engine's own Green/Tint adjuster spans the same 0.02..100 range on
    // a log scale because it is a multiplier, not an offset: 1.0 (neutral)
    // sits almost at the left edge of a *linear* 0.02..100 slider, which
    // would make fine adjustment near neutral unusable. Mirror that with
    // RawDevelopSlider's own logarithmic mode.
    m_tintRow = addParamRow(layout, tr("Tint"), .02, 100.0, 1.0, 3,
        tr("Engine mapping: green multiplier. There is no Adobe-style centered Tint unit."),
        [this] { return m_params.whiteBalance().green; }, [this](double value) {
            auto wb = m_params.whiteBalance(); wb.method = "Custom"; wb.green = value; m_params.setWhiteBalance(wb);
            QSignalBlocker blocker(m_whiteBalanceCombo);
            m_whiteBalanceCombo->setCurrentIndex(m_whiteBalanceCombo->findData(QStringLiteral("Custom")));
        }, RawPreviewUpdate::WhiteBalance, /*logarithmic=*/true);
    connect(m_whiteBalanceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) return;
        auto wb = m_params.whiteBalance();
        wb.method = m_whiteBalanceCombo->itemData(index).toString().toStdString();
        // A fixed-temperature preset drives the engine from its own spectral
        // data and ignores these two fields, so they have to be moved onto
        // the preset's own values — otherwise the sliders would keep
        // displaying the previous method's numbers, and the next Temperature
        // drag (which switches to "Custom") would jump the image to them.
        RawDevelopParams::WhiteBalance preset;
        if (RawDevelopParams::presetWhiteBalance(wb.method, preset)) {
            wb.temperatureKelvin = preset.temperatureKelvin;
            wb.green = preset.green;
        }
        m_params.setWhiteBalance(wb);
        syncControlsFromParams();
        applyParamsChanged(RawPreviewUpdate::WhiteBalance);
    });
    m_syncControls.push_back([this]() {
        const auto wb = m_params.whiteBalance();
        const int index = m_whiteBalanceCombo->findData(QString::fromStdString(wb.method));
        m_whiteBalanceCombo->setCurrentIndex(index >= 0 ? index : m_whiteBalanceCombo->findData(QStringLiteral("Custom")));
    });
    m_tintRow->setStops({
        {0.0, theme->colorSuccess},
        {0.5, theme->colorTextDisabled},
        {1.0, theme->colorAccentHover}
    });
    auto* vibrance = addParamRow(layout, tr("Vibrance"), -100, 100, 0, 0, tr("Vibrance (protects already-saturated colors)."),
        [this] { return m_params.color().vibrance; }, [this](double value) { auto s = m_params.color(); s.vibrance = qRound(value); m_params.setColor(s); }, RawPreviewUpdate::Color);
    vibrance->setStops({
        {0.0, theme->colorAccentPressed},
        {0.5, theme->colorTextDisabled},
        {1.0, theme->colorDanger}
    });
    auto* saturation = addParamRow(layout, tr("Saturation"), -100, 100, 0, 0, tr("Tone curve saturation."),
        [this] { return m_params.color().saturation; }, [this](double value) { auto s = m_params.color(); s.saturation = qRound(value); m_params.setColor(s); }, RawPreviewUpdate::Color);
    // Grey -> saturated, the same composition HueSaturationAdjustmentWidget
    // gives its own Saturation slider. A full hue spectrum would read as a
    // Hue control.
    saturation->setStops(ColorStops::saturation(0));
    return panel;
}

QWidget* RawDevelopWindow::buildEffectsPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 6);
    layout->setSpacing(1);
    auto addEnable = [this, layout](const QString& label, const QString& tip, bool value, const std::function<void(bool)>& setter) {
        auto* check = new AppCheckBox(label, layout->parentWidget()); check->setChecked(value); check->setToolTip(tip);
        check->setContentsMargins(8, 2, 6, 2); layout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, setter](bool on) { if (!m_syncing) { setter(on); applyParamsChanged(); } });
        return check;
    };
    auto* textureEnabled = addEnable(tr("Texture"), tr("Engine mapping: Sharpen Microcontrast. Like Sharpening it exists only in the engine's 1:1 pipeline — set the zoom to 100% to see it."), m_params.effects().textureEnabled,
        [this](bool on) { auto s = m_params.effects(); s.textureEnabled = on; m_params.setEffects(s); });
    m_syncControls.push_back([this, textureEnabled]() { textureEnabled->setChecked(m_params.effects().textureEnabled); });
    addParamRow(layout, tr("Texture amount"), 0, 100, 20, 0, tr("Sharpen Microcontrast amount."),
        [this] { return m_params.effects().texture; }, [this](double value) { auto s = m_params.effects(); s.texture = value; m_params.setEffects(s); });
    auto* clarityEnabled = addEnable(tr("Clarity"), tr("Engine mapping: Local Contrast."), m_params.effects().clarityEnabled,
        [this](bool on) { auto s = m_params.effects(); s.clarityEnabled = on; m_params.setEffects(s); });
    m_syncControls.push_back([this, clarityEnabled]() { clarityEnabled->setChecked(m_params.effects().clarityEnabled); });
    addParamRow(layout, tr("Clarity amount"), 0, 1, .2, 2, tr("Local Contrast amount."),
        [this] { return m_params.effects().clarity; }, [this](double value) { auto s = m_params.effects(); s.clarity = value; m_params.setEffects(s); });
    auto* dehazeEnabled = addEnable(tr("Dehaze"), tr("Dehaze."), m_params.effects().dehazeEnabled,
        [this](bool on) { auto s = m_params.effects(); s.dehazeEnabled = on; m_params.setEffects(s); });
    m_syncControls.push_back([this, dehazeEnabled]() { dehazeEnabled->setChecked(m_params.effects().dehazeEnabled); });
    addParamRow(layout, tr("Dehaze amount"), 0, 100, 50, 0, tr("Dehaze strength."),
        [this] { return m_params.effects().dehaze; }, [this](double value) { auto s = m_params.effects(); s.dehaze = qRound(value); m_params.setEffects(s); });
    auto* vignetteEnabled = addEnable(tr("Vignette"), tr("Post-crop Vignette."), m_params.effects().vignetteEnabled,
        [this](bool on) { auto s = m_params.effects(); s.vignetteEnabled = on; m_params.setEffects(s); });
    m_syncControls.push_back([this, vignetteEnabled]() { vignetteEnabled->setChecked(m_params.effects().vignetteEnabled); });
    addParamRow(layout, tr("Amount"), -6, 6, 0, 2, tr("Post-crop Vignette strength, in EV."),
        [this] { return m_params.effects().vignetteAmount; }, [this](double value) { auto s = m_params.effects(); s.vignetteAmount = value; m_params.setEffects(s); });
    addParamRow(layout, tr("Feather"), 0, 100, 50, 0, tr("Post-crop Vignette feather."),
        [this] { return m_params.effects().vignetteFeather; }, [this](double value) { auto s = m_params.effects(); s.vignetteFeather = qRound(value); m_params.setEffects(s); });
    addParamRow(layout, tr("Roundness"), 0, 100, 50, 0, tr("Post-crop Vignette roundness."),
        [this] { return m_params.effects().vignetteRoundness; }, [this](double value) { auto s = m_params.effects(); s.vignetteRoundness = qRound(value); m_params.setEffects(s); });
    auto* grain = new QLabel(tr("Vignette Midpoint is unavailable: the engine's Post-Crop Vignette is shaped by Feather and Roundness only. Grain is unavailable too — the embedded engine has no equivalent non-destructive grain operation."), panel);
    grain->setWordWrap(true); grain->setContentsMargins(8, 5, 8, 2); grain->setStyleSheet(QStringLiteral("color: %1;").arg(ThemeManager::instance()->current()->colorTextSecondary.name()));
    layout->addWidget(grain);
    return panel;
}

QWidget* RawDevelopWindow::buildCurvePanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 5, 8, 8);
    auto* note = new QLabel(tr("Point curves map to the engine's diagonal curves. There is no Camera Raw-style parametric-region curve."), panel);
    note->setWordWrap(true); layout->addWidget(note);
    m_curveEditor = new CurvesEditorWidget(panel);
    m_curveEditor->setStandaloneMode(true);
    m_curveEditor->setCurveData(curveDataFromParams());
    connect(m_curveEditor, &CurvesEditorWidget::curveDataChanged, this, [this]() {
        if (m_syncing)
            return;
        setCurvesFromEditor();
        applyParamsChanged();
    });
    layout->addWidget(m_curveEditor);
    return panel;
}

QWidget* RawDevelopWindow::buildColorMixerPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 6);
    auto* enable = new AppCheckBox(tr("Enable HSV Color Mixer"), panel);
    enable->setContentsMargins(8, 2, 6, 2);
    layout->addWidget(enable);
    connect(enable, &QCheckBox::toggled, this, [this](bool on) { if (!m_syncing) { auto mixer = m_params.colorMixer(); mixer.enabled = on; m_params.setColorMixer(mixer); applyParamsChanged(); } });
    m_syncControls.push_back([this, enable]() { enable->setChecked(m_params.colorMixer().enabled); });
    auto* tabs = new QTabWidget(panel);
    const std::array<QString, 3> tabNames{tr("Hue"), tr("Saturation"), tr("Luminance")};
    const std::array<QString, 8> bands{tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"), tr("Aqua"), tr("Blue"), tr("Purple"), tr("Magenta")};
    const std::array<QColor, 8> bandColors{QColor("#e05252"), QColor("#e88a36"), QColor("#d4c845"), QColor("#58b66a"), QColor("#4fb8b8"), QColor("#4b83d1"), QColor("#9b69c8"), QColor("#d05eaa")};
    for (int channel = 0; channel < 3; ++channel) {
        auto* page = new QWidget(tabs);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 4, 0, 4);
        for (int band = 0; band < 8; ++band) {
            auto* row = addParamRow(pageLayout, bands[band], -100, 100, 0, 0,
                tr("HSV Equalizer %1 band.").arg(tabNames[channel]),
                [this, channel, band] { const auto m = m_params.colorMixer(); return channel == 0 ? m.hue[band] : channel == 1 ? m.saturation[band] : m.luminance[band]; },
                [this, enable, channel, band](double value) {
                    auto m = m_params.colorMixer();
                    if (channel == 0) m.hue[band] = value;
                    else if (channel == 1) m.saturation[band] = value;
                    else m.luminance[band] = value;
                    m.enabled = true; m_params.setColorMixer(m);
                    const QSignalBlocker blocker(enable);
                    enable->setChecked(true);
                });
            row->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(bandColors[band].name()));
            m_mixerRows[channel][band] = row;
        }
        tabs->addTab(page, tabNames[channel]);
    }
    layout->addWidget(tabs);
    return panel;
}

QWidget* RawDevelopWindow::buildColorGradingPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 5, 8, 8);
    auto* enable = new AppCheckBox(tr("Enable Color Grading"), panel);
    layout->addWidget(enable);
    connect(enable, &QCheckBox::toggled, this, [this](bool on) { if (!m_syncing) { auto g = m_params.colorGrading(); g.enabled = on; m_params.setColorGrading(g); applyParamsChanged(); } });
    m_syncControls.push_back([this, enable]() { enable->setChecked(m_params.colorGrading().enabled); });
    auto* wheels = new QHBoxLayout;
    const std::array<QString, 3> labels{tr("Shadows"), tr("Midtones"), tr("Highlights")};
    for (int i = 0; i < 3; ++i) {
        auto* column = new QVBoxLayout;
        auto* wheel = new RawColorWheel(panel);
        column->addWidget(wheel, 0, Qt::AlignCenter);
        auto* label = new QLabel(labels[i], panel); label->setAlignment(Qt::AlignCenter); column->addWidget(label);
        wheels->addLayout(column);
        wheel->changed = [this, enable, wheel, i]() {
            if (m_syncing) return;
            auto grading = m_params.colorGrading();
            if (i == 0) grading.shadows = wheel->values();
            else if (i == 1) grading.midtones = wheel->values();
            else grading.highlights = wheel->values();
            grading.enabled = true;
            m_params.setColorGrading(grading);
            const QSignalBlocker blocker(enable);
            enable->setChecked(true);
            applyParamsChanged();
        };
        m_syncControls.push_back([this, wheel, i]() {
            const auto g = m_params.colorGrading();
            wheel->setValues(i == 0 ? g.shadows : i == 1 ? g.midtones : g.highlights);
        });
    }
    layout->addLayout(wheels);
    auto* gradingNote = new QLabel(tr("Engine mapping: Color Toning in Split Color mode. Balance has no effect there — it belongs to the engine's two-colour toning modes — so it is not offered. Enabling this panel replaces any other Color Toning mode a loaded profile carried."), panel);
    gradingNote->setWordWrap(true);
    gradingNote->setStyleSheet(QStringLiteral("color: %1;").arg(ThemeManager::instance()->current()->colorTextSecondary.name()));
    layout->addWidget(gradingNote);
    addParamRow(layout, tr("Strength"), 0, 100, 50, 0, tr("Color Toning strength."),
        [this] { return m_params.colorGrading().strength; }, [this, enable](double value) {
            auto grading = m_params.colorGrading();
            grading.strength = qRound(value);
            grading.enabled = true;
            m_params.setColorGrading(grading);
            const QSignalBlocker blocker(enable);
            enable->setChecked(true);
        });
    return panel;
}

QWidget* RawDevelopWindow::buildDetailPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 6);
    auto* sharpen = new AppCheckBox(tr("Sharpening"), panel); sharpen->setContentsMargins(8, 2, 6, 2); layout->addWidget(sharpen);
    connect(sharpen, &QCheckBox::toggled, this, [this](bool on) { if (!m_syncing) { auto d = m_params.detail(); d.sharpeningEnabled = on; m_params.setDetail(d); applyParamsChanged(); } });
    m_syncControls.push_back([this, sharpen]() { sharpen->setChecked(m_params.detail().sharpeningEnabled); });
    addParamRow(layout, tr("Amount"), 1, 1000, 200, 0, tr("Unsharp Mask amount."),
        [this] { return m_params.detail().sharpeningAmount; }, [this](double value) { auto d = m_params.detail(); d.sharpeningAmount = qRound(value); m_params.setDetail(d); });
    addParamRow(layout, tr("Radius"), .3, 3, .5, 2, tr("Unsharp Mask radius."),
        [this] { return m_params.detail().sharpeningRadius; }, [this](double value) { auto d = m_params.detail(); d.sharpeningRadius = value; m_params.setDetail(d); });
    addParamRow(layout, tr("Detail"), 0, 200, 20, 0, tr("Engine mapping: sharpening contrast; it is not Adobe Detail."),
        [this] { return m_params.detail().sharpeningDetail; }, [this](double value) { auto d = m_params.detail(); d.sharpeningDetail = value; m_params.setDetail(d); });
    auto* limitations = new QLabel(tr("Sharpening and Noise Reduction exist only in the engine's 1:1 pipeline. Set the zoom to 100% and the visible region is re-rendered with them; the fit view cannot show them at all. Masking is unavailable: the engine's edge-only control is not equivalent to Camera Raw Masking. Its luminance denoiser has no compatible Luminance Contrast, Color Detail, or Smoothness controls."), panel);
    limitations->setWordWrap(true); limitations->setContentsMargins(8, 3, 8, 3); layout->addWidget(limitations);
    auto* denoise = new AppCheckBox(tr("Noise Reduction"), panel); denoise->setContentsMargins(8, 5, 6, 2); layout->addWidget(denoise);
    connect(denoise, &QCheckBox::toggled, this, [this](bool on) { if (!m_syncing) { auto d = m_params.detail(); d.denoiseEnabled = on; m_params.setDetail(d); applyParamsChanged(); } });
    m_syncControls.push_back([this, denoise]() { denoise->setChecked(m_params.detail().denoiseEnabled); });
    addParamRow(layout, tr("Luminance"), 0, 100, 0, 2, tr("Directional Pyramid luminance smoothing."),
        [this] { return m_params.detail().luminanceNoise; }, [this](double value) { auto d = m_params.detail(); d.luminanceNoise = value; m_params.setDetail(d); });
    addParamRow(layout, tr("Luminance Detail"), 0, 100, 0, 2, tr("Directional Pyramid luminance detail."),
        [this] { return m_params.detail().luminanceDetail; }, [this](double value) { auto d = m_params.detail(); d.luminanceDetail = value; m_params.setDetail(d); });
    addParamRow(layout, tr("Color"), 0, 100, 15, 2, tr("Directional Pyramid chrominance master."),
        [this] { return m_params.detail().colorNoise; }, [this](double value) { auto d = m_params.detail(); d.colorNoise = value; m_params.setDetail(d); });
    return panel;
}

QWidget* RawDevelopWindow::buildOpticsPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 6);
    auto addCheck = [this, layout](const QString& text, const QString& tip, const std::function<bool()>& current, const std::function<void(bool)>& set) {
        auto* check = new AppCheckBox(text, layout->parentWidget()); check->setContentsMargins(8, 2, 6, 2); check->setToolTip(tip); layout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, set](bool on) { if (!m_syncing) { set(on); applyParamsChanged(); } });
        m_syncControls.push_back([check, current]() { check->setChecked(current()); });
    };
    addCheck(tr("Automatic Lens Profile Correction"), tr("Lensfun automatic camera/lens match."),
        [this] { return m_params.optics().automaticLensProfile; }, [this](bool on) { auto o = m_params.optics(); o.automaticLensProfile = on; m_params.setOptics(o); });
    addCheck(tr("Correct distortion"), tr("Enable geometric correction from the selected Lensfun profile."),
        [this] { return m_params.optics().correctDistortion; }, [this](bool on) { auto o = m_params.optics(); o.correctDistortion = on; m_params.setOptics(o); });
    addCheck(tr("Correct lens vignetting"), tr("Enable vignetting correction from the selected Lensfun profile."),
        [this] { return m_params.optics().correctVignetting; }, [this](bool on) { auto o = m_params.optics(); o.correctVignetting = on; m_params.setOptics(o); });
    addCheck(tr("Correct chromatic aberration"), tr("Enable chromatic-aberration correction from the selected Lensfun profile."),
        [this] { return m_params.optics().correctChromaticAberration; }, [this](bool on) { auto o = m_params.optics(); o.correctChromaticAberration = on; m_params.setOptics(o); });
    addParamRow(layout, tr("Distortion"), -.5, .5, 0, 3, tr("Manual distortion amount."),
        [this] { return m_params.optics().distortion; }, [this](double value) { auto o = m_params.optics(); o.distortion = value; m_params.setOptics(o); });
    addParamRow(layout, tr("Lens Vignetting"), -100, 100, 0, 0, tr("Manual vignetting amount."),
        [this] { return m_params.optics().lensVignetting; }, [this](double value) { auto o = m_params.optics(); o.lensVignetting = qRound(value); m_params.setOptics(o); });
    // The engine's own CA Correction adjuster range is -0.005..0.005 (a
    // sub-pixel fringe-shift fraction, not an EV-like unit); -4..4 would be
    // 800x too wide and leave only a few unusable pixels of slider travel
    // anywhere near a real correction value.
    addParamRow(layout, tr("CA Red"), -0.005, 0.005, 0, 4, tr("Manual red chromatic-aberration correction."),
        [this] { return m_params.optics().redChromaticAberration; }, [this](double value) { auto o = m_params.optics(); o.redChromaticAberration = value; m_params.setOptics(o); });
    addParamRow(layout, tr("CA Blue"), -0.005, 0.005, 0, 4, tr("Manual blue chromatic-aberration correction."),
        [this] { return m_params.optics().blueChromaticAberration; }, [this](double value) { auto o = m_params.optics(); o.blueChromaticAberration = value; m_params.setOptics(o); });
    auto* defringe = new AppCheckBox(tr("Defringe"), panel); defringe->setContentsMargins(8, 4, 6, 2); layout->addWidget(defringe);
    connect(defringe, &QCheckBox::toggled, this, [this](bool on) { if (!m_syncing) { auto o = m_params.optics(); o.defringeEnabled = on; m_params.setOptics(o); applyParamsChanged(); } });
    m_syncControls.push_back([this, defringe]() { defringe->setChecked(m_params.optics().defringeEnabled); });
    addParamRow(layout, tr("Defringe Radius"), .5, 5, 2, 1, tr("Defringe radius."),
        [this] { return m_params.optics().defringeRadius; }, [this](double value) { auto o = m_params.optics(); o.defringeRadius = value; m_params.setOptics(o); });
    addParamRow(layout, tr("Defringe Threshold"), 0, 100, 13, 0, tr("Defringe threshold."),
        [this] { return m_params.optics().defringeThreshold; }, [this](double value) { auto o = m_params.optics(); o.defringeThreshold = qRound(value); m_params.setOptics(o); });
    auto* defringeNote = new QLabel(tr("Defringe is a 1:1-only stage like Sharpening: set the zoom to 100% to see it on the visible region. Everything above it is rendered in the fit preview."), panel);
    defringeNote->setWordWrap(true); defringeNote->setContentsMargins(8, 4, 8, 2);
    defringeNote->setStyleSheet(QStringLiteral("color: %1;").arg(ThemeManager::instance()->current()->colorTextSecondary.name()));
    layout->addWidget(defringeNote);
    return panel;
}

void RawDevelopWindow::syncControlsFromParams()
{
    m_syncing = true;
    for (const auto& sync : m_syncControls) sync();
    if (m_blackWhiteCheck) m_blackWhiteCheck->setChecked(m_params.color().blackAndWhite);
    if (m_curveEditor)
        m_curveEditor->setCurveData(curveDataFromParams());
    if (m_profileCombo) {
        const int mode = m_params.inputProfileMode() == RawDevelopParams::InputProfileMode::Custom
            ? -1 : static_cast<int>(m_params.inputProfileMode());
        const int index = m_profileCombo->findData(mode);
        if (index >= 0) m_profileCombo->setCurrentIndex(index);
    }
    m_syncing = false;
    updateProfileSummary();
}

void RawDevelopWindow::applyParamsChanged(RawPreviewUpdate update)
{
    ++m_previewGeneration;
    m_pendingPreviewUpdates |= update;
    m_showBefore = false;
    if (m_beforeAfterButton) {
        QSignalBlocker blocker(m_beforeAfterButton);
        m_beforeAfterButton->setChecked(false);
    }
    requestPreview();
}

bool RawDevelopWindow::detailStagesActive() const
{
    const auto detail = m_params.detail();
    return detail.sharpeningEnabled || detail.denoiseEnabled
        || m_params.effects().textureEnabled || m_params.optics().defringeEnabled;
}

QRect RawDevelopWindow::detailRegionRequest() const
{
    // Below 1:1 a slice would have to be shrunk to be shown, which is both
    // ruinous — it is a full-resolution render of everything on screen — and
    // misleading about what the stages actually do. And with none of the
    // four enabled the slice would hold nothing the fast preview doesn't
    // already show, so it would not be worth the full-quality demosaic it
    // forces on the whole preview (see RawDetailRegion).
    // Keyed off the zoom actually on screen, not the explicit factor: a small
    // frame can reach 1:1 (or past it) while the view is still in Fit, and
    // the stages are just as renderable there.
    const double zoom = effectiveZoom();
    if (zoom < 1.0 || !detailStagesActive()) {
        return {};
    }
    if (!m_preview || m_previewFullSize.isEmpty()) {
        return {};
    }

    // The view carries the frame at that zoom, so both its scroll offset and
    // its size divide by it to land in the engine's 1:1 space. When the
    // zoomed frame is smaller than the viewport the offset is 0 and this
    // resolves to the whole (small) frame, which is correct.
    const QSize viewport = m_preview->size();
    const QPoint offset = m_preview->scrollOffset();
    const QRect region(QPoint(qFloor(offset.x() / zoom), qFloor(offset.y() / zoom)),
                       QSize(qCeil(viewport.width() / zoom), qCeil(viewport.height() / zoom)));
    return region.intersected(QRect(QPoint(0, 0), m_previewFullSize));
}

QSize RawDevelopWindow::frameSize() const
{
    // Zoom percentages are relative to the document's actual resolution (the
    // layer's current develop), not to the viewport-budget-capped preview
    // buffer: sizing off the latter would make "100%" show a fraction of
    // true size on a typical multi-megapixel RAW.
    if (!m_beforeImage.isNull())
        return m_beforeImage.size();
    if (!m_previewFullSize.isEmpty())
        return m_previewFullSize;
    return m_previewImage.size();
}

double RawDevelopWindow::effectiveZoom() const
{
    if (!m_fitToWindow)
        return m_zoom;
    const QSize frame = frameSize();
    if (!m_preview || frame.isEmpty())
        return m_zoom;
    const QSize available = (m_preview->size() - QSize(2 * kFitMargin, 2 * kFitMargin))
        .expandedTo(QSize(1, 1));
    const QSize fitted = frame.scaled(available, Qt::KeepAspectRatio);
    // No lower clamp: a frame too large to fit even at the minimum zoom must
    // still report what it is actually showing, or the composed tile and the
    // viewport would disagree about where the frame is.
    return std::min(double(fitted.width()) / frame.width(), kMaxZoom);
}

void RawDevelopWindow::setZoom(double zoom)
{
    m_fitToWindow = false;
    m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    updateZoomControls();
    updateDisplayedImage();
    maybeRequestDetail();
    refreshPreviewStatus();
}

void RawDevelopWindow::setFitToWindow()
{
    m_fitToWindow = true;
    updateZoomControls();
    updateDisplayedImage();
    maybeRequestDetail();
    refreshPreviewStatus();
}

void RawDevelopWindow::zoomStep(int direction)
{
    const double current = effectiveZoom();
    if (direction > 0) {
        for (const double stop : kZoomStops) {
            if (stop > current + 0.001) {
                setZoom(stop);
                return;
            }
        }
        setZoom(kMaxZoom);
        return;
    }
    for (auto it = std::rbegin(kZoomStops); it != std::rend(kZoomStops); ++it) {
        if (*it < current - 0.001) {
            setZoom(*it);
            return;
        }
    }
    setZoom(kMinZoom);
}

void RawDevelopWindow::updateZoomControls()
{
    if (!m_zoomBox)
        return;
    // The readout is the zoom actually on screen, Fit included — the two
    // used to disagree, with Fit reported as its own "zoom level".
    m_zoomBox->setZoom(static_cast<float>(effectiveZoom()));
    const QSignalBlocker fitBlocker(m_fitButton);
    const QSignalBlocker hundredBlocker(m_hundredButton);
    m_fitButton->setChecked(m_fitToWindow);
    m_hundredButton->setChecked(!m_fitToWindow && std::abs(m_zoom - 1.0) < 0.001);
}

rawpreview::State RawDevelopWindow::previewState() const
{
    rawpreview::State state;
    state.engineAvailable = RawEngine::isAvailable() && m_rawData.source != nullptr;
    state.processing = (m_previewWatcher && m_previewWatcher->isRunning())
        || m_previewPending
        || (m_previewDebounce && m_previewDebounce->isActive());
    state.applying = m_applyInProgress;
    state.showingBefore = m_showBefore && !m_splitView;
    state.zoom = effectiveZoom();
    state.detailSliceVisible = !m_detailImage.isNull() && !m_detailRect.isEmpty();
    state.activeStages = rawpreview::activeStages(m_params);
    state.errorMessage = m_previewError;
    return state;
}

void RawDevelopWindow::refreshPreviewStatus()
{
    if (!m_statusLabel || !m_preview)
        return;

    const rawpreview::State state = previewState();
    m_statusLabel->setText(rawpreview::statusText(state));
    m_previewProgress->setVisible(rawpreview::status(state) == rawpreview::Status::Processing);

    const rawpreview::Notice alert = rawpreview::notice(state);
    const bool visible = !alert.isEmpty();
    m_noticeIcon->setVisible(visible);
    m_noticeLabel->setVisible(visible);
    m_noticeAction->setVisible(visible && alert.offerFullZoom);
    if (visible) {
        m_noticeIcon->setPixmap(noticeBadge(alert.severity));
        m_noticeLabel->setText(alert.message);
        m_noticeLabel->setToolTip(alert.message);
        // Mirrored over the image for as long as the status bar shows it;
        // re-setting on every unchanged call would just repaint for nothing.
        if (alert.message != m_lastNotice)
            m_preview->showNotice(alert.message, noticeAccent(alert.severity));
    } else {
        m_preview->clearNotice();
    }
    m_lastNotice = visible ? alert.message : QString();
}

void RawDevelopWindow::maybeRequestDetail()
{
    const QRect wanted = detailRegionRequest();
    // The generation counter is what previewFinished() uses to decide a
    // render is still wanted, so it has to move even though no parameter
    // did — otherwise the render this schedules would be thrown away.
    if (wanted.isEmpty()) {
        if (m_detailImage.isNull() && m_detailRect.isEmpty()) {
            return;
        }
        // Dropping the slice is also what unregisters the region on the
        // engine side, which is what puts the preview back on the cheap
        // demosaic — so it needs a render, not just a repaint.
        m_detailImage = QImage();
        m_detailRect = QRect();
        m_detailRequested = QRect();
        ++m_previewGeneration;
        updateDisplayedImage();
        requestPreview();
        return;
    }
    if (wanted == m_detailRequested && !m_detailImage.isNull()) {
        return;
    }
    ++m_previewGeneration;
    requestPreview();
}

void RawDevelopWindow::requestPreview()
{
    if (!m_previewDebounce) return;
    m_previewDebounce->start();
    refreshPreviewStatus();
}

void RawDevelopWindow::startPreview()
{
    // startPreview() can also be scheduled directly when an obsolete render
    // finishes before the debounce expires. Cancel that timer so it cannot
    // enqueue a redundant third render for the same generation.
    if (m_previewDebounce)
        m_previewDebounce->stop();

    if (!RawEngine::isAvailable() || !m_rawData.source) {
        m_previewPending = false;
        refreshPreviewStatus();
        return;
    }
    if (m_previewWatcher->isRunning()) {
        m_previewPending = true;
        return;
    }
    if (m_previewSourcePath.isEmpty() && m_rawData.mode == RawStorageMode::Linked)
        m_previewSourcePath = m_rawData.source->path;
    if (m_previewSourcePath.isEmpty() && m_rawData.mode != RawStorageMode::Embedded) {
        m_previewPending = false;
        m_previewError = tr("Could not prepare the RAW source for preview.");
        refreshPreviewStatus();
        return;
    }
    const RawDevelopParams params = m_params;
    const QString existingSourcePath = m_previewSourcePath;
    // Embedded sources materialize (write tens of MB to a temp file) lazily,
    // the first time, inside the worker below — never on the UI thread.
    const std::shared_ptr<const RawSource> source = m_rawData.source;
    const quint64 generation = m_previewGeneration;
    const RawPreviewUpdate previewUpdates = m_pendingPreviewUpdates;
    const QSize viewportSize = m_preview ? m_preview->size() : QSize();
    const qreal pixelRatio = m_preview ? m_preview->devicePixelRatioF() : 1.0;
    const int viewportEdge = qCeil(std::max(viewportSize.width(), viewportSize.height()) * pixelRatio);
    const int previewMaxEdge = std::clamp(viewportEdge, kPreviewMinEdge, kPreviewMaxEdge);
    const QRect detailRequest = detailRegionRequest();
    m_previewPending = false;
    m_pendingPreviewUpdates = RawPreviewUpdate::None;
    m_previewWatcher->setFuture(QtConcurrent::run(m_previewPool,
        [this, params, existingSourcePath, source, generation, previewMaxEdge, previewUpdates,
         detailRequest]() {
        RawEngine::applyBackgroundThreadPolicy();
        PreviewResult result;
        result.generation = generation;
        QString sourcePath = existingSourcePath;
        if (sourcePath.isEmpty()) {
            sourcePath = RawDevelopService::materializeEmbedded(*source);
            result.ownsSourcePath = !sourcePath.isEmpty();
        }
        if (sourcePath.isEmpty()) {
            result.errorMessage = QObject::tr("Could not prepare the RAW source for preview.");
            return result;
        }
        result.sourcePath = sourcePath;
        if (!m_previewSession) {
            std::string error;
            m_previewSession = RawEngine::openFile(sourcePath.toStdString(), &error);
            if (!m_previewSession) {
                result.errorMessage = QString::fromStdString(error);
                return result;
            }
        }
        // Always ask for the resolved parameters: getParams() is a struct
        // copy, and the engine writes back values the UI has no other way to
        // learn — the white balance it actually used for every method, plus
        // an auto-exposure or Auto-Matched Curve result when one is active.
        result.hasResolvedToneCurve = params.needsResolvedToneCurve();
        result.hasResolvedParams = true;
        RawDetailRegion detailRegion{detailRequest.x(), detailRequest.y(),
                                     detailRequest.width(), detailRequest.height()};
        RawImageBuffer detailBuffer;
        const RawImageBuffer buffer = m_previewSession->renderPreview(
            previewMaxEdge, params, previewUpdates, nullptr, &result.resolvedParams,
            &detailRegion, &detailBuffer);
        if (buffer.isEmpty()) {
            result.errorMessage = QObject::tr("RAW preview render failed.");
            return result;
        }
        result.image = RawDevelopService::convertToRgba8888(buffer, false);
        result.fullSize = QSize(buffer.geometry.fullWidth, buffer.geometry.fullHeight);
        // renderPreview() reports back the rectangle it could actually
        // deliver, which is narrower than the request wherever the region
        // ran into the frame edge.
        if (!detailBuffer.isEmpty()) {
            result.detailImage = RawDevelopService::convertToRgba8888(detailBuffer, false);
            result.detailRect = QRect(detailRegion.x, detailRegion.y,
                                      detailRegion.width, detailRegion.height);
            result.detailRequested = detailRequest;
        }
        return result;
    }));
    // Only now does m_previewWatcher->isRunning() actually report the render
    // that just started; refreshing before setFuture() reads the watcher's
    // stale (not-running) state and hides the progress bar for the render's
    // whole duration instead of showing it.
    refreshPreviewStatus();
}

void RawDevelopWindow::previewFinished()
{
    const PreviewResult result = m_previewWatcher->result();
    if (m_previewSourcePath.isEmpty() && !result.sourcePath.isEmpty()) {
        m_previewSourcePath = result.sourcePath;
        m_ownsPreviewSourcePath = result.ownsSourcePath;
    }
    if (result.generation == m_previewGeneration && !result.image.isNull()) {
        if (result.hasResolvedParams) {
            // Both adopters report whether anything actually moved, so the
            // controls are only re-synced until the engine's values settle —
            // re-syncing on every preview would reset the curve editor out
            // from under an in-progress edit.
            bool changed = m_params.adoptResolvedWhiteBalance(result.resolvedParams);
            if (result.hasResolvedToneCurve)
                changed = m_params.adoptResolvedToneCurve(result.resolvedParams) || changed;
            if (changed)
                syncControlsFromParams();
        }
        if (!result.fullSize.isEmpty()) {
            m_previewFullSize = result.fullSize;
        }
        m_detailImage = result.detailImage;
        m_detailRect = result.detailRect;
        m_detailRequested = result.detailRequested;
        m_previewError.clear();
        updatePreviewImage(result.image);
    } else if (result.generation == m_previewGeneration && !result.errorMessage.isEmpty()) {
        m_previewError = result.errorMessage;
    }
    if (m_previewPending || result.generation != m_previewGeneration) {
        // Left set, not cleared, so the status line keeps reporting a render
        // in flight across the hand-off; startPreview() clears it when it
        // actually launches the next one.
        m_previewPending = true;
        QTimer::singleShot(0, this, &RawDevelopWindow::startPreview);
    }
    // After the pending-render bookkeeping, so "Processing" stays up while a
    // superseded result is being replaced rather than blinking to Ready.
    refreshPreviewStatus();
}

void RawDevelopWindow::updatePreviewImage(const QImage& image)
{
    if (image.isNull()) return;
    m_previewImage = image;
    updateDisplayedImage();
    updateHistogram(m_showBefore ? m_beforeImage : m_previewImage);
}

void RawDevelopWindow::updateDisplayedImage()
{
    if (!m_preview) return;
    const QImage after = m_previewImage;
    const QImage before = m_beforeImage;
    const bool showBeforeOnly = m_showBefore && !m_splitView;
    const QImage primary = showBeforeOnly ? before : after;
    if (primary.isNull()) return;

    const QSize frame = frameSize();
    if (frame.isEmpty()) return;
    const QSize content = (QSizeF(frame) * effectiveZoom()).toSize().expandedTo(QSize(1, 1));
    // The view re-anchors its pan around the viewport centre when the frame
    // size changes, so it has to learn the new size before it is asked where
    // the viewport now sits.
    m_preview->setContentSize(content);

    // Only the visible part of the frame is composed, plus a margin so a pan
    // drag does not immediately expose unpainted pasteboard. Scaling the whole
    // frame instead would allocate gigabytes at the top of the zoom range.
    QRect tileRect(m_preview->scrollOffset(), m_preview->size());
    tileRect.adjust(-m_preview->width() / 4, -m_preview->height() / 4,
                    m_preview->width() / 4, m_preview->height() / 4);
    tileRect = tileRect.intersected(QRect(QPoint(0, 0), content));
    if (tileRect.isEmpty()) return;

    // Wherever the engine produced a 1:1 slice it replaces the upscaled fast
    // preview underneath: same frame, but carrying the four stages the scaled
    // pipeline drops. The before frame never carries one — it is the layer as
    // it already stands.
    const bool drawSlice = !m_detailImage.isNull() && !m_detailRect.isEmpty()
        && !m_previewFullSize.isEmpty();

    // Both halves of a split are composed against the same content rectangle,
    // so they stay pixel-aligned even though their sources differ in size.
    auto composeTile = [&](const QImage& source, bool withSlice) -> QPixmap {
        if (source.isNull())
            return {};
        const double fx = source.width() / double(content.width());
        const double fy = source.height() / double(content.height());
        // Aligned outwards so no wanted pixel is dropped, then clamped: at
        // the frame edge the crop covers slightly less than the tile, which
        // is why the destination below is derived from the *clamped* rect
        // rather than assumed to be the whole tile.
        const QRect sourceRect = QRectF(tileRect.x() * fx, tileRect.y() * fy,
                                        tileRect.width() * fx, tileRect.height() * fy)
            .toAlignedRect().intersected(QRect(QPoint(0, 0), source.size()));
        if (sourceRect.isEmpty())
            return {};
        QImage crop = source.copy(sourceRect);
        markClipping(crop, m_clipShadows, m_clipHighlights);

        const QRect destRect(qRound(sourceRect.x() / fx) - tileRect.x(),
                             qRound(sourceRect.y() / fy) - tileRect.y(),
                             qRound(sourceRect.width() / fx),
                             qRound(sourceRect.height() / fy));
        // The slice covers the whole viewport, so the only upscaled pixels
        // that ever reach the screen are the ones a pan briefly uncovers
        // before the next slice lands; that case gets the cheap filter.
        QImage scaled = crop.scaled(destRect.size().expandedTo(QSize(1, 1)),
                                    Qt::IgnoreAspectRatio,
                                    withSlice ? Qt::FastTransformation : Qt::SmoothTransformation);
        if (!withSlice && destRect == QRect(QPoint(0, 0), tileRect.size()))
            return QPixmap::fromImage(scaled);

        QImage tile(tileRect.size(), QImage::Format_ARGB32_Premultiplied);
        tile.fill(Qt::transparent);
        QPainter painter(&tile);
        painter.drawImage(destRect.topLeft(), scaled);
        if (withSlice) {
            QImage slice = m_detailImage;
            markClipping(slice, m_clipShadows, m_clipHighlights);
            // Placed against the engine's own frame size rather than the
            // zoom, so the slice stays put if the two ever disagree.
            const double sx = content.width() / double(m_previewFullSize.width());
            const double sy = content.height() / double(m_previewFullSize.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(QRectF(m_detailRect.x() * sx - tileRect.x(),
                                     m_detailRect.y() * sy - tileRect.y(),
                                     m_detailRect.width() * sx, m_detailRect.height() * sy),
                              slice);
        }
        painter.end();
        return QPixmap::fromImage(tile);
    };

    const QPixmap tile = composeTile(primary, drawSlice && !showBeforeOnly);
    // The comparison half is only composed while the split is on, and only
    // when the view moved: the before frame itself never changes, so a
    // slider drag must not pay for a second full-resolution scale per render.
    if (m_splitView) {
        const bool cached = !m_splitTile.isNull() && m_splitTileContent == content
            && m_splitTileRect == tileRect
            && m_splitTileClipShadows == m_clipShadows
            && m_splitTileClipHighlights == m_clipHighlights;
        if (!cached) {
            m_splitTile = composeTile(before, false);
            m_splitTileContent = content;
            m_splitTileRect = tileRect;
            m_splitTileClipShadows = m_clipShadows;
            m_splitTileClipHighlights = m_clipHighlights;
        }
    } else if (!m_splitTile.isNull()) {
        m_splitTile = QPixmap();
    }
    m_tileRect = tileRect;
    m_tileContent = content;
    m_preview->setTile(tileRect.topLeft(), tile, m_splitTile);
}

void RawDevelopWindow::updateHistogram(const QImage& image)
{
    if (image.isNull() || !m_histogram || !m_histogramCache)
        return;

    HistogramGenerator::Options options;
    options.source = HistogramSource::EntireImage;
    options.autoDownsample = true;
    options.maxSampledPixels = 500'000;

    HistogramKey key;
    key.generation = ++m_histogramGeneration;
    key.source = HistogramSource::EntireImage;

    // HistogramCacheManager owns the worker and keeps the last bins visible
    // while this detached image is processed. The monotonically increasing
    // key prevents a slower, superseded preview from replacing newer data.
    m_histogramCache->request(image, QImage(), key, options);
}

void RawDevelopWindow::reloadPresets()
{
    if (!m_presetCombo) return;
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItem(tr("Camera Default"), QStringLiteral("default"));
    // Recursive on purpose: most of the bundled profiles live in
    // subdirectories of rtdata/profiles (Pop/, Pixel Shift/, Non-raw/), so a
    // flat listing would hide the majority of them. The relative path doubles
    // as the display name, which keeps "Pop 1" and a same-named user file
    // distinguishable.
    const QDir bundled(QDir(AppPaths::rawEngineResourceDir()).filePath(QStringLiteral("profiles")));
    QStringList bundledPaths;
    QDirIterator it(bundled.absolutePath(), {QStringLiteral("*.pp3"), QStringLiteral("*.PP3")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        bundledPaths << it.next();
    bundledPaths.sort(Qt::CaseInsensitive);
    for (const QString& path : bundledPaths) {
        QString name = bundled.relativeFilePath(path);
        name.chop(4); // ".pp3"
        m_presetCombo->addItem(name.replace(QLatin1Char('/'), QStringLiteral(" / ")), path);
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("rawDevelop/presets"));
    for (const QString& name : settings.childKeys())
        m_presetCombo->addItem(name, QStringLiteral("user:") + name);
    settings.endGroup();
}

void RawDevelopWindow::applyPreset(int index)
{
    if (index < 0) return;
    const QString key = m_presetCombo->itemData(index).toString();
    if (key == QStringLiteral("default")) {
        m_params = RawDevelopParams::cameraDefaults();
    } else if (key.startsWith(QStringLiteral("user:"))) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("rawDevelop/presets"));
        m_params = RawDevelopParams::fromPP3String(settings.value(key.mid(5)).toString().toStdString());
        settings.endGroup();
    } else {
        RawDevelopParams loaded;
        if (!RawDevelopParams::loadPP3File(key.toStdString(), loaded)) {
            m_previewError = tr("Could not load preset %1.").arg(QFileInfo(key).fileName());
            refreshPreviewStatus();
            return;
        }
        m_params = std::move(loaded);
    }
    syncControlsFromParams();
    applyParamsChanged();
}

void RawDevelopWindow::savePreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Develop Preset"), tr("Preset name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("rawDevelop/presets"));
    settings.setValue(name, QString::fromStdString(m_params.toPP3String()));
    settings.endGroup();
    reloadPresets();
    m_presetCombo->setCurrentIndex(m_presetCombo->findData(QStringLiteral("user:") + name));
}

void RawDevelopWindow::loadCustomProfile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Camera Profile"), QString(),
        tr("Camera profiles (*.dcp *.DCP *.icc *.ICC *.icm *.ICM)"));
    if (path.isEmpty()) {
        // The combo may already show "Custom profile…" (it is what triggered
        // this call) even though the mode never actually changed — put it
        // back on whatever profile is still active.
        QSignalBlocker blocker(m_profileCombo);
        const int mode = m_params.inputProfileMode() == RawDevelopParams::InputProfileMode::Custom
            ? -1 : static_cast<int>(m_params.inputProfileMode());
        const int index = m_profileCombo->findData(mode);
        if (index >= 0) m_profileCombo->setCurrentIndex(index);
        return;
    }
    m_params.setCustomInputProfile(path.toStdString());
    {
        QSignalBlocker blocker(m_profileCombo);
        m_profileCombo->setCurrentIndex(m_profileCombo->findData(-1));
    }
    updateProfileSummary();
    applyParamsChanged();
}

void RawDevelopWindow::updateProfileSummary()
{
    if (!m_profileSummary) return;
    if (m_params.inputProfileMode() == RawDevelopParams::InputProfileMode::Custom) {
        m_profileSummary->setText(tr("Custom input profile: %1").arg(QString::fromStdString(m_params.customInputProfile())));
        return;
    }
    if (!m_rawData.source || m_params.inputProfileMode() != RawDevelopParams::InputProfileMode::CameraMatched) {
        m_profileSummary->clear();
        return;
    }
    const MatchedCameraProfile matched = RawEngine::matchedCameraProfile(m_rawData.source->meta);
    const QString kind = matched.kind == MatchedProfileKind::Dcp ? tr("DCP")
        : matched.kind == MatchedProfileKind::Icc ? tr("ICC") : tr("camera matrix");
    m_profileSummary->setText(tr("Automatic match: %1 (%2)")
        .arg(QString::fromStdString(matched.matchedCameraName), kind));
}

void RawDevelopWindow::resetAll()
{
    m_params = RawDevelopParams::cameraDefaults();
    syncControlsFromParams();
    applyParamsChanged();
}

curves::CurvesData RawDevelopWindow::curveDataFromParams() const
{
    curves::CurvesData data;
    for (int i = 0; i < 4; ++i) {
        curves::PointList points;
        for (const auto& point : m_params.curve(static_cast<RawDevelopParams::CurveChannel>(i)))
            points.emplace_back(qRound(point.x * 255.0), qRound(point.y * 255.0));
        data.points(static_cast<curves::Channel>(i)) = curves::sanitize(points);
    }
    data.preset = data.isIdentity() ? QStringLiteral("Default") : QStringLiteral("Custom");
    return data;
}

void RawDevelopWindow::setCurvesFromEditor()
{
    if (!m_curveEditor)
        return;
    const curves::CurvesData& data = m_curveEditor->curveData();
    const curves::CurvesData previous = curveDataFromParams();
    for (int i = 0; i < 4; ++i) {
        const curves::Channel channel = static_cast<curves::Channel>(i);
        if (data.points(channel) == previous.points(channel))
            continue;
        std::vector<RawDevelopParams::CurvePoint> points;
        for (const QPoint& point : data.points(channel))
            points.push_back({point.x() / 255.0, point.y() / 255.0});
        m_params.setCurve(static_cast<RawDevelopParams::CurveChannel>(i), points);
    }
}

void RawDevelopWindow::setApplyInProgress(bool busy)
{
    m_applyInProgress = busy;
    if (centralWidget())
        centralWidget()->setEnabled(!busy);
    if (busy)
        m_previewError.clear();
    refreshPreviewStatus();
}

void RawDevelopWindow::developFinished(bool ok, const QString& errorMessage)
{
    if (!ok)
        m_previewError = errorMessage.isEmpty() ? tr("RAW development failed.") : errorMessage;
    setApplyInProgress(false);
    if (ok)
        close();
}

void RawDevelopWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Recomposing and re-requesting the 1:1 slice is driven by the view's own
    // resize (RawPreviewView::scrolled); only the readout is this window's
    // business, and only while Fit derives the zoom from the viewport.
    if (m_fitToWindow) {
        updateZoomControls();
        refreshPreviewStatus();
    }
}

void RawDevelopWindow::closeEvent(QCloseEvent* event)
{
    if (m_applyInProgress) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
