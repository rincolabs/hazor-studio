#include "RawDevelopWindow.hpp"

#include "core/AppPaths.hpp"
#include "ui/AppCheckBox.hpp"
#include "histogram/HistogramGenerator.hpp"
#include "raw/RawDevelopService.hpp"
#include "raw/RawEngine.hpp"
#include "raw/RawSession.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/ColorTrack.hpp"
#include "ui/CurvesEditorWidget.hpp"
#include "ui/HistogramWidget.hpp"
#include "ui/RawDevelopSlider.hpp"

#include <QtConcurrent/QtConcurrent>

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QRadialGradient>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

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

        for (int degrees = 0; degrees < 360; ++degrees) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor::fromHsv(degrees, 255, 255));
            painter.drawPie(circle, degrees * 16, 2 * 16);
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

        QColor color;
        color.setRgbF(std::clamp((m_values[0] + 100.0) / 200.0, 0.0, 1.0),
                      std::clamp((m_values[1] + 100.0) / 200.0, 0.0, 1.0),
                      std::clamp((m_values[2] + 100.0) / 200.0, 0.0, 1.0));
        float hue = 0.0f, saturation = 0.0f, value = 0.0f;
        color.getHsvF(&hue, &saturation, &value);
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
        QColor color = QColor::fromHsvF(hue, distance, 1.0);
        m_values = {color.redF() * 200.0 - 100.0,
                    color.greenF() * 200.0 - 100.0,
                    color.blueF() * 200.0 - 100.0};
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

RawDevelopWindow::RawDevelopWindow(int flatIndex, const RawLayerData& rawData,
                                   const QImage& currentImage, QWidget* parent)
    : QMainWindow(parent)
    , m_flatIndex(flatIndex)
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
    buildUi();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &RawDevelopWindow::applyTheme);
    syncControlsFromParams();
    updatePreviewImage(m_previewImage);
    updateProfileSummary();
    requestPreview();
}

RawDevelopWindow::~RawDevelopWindow()
{
    if (m_previewWatcher && m_previewWatcher->isRunning())
        m_previewWatcher->waitForFinished();
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

    auto* body = new QHBoxLayout;
    body->setSpacing(theme->spaceMD);
    rootLayout->addLayout(body, 1);

    auto* previewPane = new QWidget(root);
    previewPane->setObjectName(QStringLiteral("rawDevelopPreviewPane"));
    auto* previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(theme->spaceSM);

    m_previewScroll = new QScrollArea(previewPane);
    m_previewScroll->setObjectName(QStringLiteral("rawDevelopPreview"));
    m_previewScroll->setWidgetResizable(false);
    m_previewScroll->setAlignment(Qt::AlignCenter);
    m_previewScroll->setBackgroundRole(QPalette::Dark);
    m_previewScroll->viewport()->setObjectName(QStringLiteral("rawDevelopPreviewViewport"));
    m_imageLabel = new QLabel(m_previewScroll);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setText(tr("Preparing RAW preview…"));
    m_previewScroll->setWidget(m_imageLabel);
    previewLayout->addWidget(m_previewScroll, 1);

    auto* previewTools = new QHBoxLayout;
    m_zoomCombo = new QComboBox(previewPane);
    m_zoomCombo->addItem(tr("Fit"), 0.0);
    m_zoomCombo->addItem(QStringLiteral("25%"), .25);
    m_zoomCombo->addItem(QStringLiteral("50%"), .50);
    m_zoomCombo->addItem(QStringLiteral("100%"), 1.0);
    m_zoomCombo->addItem(QStringLiteral("200%"), 2.0);
    previewTools->addWidget(m_zoomCombo);
    auto* fitButton = new QToolButton(previewPane);
    fitButton->setText(tr("Fit"));
    fitButton->setToolTip(tr("Fit preview to window"));
    previewTools->addWidget(fitButton);
    auto* hundredButton = new QToolButton(previewPane);
    hundredButton->setText(QStringLiteral("100%"));
    hundredButton->setToolTip(tr("Show at 100%"));
    previewTools->addWidget(hundredButton);
    previewTools->addStretch();
    m_beforeAfterButton = new QToolButton(previewPane);
    m_beforeAfterButton->setText(tr("Before"));
    m_beforeAfterButton->setCheckable(true);
    m_beforeAfterButton->setToolTip(tr("Toggle Before / After"));
    previewTools->addWidget(m_beforeAfterButton);
    m_shadowClipButton = new QToolButton(previewPane);
    m_shadowClipButton->setText(tr("Shadows"));
    m_shadowClipButton->setCheckable(true);
    m_shadowClipButton->setToolTip(tr("Show clipped shadows in blue"));
    previewTools->addWidget(m_shadowClipButton);
    m_highlightClipButton = new QToolButton(previewPane);
    m_highlightClipButton->setText(tr("Highlights"));
    m_highlightClipButton->setCheckable(true);
    m_highlightClipButton->setToolTip(tr("Show clipped highlights in red"));
    previewTools->addWidget(m_highlightClipButton);
    previewLayout->addLayout(previewTools);

    m_previewStatus = new QLabel(previewPane);
    m_previewStatus->setStyleSheet(QStringLiteral("color: %1;").arg(theme->colorTextSecondary.name()));
    previewLayout->addWidget(m_previewStatus);
    body->addWidget(previewPane, 1);

    auto* side = new QWidget(root);
    side->setObjectName(QStringLiteral("rawDevelopPanel"));
    side->setMinimumWidth(330);
    side->setMaximumWidth(430);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(theme->spaceMD, theme->spaceSM,
                                   theme->spaceMD, theme->spaceSM);
    sideLayout->setSpacing(theme->spaceSM);

    m_histogram = new HistogramWidget(side);
    m_histogram->setHasDocument(true);
    m_histogram->setChannel(HistogramChannel::Colors);
    sideLayout->addWidget(m_histogram);

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

    auto* bottom = new QHBoxLayout;
    auto* resetAllButton = new QPushButton(tr("Restore Defaults"), root);
    bottom->addWidget(resetAllButton);
    bottom->addStretch();
    auto* cancelButton = new QPushButton(tr("Cancel"), root);
    bottom->addWidget(cancelButton);
    m_applyButton = new QToolButton(root);
    m_applyButton->setText(tr("Apply"));
    m_applyButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bottom->addWidget(m_applyButton);
    rootLayout->addLayout(bottom);

    m_previewDebounce = new QTimer(this);
    m_previewDebounce->setSingleShot(true);
    m_previewWatcher = new QFutureWatcher<PreviewResult>(this);
    connect(m_previewDebounce, &QTimer::timeout, this, &RawDevelopWindow::startPreview);
    connect(m_previewWatcher, &QFutureWatcher<PreviewResult>::finished,
            this, &RawDevelopWindow::previewFinished);
    connect(m_zoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const double zoom = m_zoomCombo->itemData(index).toDouble();
        m_fitToWindow = zoom <= 0.0;
        if (!m_fitToWindow) m_zoom = zoom;
        updateDisplayedImage();
    });
    connect(fitButton, &QToolButton::clicked, this, [this]() {
        m_fitToWindow = true; m_zoomCombo->setCurrentIndex(0); updateDisplayedImage();
    });
    connect(hundredButton, &QToolButton::clicked, this, [this]() {
        m_fitToWindow = false; m_zoom = 1.0; m_zoomCombo->setCurrentIndex(3); updateDisplayedImage();
    });
    connect(m_beforeAfterButton, &QToolButton::toggled, this, [this](bool before) {
        m_showBefore = before;
        m_beforeAfterButton->setText(before ? tr("After") : tr("Before"));
        updateDisplayedImage();
        updateHistogram(before ? m_beforeImage : m_previewImage, false);
    });
    connect(m_shadowClipButton, &QToolButton::toggled, this, [this](bool on) {
        m_clipShadows = on; updateDisplayedImage();
    });
    connect(m_highlightClipButton, &QToolButton::toggled, this, [this](bool on) {
        m_clipHighlights = on; updateDisplayedImage();
    });
    connect(autoButton, &QToolButton::clicked, this, [this]() {
        auto light = m_params.light(); light.autoExposure = true; m_params.setLight(light);
        auto wb = m_params.whiteBalance(); wb.method = "Auto"; m_params.setWhiteBalance(wb);
        syncControlsFromParams(); applyParamsChanged();
    });
    connect(m_blackWhiteCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) return;
        auto color = m_params.color(); color.blackAndWhite = enabled; m_params.setColor(color); applyParamsChanged();
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
    connect(m_applyButton, &QToolButton::clicked, this, [this]() {
        if (!m_applyInProgress) emit applyRequested(m_flatIndex, m_params);
    });

    applyTheme();
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
            "QWidget#rawDevelopRoot { background: %1; color: %2; }"
            "QWidget#rawDevelopPreviewPane { background: %1; }"
            "QWidget#rawDevelopPanel { background: %3; }"
            "QWidget#rawDevelopSectionHeader { background: %3; border-bottom: %4px solid %5; }"
            "QWidget#rawDevelopSectionContents { background: %3; }"
            "QWidget#rawDevelopPanel QLabel { background: transparent; border: none; }")
            .arg(theme->colorBackgroundPrimary.name(), theme->colorTextPrimary.name(),
                 theme->colorSurface.name())
            .arg(theme->borderThinWidth)
            .arg(theme->colorBorder.name()));
    }

    if (m_previewScroll) {
        m_previewScroll->setStyleSheet(QStringLiteral(
            "QScrollArea#rawDevelopPreview { background: %1; border: %2px solid %3; }"
            "QWidget#rawDevelopPreviewViewport { background: %1; }")
            .arg(theme->colorSurfaceDark.name())
            .arg(theme->borderThinWidth)
            .arg(theme->colorBorder.name()));
    }

    if (auto* adjustmentScroll = findChild<QScrollArea*>(QStringLiteral("rawDevelopAdjustments"))) {
        adjustmentScroll->setStyleSheet(QStringLiteral(
            "QScrollArea#rawDevelopAdjustments { background: %1; border: none; }"
            "QWidget#rawDevelopAdjustmentsViewport { background: %1; }"
            "QWidget#rawDevelopAdjustmentsContent { background: %1; }")
            .arg(theme->colorSurface.name()));
    }

    const QString secondary = QStringLiteral("color: %1;").arg(theme->colorTextSecondary.name());
    if (m_previewStatus)
        m_previewStatus->setStyleSheet(secondary);
    if (m_profileSummary)
        m_profileSummary->setStyleSheet(secondary);
}

RawDevelopSlider* RawDevelopWindow::addParamRow(QVBoxLayout* layout, const QString& label,
                                        double min, double max, double defaultValue,
                                        int decimals, const QString& toolTip,
                                        const std::function<double()>& currentValue,
                                        const std::function<void(double)>& setValue,
                                        bool logarithmic, bool centerZeroLog, double logBase)
{
    auto* row = new RawDevelopSlider(label, min, max, defaultValue, decimals,
                                     logarithmic, centerZeroLog, logBase);
    row->setToolTip(toolTip);
    layout->addWidget(row);
    connect(row, &RawDevelopSlider::valueChanged, this, [this, setValue](double value) {
        if (m_syncing) return;
        setValue(value); applyParamsChanged();
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
        /*logarithmic=*/false, /*centerZeroLog=*/true, /*logBase=*/2.0);
    addParamRow(layout, tr("Contrast"), -100, 100, 0, 0, tr("Tone curve contrast."),
        [this] { return m_params.light().contrast; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.contrast = qRound(value); m_params.setLight(s); });
    addParamRow(layout, tr("Highlights"), 0, 100, 0, 0, tr("Highlight recovery strength."),
        [this] { return m_params.light().highlights; }, [this](double value) { auto s = m_params.light(); s.highlights = qRound(value); m_params.setLight(s); });
    addParamRow(layout, tr("Shadows"), 0, 100, 0, 0, tr("Shadow recovery strength."),
        [this] { return m_params.light().shadows; }, [this](double value) { auto s = m_params.light(); s.shadows = qRound(value); m_params.setLight(s); });
    addParamRow(layout, tr("Whites"), 0, 500, 0, 0, tr("Engine mapping: Highlight Compression. Positive values recover, rather than raise, white detail."),
        [this] { return m_params.light().whiteRecovery; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.whiteRecovery = qRound(value); m_params.setLight(s); });
    addParamRow(layout, tr("Blacks"), -16384, 32768, 0, 0, tr("Engine mapping: Black Level; this is not Adobe's normalized Blacks control."),
        [this] { return m_params.light().blackPoint; }, [this](double value) { auto s = m_params.light(); s.autoExposure = false; s.blackPoint = qRound(value); m_params.setLight(s); },
        /*logarithmic=*/false, /*centerZeroLog=*/true, /*logBase=*/10.0);
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
    const QList<QPair<QString, QString>> methods = {
        {tr("As Shot"), QStringLiteral("Camera")}, {tr("Auto"), QStringLiteral("Auto")},
        {tr("Daylight"), QStringLiteral("Daylight")}, {tr("Cloudy"), QStringLiteral("Cloudy")},
        {tr("Shade"), QStringLiteral("Shade")}, {tr("Tungsten"), QStringLiteral("Tungsten")},
        {tr("Fluorescent"), QStringLiteral("Fluorescent")}, {tr("Flash"), QStringLiteral("Flash")},
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

    m_temperatureSlider = addParamRow(layout, tr("Temperature"), 1500, 60000, 6504, 0,
        tr("White-balance color temperature, in Kelvin."),
        [this] { return m_params.whiteBalance().temperatureKelvin; }, [this](double value) {
            auto wb = m_params.whiteBalance(); wb.method = "Custom"; wb.temperatureKelvin = qRound(value); m_params.setWhiteBalance(wb);
            QSignalBlocker blocker(m_whiteBalanceCombo);
            m_whiteBalanceCombo->setCurrentIndex(m_whiteBalanceCombo->findData(QStringLiteral("Custom")));
        });
    const auto* theme = ThemeManager::instance()->current();
    m_temperatureSlider->setStops({
        {0.0, theme->colorAccentPressed},
        {0.42, theme->colorTextSecondary},
        {0.72, theme->colorWarning},
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
        }, /*logarithmic=*/true);
    connect(m_whiteBalanceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) return;
        auto wb = m_params.whiteBalance(); wb.method = m_whiteBalanceCombo->itemData(index).toString().toStdString(); m_params.setWhiteBalance(wb); applyParamsChanged();
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
        [this] { return m_params.color().vibrance; }, [this](double value) { auto s = m_params.color(); s.vibrance = qRound(value); m_params.setColor(s); });
    vibrance->setStops({
        {0.0, theme->colorAccentPressed},
        {0.5, theme->colorTextDisabled},
        {1.0, theme->colorDanger}
    });
    auto* saturation = addParamRow(layout, tr("Saturation"), -100, 100, 0, 0, tr("Tone curve saturation."),
        [this] { return m_params.color().saturation; }, [this](double value) { auto s = m_params.color(); s.saturation = qRound(value); m_params.setColor(s); });
    saturation->setStops(ColorStops::hueSpectrum());
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
    auto* textureEnabled = addEnable(tr("Texture"), tr("Engine mapping: Sharpen Microcontrast."), m_params.effects().textureEnabled,
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
    auto* grain = new QLabel(tr("Grain is unavailable: the embedded engine has no equivalent non-destructive grain operation."), panel);
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
    auto* tabs = new QTabWidget(panel);
    const std::array<QString, 4> names{tr("RGB"), tr("Red"), tr("Green"), tr("Blue")};
    const std::array<QColor, 4> colors{QColor(220, 220, 220), QColor(220, 80, 80), QColor(90, 205, 115), QColor(85, 135, 240)};
    for (int i = 0; i < 4; ++i) {
        auto* graph = new CurveGraphWidget(tabs);
        graph->setChannelColor(colors[i]);
        m_curveGraphs[i] = graph;
        syncCurveGraph(graph, static_cast<RawDevelopParams::CurveChannel>(i));
        connect(graph, &CurveGraphWidget::pointsChanged, this, [this, graph, i]() {
            if (!m_syncing) { setCurveFromGraph(graph, static_cast<RawDevelopParams::CurveChannel>(i)); applyParamsChanged(); }
        });
        tabs->addTab(graph, names[i]);
    }
    layout->addWidget(tabs);
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
                [this, channel, band](double value) {
                    auto m = m_params.colorMixer();
                    if (channel == 0) m.hue[band] = value;
                    else if (channel == 1) m.saturation[band] = value;
                    else m.luminance[band] = value;
                    m.enabled = true; m_params.setColorMixer(m);
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
        wheel->changed = [this, wheel, i]() {
            if (m_syncing) return;
            auto grading = m_params.colorGrading();
            if (i == 0) grading.shadows = wheel->values();
            else if (i == 1) grading.midtones = wheel->values();
            else grading.highlights = wheel->values();
            grading.enabled = true; m_params.setColorGrading(grading); applyParamsChanged();
        };
        m_syncControls.push_back([this, wheel, i]() {
            const auto g = m_params.colorGrading();
            wheel->setValues(i == 0 ? g.shadows : i == 1 ? g.midtones : g.highlights);
        });
    }
    layout->addLayout(wheels);
    addParamRow(layout, tr("Blending"), 0, 100, 50, 0, tr("Engine mapping: Color Toning strength."),
        [this] { return m_params.colorGrading().blending; }, [this](double value) { auto g = m_params.colorGrading(); g.blending = qRound(value); m_params.setColorGrading(g); });
    addParamRow(layout, tr("Balance"), -100, 100, 0, 0, tr("Color Toning balance."),
        [this] { return m_params.colorGrading().balance; }, [this](double value) { auto g = m_params.colorGrading(); g.balance = qRound(value); m_params.setColorGrading(g); });
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
    auto* limitations = new QLabel(tr("Masking is unavailable: the engine's edge-only control is not equivalent to Camera Raw Masking. Its luminance denoiser has no compatible Luminance Contrast, Color Detail, or Smoothness controls."), panel);
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
    return panel;
}

void RawDevelopWindow::syncControlsFromParams()
{
    m_syncing = true;
    for (const auto& sync : m_syncControls) sync();
    if (m_blackWhiteCheck) m_blackWhiteCheck->setChecked(m_params.color().blackAndWhite);
    for (int i = 0; i < 4; ++i)
        if (m_curveGraphs[i]) syncCurveGraph(m_curveGraphs[i], static_cast<RawDevelopParams::CurveChannel>(i));
    if (m_profileCombo) {
        const int mode = m_params.inputProfileMode() == RawDevelopParams::InputProfileMode::Custom
            ? -1 : static_cast<int>(m_params.inputProfileMode());
        const int index = m_profileCombo->findData(mode);
        if (index >= 0) m_profileCombo->setCurrentIndex(index);
    }
    m_syncing = false;
    updateProfileSummary();
}

void RawDevelopWindow::applyParamsChanged()
{
    ++m_previewGeneration;
    m_showBefore = false;
    if (m_beforeAfterButton) {
        QSignalBlocker blocker(m_beforeAfterButton);
        m_beforeAfterButton->setChecked(false);
        m_beforeAfterButton->setText(tr("Before"));
    }
    requestPreview();
}

void RawDevelopWindow::requestPreview()
{
    if (!m_previewDebounce) return;
    m_previewStatus->setText(tr("Updating RAW preview…"));
    m_previewDebounce->start(180);
}

void RawDevelopWindow::startPreview()
{
    if (!RawEngine::isAvailable() || !m_rawData.source) {
        m_previewStatus->setText(tr("RAW preview is unavailable in this build."));
        return;
    }
    if (m_previewWatcher->isRunning()) {
        m_previewPending = true;
        return;
    }
    if (m_previewSourcePath.isEmpty() && m_rawData.mode == RawStorageMode::Linked)
        m_previewSourcePath = m_rawData.source->path;
    if (m_previewSourcePath.isEmpty() && m_rawData.mode != RawStorageMode::Embedded) {
        m_previewStatus->setText(tr("Could not prepare the RAW source for preview."));
        return;
    }
    const RawDevelopParams params = m_params;
    const QString existingSourcePath = m_previewSourcePath;
    // Embedded sources materialize (write tens of MB to a temp file) lazily,
    // the first time, inside the worker below — never on the UI thread.
    const std::shared_ptr<const RawSource> source = m_rawData.source;
    const quint64 generation = m_previewGeneration;
    m_previewPending = false;
    m_previewWatcher->setFuture(QtConcurrent::run([params, existingSourcePath, source, generation]() {
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
        std::string error;
        auto session = RawEngine::openFile(sourcePath.toStdString(), &error);
        if (!session) {
            result.errorMessage = QString::fromStdString(error);
            return result;
        }
        const RawImageBuffer buffer = session->renderPreview(2048, params);
        if (buffer.isEmpty()) {
            result.errorMessage = QObject::tr("RAW preview render failed.");
            return result;
        }
        result.image = RawDevelopService::convertToRgba8888(buffer, false);
        return result;
    }));
}

void RawDevelopWindow::previewFinished()
{
    const PreviewResult result = m_previewWatcher->result();
    if (m_previewSourcePath.isEmpty() && !result.sourcePath.isEmpty()) {
        m_previewSourcePath = result.sourcePath;
        m_ownsPreviewSourcePath = result.ownsSourcePath;
    }
    if (result.generation == m_previewGeneration && !result.image.isNull()) {
        updatePreviewImage(result.image);
        m_previewStatus->setText(tr("Preview uses interactive quality."));
    } else if (result.generation == m_previewGeneration && !result.errorMessage.isEmpty()) {
        m_previewStatus->setText(result.errorMessage);
    }
    if (m_previewPending || result.generation != m_previewGeneration) {
        m_previewPending = false;
        QTimer::singleShot(0, this, &RawDevelopWindow::startPreview);
    }
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
    if (!m_imageLabel || !m_previewScroll) return;
    QImage display = m_showBefore ? m_beforeImage : m_previewImage;
    if (display.isNull()) return;
    if (m_clipShadows || m_clipHighlights) {
        display = display.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < display.height(); ++y) {
            uchar* row = display.scanLine(y);
            for (int x = 0; x < display.width(); ++x) {
                uchar* px = row + x * 4;
                const int lo = std::min({px[0], px[1], px[2]});
                const int hi = std::max({px[0], px[1], px[2]});
                if (m_clipShadows && lo <= 3) { px[0] = 30; px[1] = 105; px[2] = 255; }
                if (m_clipHighlights && hi >= 252) { px[0] = 255; px[1] = 55; px[2] = 35; }
            }
        }
    }
    QSize target = display.size();
    if (m_fitToWindow) {
        const QSize available = m_previewScroll->viewport()->size() - QSize(20, 20);
        target = display.size().scaled(available.expandedTo(QSize(1, 1)), Qt::KeepAspectRatio);
    } else {
        // Zoom percentages are relative to the document's actual resolution
        // (m_beforeImage — the layer's full develop), not display.size():
        // the interactive "After" preview is capped to renderPreview()'s
        // 2048px maxEdge for speed, so sizing off it would make "100%" show
        // roughly a third of true size on a typical multi-megapixel RAW.
        target = (QSizeF(m_beforeImage.size()) * m_zoom).toSize();
    }
    const QPixmap pixmap = QPixmap::fromImage(display.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_imageLabel->setPixmap(pixmap);
    m_imageLabel->setFixedSize(pixmap.size());
}

void RawDevelopWindow::updateHistogram(const QImage& image, bool stale)
{
    if (image.isNull() || !m_histogram) return;
    HistogramGenerator::Options options;
    options.autoDownsample = true;
    m_histogram->setData(HistogramGenerator::generate(image, nullptr, options));
    m_histogram->setStale(stale);
}

void RawDevelopWindow::reloadPresets()
{
    if (!m_presetCombo) return;
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItem(tr("Camera Default"), QStringLiteral("default"));
    const QDir bundled(QDir(AppPaths::rawEngineResourceDir()).filePath(QStringLiteral("profiles")));
    for (const QString& file : bundled.entryList({QStringLiteral("*.pp3"), QStringLiteral("*.PP3")}, QDir::Files))
        m_presetCombo->addItem(file, bundled.filePath(file));
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
            m_previewStatus->setText(tr("Could not load preset %1.").arg(QFileInfo(key).fileName()));
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

void RawDevelopWindow::setCurveFromGraph(CurveGraphWidget* graph, RawDevelopParams::CurveChannel channel)
{
    std::vector<RawDevelopParams::CurvePoint> points;
    for (const QPoint& point : graph->points())
        points.push_back({point.x() / 255.0, point.y() / 255.0});
    m_params.setCurve(channel, points);
}

void RawDevelopWindow::syncCurveGraph(CurveGraphWidget* graph, RawDevelopParams::CurveChannel channel)
{
    std::vector<QPoint> points;
    for (const auto& point : m_params.curve(channel))
        points.emplace_back(qRound(point.x * 255.0), qRound(point.y * 255.0));
    graph->setPoints(points);
}

void RawDevelopWindow::setApplyInProgress(bool busy)
{
    m_applyInProgress = busy;
    if (centralWidget())
        centralWidget()->setEnabled(!busy);
    if (busy) {
        m_previewStatus->setText(tr("Applying full-quality RAW development…"));
    }
}

void RawDevelopWindow::developFinished(bool ok, const QString& errorMessage)
{
    setApplyInProgress(false);
    if (ok) {
        close();
    } else {
        m_previewStatus->setText(errorMessage.isEmpty() ? tr("RAW development failed.") : errorMessage);
    }
}

void RawDevelopWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_fitToWindow) updateDisplayedImage();
}

void RawDevelopWindow::closeEvent(QCloseEvent* event)
{
    if (m_applyInProgress) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
