#include "HistogramPanel.hpp"

#include "HistogramWidget.hpp"
#include "HistogramStatsWidget.hpp"
#include "histogram/HistogramCacheManager.hpp"
#include "histogram/HistogramGenerator.hpp"
#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "io/ImageIO.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>
#include <QTimer>
#include <QFrame>

namespace {
constexpr int kDebounceMs = 130;
}

HistogramPanel::HistogramPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("histogramPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    m_cache = new HistogramCacheManager(this);
    connect(m_cache, &HistogramCacheManager::ready,
            this, &HistogramPanel::onCacheReady);
    connect(m_cache, &HistogramCacheManager::computingChanged,
            this, &HistogramPanel::onComputingChanged);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, [this]() { recompute(false); });

    buildUi();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HistogramPanel::applyTheme);
    applyTheme();
}

void HistogramPanel::buildUi()
{
    auto* t = ThemeManager::instance()->current();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    lay->setSpacing(t->spaceSM);

    // ── Channel row ──
    auto* row = new QHBoxLayout();
    row->setSpacing(t->spaceSM);

    auto* channelLabel = new QLabel(tr("Channel:"), this);
    row->addWidget(channelLabel);

    m_channelCombo = new QComboBox(this);
    m_channelCombo->addItem(tr("Colors"));
    m_channelCombo->addItem(tr("RGB"));
    m_channelCombo->addItem(tr("Red"));
    m_channelCombo->addItem(tr("Green"));
    m_channelCombo->addItem(tr("Blue"));
    m_channelCombo->addItem(tr("Luminosity"));
    m_channelCombo->setCurrentIndex(0);
    row->addWidget(m_channelCombo, 1);

    m_refreshBtn = new QToolButton(this);
    m_refreshBtn->setText(QString::fromUtf8("↻")); // ↻
    m_refreshBtn->setToolTip(tr("Recalculate histogram"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setAutoRaise(true);
    row->addWidget(m_refreshBtn);

    lay->addLayout(row);

    // ── Graph ──
    m_graph = new HistogramWidget(this);
    lay->addWidget(m_graph, 1);

    // ── Separator ──
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color:%1;").arg(t->colorSurface.name()));
    lay->addWidget(sep);

    // ── Stats ──
    m_stats = new HistogramStatsWidget(this);
    lay->addWidget(m_stats);

    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HistogramPanel::onChannelChanged);
    connect(m_refreshBtn, &QToolButton::clicked, this, [this]() { recompute(true); });
    connect(m_graph, &HistogramWidget::levelHovered, this, &HistogramPanel::onLevelHovered);

    m_graph->setHasDocument(false);
}

void HistogramPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("HistogramPanel { background: %1; color: %2; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));
    update();
}

void HistogramPanel::setController(ImageController* ctrl)
{
    for (const auto& c : m_conns)
        QObject::disconnect(c);
    m_conns.clear();

    m_ctrl = ctrl;
    m_cache->invalidate();
    m_lastGeneration = ~0ull;
    m_dirty = true;

    if (m_ctrl) {
        auto hook = [this](void (ImageController::*sig)()) {
            m_conns << connect(m_ctrl, sig, this, [this]() { scheduleUpdate(); });
        };
        hook(&ImageController::imageChanged);
        hook(&ImageController::documentChanged);
        hook(&ImageController::selectionChanged);
        hook(&ImageController::historyChanged);
        m_conns << connect(m_ctrl, &ImageController::activeLayerChanged,
                           this, [this](int) { scheduleUpdate(); });
        m_conns << connect(m_ctrl, &ImageController::layerChanged,
                           this, [this](int) { scheduleUpdate(); });
        m_conns << connect(m_ctrl, &ImageController::maskEditingChanged,
                           this, [this](bool) { scheduleUpdate(); });
        // Suspend the (synchronous, full-composite) histogram recompute for the
        // duration of an adjustment drag so a mid-gesture pause can't fire it and
        // stall the UI. One recompute runs when the gesture settles.
        m_conns << connect(m_ctrl, &ImageController::adjustmentLiveEditChanged,
                           this, [this](bool active) {
            m_liveEditActive = active;
            if (active)
                m_debounce->stop(); // cancel a recompute armed just before the drag
            else
                scheduleUpdate();   // settled → recompute once for the final state
        });
    } else {
        // No active document — reflect immediately.
        m_graph->setHasDocument(false);
        m_graph->setStale(false);
        m_graph->setWarningReason(QString());
        m_stats->clear();
    }

    scheduleUpdate();
}

void HistogramPanel::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_dirty)
        recompute(false);
}

void HistogramPanel::scheduleUpdate()
{
    m_dirty = true;
    // Never recompute (full CPU compositeImage) mid-drag: while an adjustment is
    // being dragged, imageChanged streams in continuously and pausing the mouse
    // would let the debounce fire and stall the UI. Stay dirty and recompute once
    // when the live edit ends (adjustmentLiveEditChanged(false) re-schedules).
    if (m_liveEditActive)
        return;
    // Always arm the timer: when it fires, recompute() does the heavy work only
    // if the panel is genuinely visible, otherwise it defers to showEvent(). This
    // is robust to signals that arrive before the panel is fully realized (e.g.
    // the layers created right after a document opens).
    m_debounce->start();
}

void HistogramPanel::recompute(bool force)
{
    // Defer heavy work while hidden; showEvent picks it up later.
    if (!isVisible() && !force) {
        m_dirty = true;
        return;
    }

    Document* doc = m_ctrl ? m_ctrl->document() : nullptr;
    if (!doc || doc->size.isEmpty()) {
        m_graph->setHasDocument(false);
        m_graph->setStale(false);
        m_graph->setWarningReason(QString());
        m_stats->clear();
        m_dirty = false;
        return;
    }

    m_graph->setHasDocument(true);

    const uint64_t gen = doc->compositionGeneration;
    if (!force && m_cache->data().valid && gen == m_lastGeneration) {
        // Composition unchanged — just refresh derived display.
        onCacheReady();
        m_dirty = false;
        return;
    }

    // Build a detached composite on the UI thread (safe document read); the
    // per-pixel binning runs on a worker thread inside the cache manager.
    const QImage source = compositeImage(doc);

    HistogramGenerator::Options opts;
    opts.source = HistogramSource::EntireImage;
    opts.autoDownsample = true;
    opts.maxSampledPixels = 2'000'000;

    HistogramKey key;
    key.generation = gen;
    key.source = HistogramSource::EntireImage;

    m_lastGeneration = gen;
    m_dirty = false;
    m_cache->request(source, QImage(), key, opts, force);
    updateWarning();
}

void HistogramPanel::onCacheReady()
{
    m_graph->setData(m_cache->data());
    updateStatsFromData();
    updateWarning();
}

void HistogramPanel::onComputingChanged(bool computing)
{
    m_graph->setStale(computing);
    updateWarning();
}

void HistogramPanel::onChannelChanged(int index)
{
    m_channel = channelForIndex(index);
    m_graph->setChannel(m_channel);
    updateStatsFromData(); // channel affects which stats/probe are shown
}

void HistogramPanel::onLevelHovered(int level)
{
    m_hoverLevel = level;
    updateProbe();
}

void HistogramPanel::updateStatsFromData()
{
    const HistogramData& d = m_cache->data();
    if (!d.valid) {
        m_stats->clear();
        return;
    }
    const HistogramStats s = HistogramGenerator::statsFor(d, m_channel);
    m_stats->setStats(s.mean, s.stdDev, s.median, d.pixelCount);
    m_stats->setCacheLevel(sampleLevelText(d.sampleLevel));
    updateProbe();
}

void HistogramPanel::updateProbe()
{
    const HistogramData& d = m_cache->data();
    if (!d.valid || m_hoverLevel < 0 || m_hoverLevel > 255) {
        m_stats->setProbe(-1, 0, 0.0);
        return;
    }
    const std::array<int, 256>& bins = d.bins(m_channel);
    long long cumulative = 0;
    long long total = 0;
    for (int i = 0; i < 256; ++i) {
        total += bins[i];
        if (i <= m_hoverLevel)
            cumulative += bins[i];
    }
    const double pct = total > 0 ? (100.0 * cumulative / total) : 0.0;
    m_stats->setProbe(m_hoverLevel, bins[m_hoverLevel], pct);
}

void HistogramPanel::updateWarning()
{
    const HistogramData& d = m_cache->data();
    QString reason;
    if (m_cache->isComputing()) {
        reason = tr("Recalculating — showing the previous result.");
    } else if (d.valid && d.sampleLevel != HistogramSampleLevel::Full) {
        reason = tr("Computed from a %1 downsampled preview for performance.")
                     .arg(sampleLevelText(d.sampleLevel));
    }
    m_graph->setWarningReason(reason);
}

HistogramChannel HistogramPanel::channelForIndex(int index)
{
    switch (index) {
        case 0: return HistogramChannel::Colors;
        case 1: return HistogramChannel::RGB;
        case 2: return HistogramChannel::Red;
        case 3: return HistogramChannel::Green;
        case 4: return HistogramChannel::Blue;
        case 5: return HistogramChannel::Luminosity;
        default: return HistogramChannel::Colors;
    }
}

QString HistogramPanel::sampleLevelText(HistogramSampleLevel level)
{
    switch (level) {
        case HistogramSampleLevel::Full:    return tr("Full");
        case HistogramSampleLevel::Half:    return QStringLiteral("1/2");
        case HistogramSampleLevel::Quarter: return QStringLiteral("1/4");
        case HistogramSampleLevel::Eighth:  return QStringLiteral("1/8");
    }
    return tr("Full");
}
