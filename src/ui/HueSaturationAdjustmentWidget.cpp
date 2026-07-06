#include "HueSaturationAdjustmentWidget.hpp"
#include "GradientSlider.hpp"

#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "io/ImageIO.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AdjustmentPanelBar.hpp"
#include "ui/AppCheckBox.hpp"
#include "ui/IconUtils.hpp"

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

// HueRangeBar handle geometry, shared by painting and hit-testing so the drawn
// handles and their grab zones can never drift apart.
constexpr double kTabW = 7.0;    // outer falloff tab width (the tab points outward)
constexpr double kBarW = 5.0;    // inner 100%-region bar width
constexpr double kBarGap = 2.0;  // minimum clear gap kept between the two inner bars

inline float wrap360f(float h)
{
    h = std::fmod(h, 360.0f);
    if (h < 0.0f)
        h += 360.0f;
    return h;
}

inline float clampf(float v, float lo, float hi)
{
    return std::min(std::max(v, lo), hi);
}

// Lightness response, mirroring HueSaturationData::applyLightness (a∈[-1,1]).
inline float applyLightPreview(float l, float a)
{
    a = clampf(a, -1.0f, 1.0f);
    return a >= 0.0f ? l + a * (1.0f - l) : l * (1.0f + a);
}

QToolButton* makeToolBtn(const QString& icon, const QString& tip)
{
    auto* b = new QToolButton();
    b->setIcon(makeIcon(icon));
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setAutoRaise(true);
    b->setIconSize(QSize(20, 20));
    return b;
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
//  HueRangeBar
// ════════════════════════════════════════════════════════════════════════════

HueRangeBar::HueRangeBar(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(46);
    setMaximumHeight(52);
    setMouseTracking(false);
}

void HueRangeBar::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    update();
}

void HueRangeBar::setEdges(float outerStart, float innerStart,
                           float innerEnd, float outerEnd)
{
    m_edges[0] = outerStart;
    m_edges[1] = innerStart;
    m_edges[2] = innerEnd;
    m_edges[3] = outerEnd;
    update();
}

void HueRangeBar::setAdjustment(int hueShift, int satAdj, int lightAdj)
{
    if (m_hueShift == hueShift && m_satAdj == satAdj && m_lightAdj == lightAdj)
        return;
    m_hueShift = hueShift;
    m_satAdj = satAdj;
    m_lightAdj = lightAdj;
    update();
}

QRectF HueRangeBar::topStripRect() const
{
    return QRectF(0.5, 0.5, width() - 1.0, 14.0);
}

QRectF HueRangeBar::bottomStripRect() const
{
    return QRectF(0.5, height() - 14.5, width() - 1.0, 14.0);
}

QRectF HueRangeBar::handleGutterRect() const
{
    const double top = topStripRect().bottom();
    const double bot = bottomStripRect().top();
    return QRectF(0.0, top, width(), bot - top);
}

double HueRangeBar::handleXForEdge(float edgeDeg) const
{
    const QRectF s = topStripRect();
    return s.left() + (wrap360f(edgeDeg) / 360.0) * s.width();
}

std::array<double, 4> HueRangeBar::handleCenters() const
{
    const double xOL = handleXForEdge(m_edges[0]);
    const double xIL = handleXForEdge(m_edges[1]);
    const double xIR = handleXForEdge(m_edges[2]);
    const double xOR = handleXForEdge(m_edges[3]);

    // Inner bars sit just inside each inner edge; when the 100% region is too
    // narrow they are recentred on the midpoint and held kBarW + kBarGap apart so
    // the two never touch (mirrors paintEvent step 3). The outer tabs point away
    // from the range, so their grab point is the tab body — offset outward by half
    // its width. This keeps the outer and inner grab zones distinct even when an
    // edge and its falloff coincide, so each handle stays independently grabbable.
    double cL = xIL + kBarW / 2.0;
    double cR = xIR - kBarW / 2.0;
    if (xIR >= xIL) {                          // non-wrapped 100% region
        const double mid = 0.5 * (xIL + xIR);
        const double minSep = kBarW + kBarGap;
        if (cR - cL < minSep) {
            cL = mid - minSep / 2.0;
            cR = mid + minSep / 2.0;
        }
    }
    return { xOL - kTabW / 2.0, cL, cR, xOR + kTabW / 2.0 };
}

int HueRangeBar::hitTestHandle(const QPointF& pos) const
{
    if (!m_active)
        return -1;
    // Hit-test against the handles' drawn centres (not their raw edge positions)
    // so the outer tab and inner bar stay independently grabbable even when their
    // edges coincide — the tab's grab point sits outside the range, the bar's just
    // inside it.
    const std::array<double, 4> cx = handleCenters();
    int best = -1;
    double bestD = 9.0;   // px tolerance
    for (int i = 0; i < 4; ++i) {
        const double d = std::abs(pos.x() - cx[i]);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void HueRangeBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor border = ThemeManager::instance()->current()->colorBorder;
    const QRectF top = topStripRect();
    const QRectF bottom = bottomStripRect();

    // Top strip: the original continuous hue spectrum.
    {
        QLinearGradient grad(top.left(), 0, top.right(), 0);
        for (int i = 0; i <= 6; ++i)
            grad.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
        p.setPen(QPen(border, 1));
        p.setBrush(grad);
        p.drawRoundedRect(top, 2, 2);
    }

    // Bottom strip: preview of the active range's adjustment, sampled per
    // column so the falloff (and wrap) is visible exactly as applied.
    {
        huesaturation::HueRange tmp;
        tmp.outerStart = m_edges[0];
        tmp.innerStart = m_edges[1];
        tmp.innerEnd = m_edges[2];
        tmp.outerEnd = m_edges[3];
        const int x0 = static_cast<int>(std::floor(bottom.left()));
        const int x1 = static_cast<int>(std::ceil(bottom.right()));
        for (int x = x0; x < x1; ++x) {
            const float hue = static_cast<float>((x - bottom.left()) / bottom.width() * 360.0);
            const float w = m_active ? tmp.weight(hue) : 1.0f;
            const float h2 = wrap360f(hue + w * m_hueShift);
            const float s2 = clampf(1.0f * (1.0f + w * m_satAdj / 100.0f), 0.0f, 1.0f);
            const float l2 = applyLightPreview(0.5f, w * m_lightAdj / 100.0f);
            p.setPen(QColor::fromHslF(h2 / 360.0f, s2, clampf(l2, 0.0f, 1.0f)));
            p.drawLine(QPointF(x + 0.5, bottom.top() + 1.0),
                       QPointF(x + 0.5, bottom.bottom() - 1.0));
        }
        p.setPen(QPen(border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(bottom, 2, 2);
    }

    if (!m_active)
        return;

    // ── Range handles, drawn in the gutter between the two
    // spectrum strips. Z-order: gray selected-range band → outer trapezoid tabs
    // → inner vertical bars → dark boundary lines on top. This is purely visual;
    // the drag math lives in HueRange / the editor.
    const QRectF gut = handleGutterRect();
    const double gT = gut.top();
    const double gB = gut.bottom();
    const double gH = gut.height();

    const double xOL = handleXForEdge(m_edges[0]);   // outer left  (falloff start)
    const double xIL = handleXForEdge(m_edges[1]);   // inner left  (100% start)
    const double xIR = handleXForEdge(m_edges[2]);   // inner right (100% end)
    const double xOR = handleXForEdge(m_edges[3]);   // outer right (falloff end)

    const QColor kFill(0xF0, 0xF0, 0xF0);            // light handle fill
    const QColor kEdge(0x20, 0x20, 0x20);            // dark 1px outline
    const QColor kLine(0x1E, 0x1E, 0x1E);            // boundary separation line

    // 1. Translucent gray band over the whole selected range [outerLeft..outerRight].
    //    Drawn as two segments when the range wraps the 0/360 seam (Reds), so it
    //    never paints a wrong full-width rectangle.
    auto fillRangeSpan = [&](double xa, double xb) {
        if (xb <= xa)
            return;
        const QRectF r(xa, gT, xb - xa, gH);
        p.fillRect(r, QColor(190, 190, 190, 120));
        p.setPen(QPen(QColor(40, 40, 40, 180), 1));
        p.drawLine(QPointF(xa, gT + 0.5), QPointF(xb, gT + 0.5));
        p.drawLine(QPointF(xa, gB - 0.5), QPointF(xb, gB - 0.5));
    };
    if (xOL <= xOR) {
        fillRangeSpan(xOL, xOR);
    } else {                                          // wrapped range
        fillRangeSpan(xOL, gut.right());
        fillRangeSpan(gut.left(), xOR);
    }

    // 2. Outer falloff handles — small inclined tabs with the vertical base on
    //    the range side and the short tip pointing *outward*, away from the range
    //    (left tab points left, right tab mirrors it). The tab body therefore sits
    //    entirely on the outer side of its edge; combined with the inner bars being
    //    inset toward the range centre (step 3), the two handle pairs never overlap,
    //    even when an edge and its falloff coincide (zero shoulder). Antialiased.
    p.setRenderHint(QPainter::Antialiasing, true);
    const double tip = gH * 0.32;     // height inset of the short outer tip edge
    QPolygonF leftTab;
    leftTab << QPointF(xOL, gT)
            << QPointF(xOL - kTabW, gT + tip)
            << QPointF(xOL - kTabW, gB - tip)
            << QPointF(xOL, gB);
    QPolygonF rightTab;
    rightTab << QPointF(xOR, gT)
             << QPointF(xOR + kTabW, gT + tip)
             << QPointF(xOR + kTabW, gB - tip)
             << QPointF(xOR, gB);
    p.setPen(QPen(kEdge, 1.0));
    p.setBrush(kFill);
    p.drawPolygon(leftTab);
    p.drawPolygon(rightTab);

    // 3. Inner 100%-region handles — narrow solid vertical bars marking the
    //    innerStart/innerEnd edges, each inset toward the range centre so it never
    //    overlaps the outer tabs (step 2). Their centres come from handleCenters()
    //    — the same geometry hit-testing uses — so when the 100% region narrows the
    //    pair is recentred and held kBarGap apart, never touching or clipping, yet
    //    both stay visible and grabbable. Crisp (no AA) so the 1px outline stays sharp.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(kEdge, 1.0));
    p.setBrush(kFill);
    auto drawInnerBar = [&](double lo, double hi) {
        const double a = std::round(lo) + 0.5;
        const double b = std::round(hi) + 0.5;
        if (b > a)
            p.drawRect(QRectF(a, gT + 0.5, b - a, gH - 1.0));
    };
    {
        const std::array<double, 4> cx = handleCenters();
        // Draw at the resolved centres, never letting a bar slide under an outer tab.
        drawInnerBar(std::max(cx[1] - kBarW / 2.0, xOL), cx[1] + kBarW / 2.0);
        drawInnerBar(cx[2] - kBarW / 2.0, std::min(cx[2] + kBarW / 2.0, xOR));
    }

    // 4. Dark boundary lines marking the falloff extremes on top of everything,
    //    so the selected range reads clearly over any gradient colour. (The inner
    //    boundaries are already defined by the vertical bars above.)
    p.setPen(QPen(kLine, 1.0));
    for (double x : { xOL, xOR }) {
        const double xx = std::round(x) + 0.5;
        p.drawLine(QPointF(xx, gT), QPointF(xx, gB));
    }
}

void HueRangeBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_active) {
        QWidget::mousePressEvent(e);
        return;
    }
    const int idx = hitTestHandle(e->position());
    if (idx < 0) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_dragHandle = idx;
    m_dragStartX = e->position().x();
    emit handlePressed(idx);
    e->accept();
}

void HueRangeBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragHandle < 0) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    const double stripW = std::max(1.0, topStripRect().width());
    const float deltaDeg = static_cast<float>((e->position().x() - m_dragStartX) / stripW * 360.0);
    emit handleDragged(m_dragHandle, deltaDeg);
    e->accept();
}

void HueRangeBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_dragHandle < 0) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    m_dragHandle = -1;
    emit handleReleased();
    e->accept();
}

// ════════════════════════════════════════════════════════════════════════════
//  HueSaturationAdjustmentWidget
// ════════════════════════════════════════════════════════════════════════════

HueSaturationAdjustmentWidget::HueSaturationAdjustmentWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void HueSaturationAdjustmentWidget::setController(ImageController* ctrl)
{
    if (m_ctrl == ctrl)
        return;
    // Leaving the previous controller (switching documents/tabs): undo any live
    // bypass still applied to the bound node, disarm the eyedropper, drop the
    // documentChanged subscription, and detach so a later showNode() rebinds.
    if (m_ctrl) {
        if (m_flatIndex >= 0 && !m_previewOn)
            m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
        disconnect(m_ctrl, &ImageController::documentChanged,
                   this, &HueSaturationAdjustmentWidget::onDocumentChanged);
    }
    disarmEyedroppers();
    m_ctrl = ctrl;
    m_flatIndex = -1;
    m_inGesture = false;
    m_previewOn = true;
    if (ctrl) {
        connect(ctrl, &ImageController::documentChanged,
                this, &HueSaturationAdjustmentWidget::onDocumentChanged,
                Qt::UniqueConnection);
    }
}

void HueSaturationAdjustmentWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Header ──
    auto* header = new QHBoxLayout();
    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QPixmap(QStringLiteral(":/icons/hue-saturation-layer.png")));
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setScaledContents(true);
    m_maskThumb = new QLabel(this);
    m_maskThumb->setFixedSize(20, 20);
    m_maskThumb->setScaledContents(true);
    m_maskThumb->setVisible(false);
    m_titleLabel = new QLabel(tr("Hue/Saturation"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    header->addWidget(m_iconLabel);
    header->addWidget(m_maskThumb);
    header->addWidget(m_titleLabel);
    header->addStretch();
    root->addLayout(header);

    // ── Preset ──
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    m_presetCombo = new QComboBox(this);
    for (const auto& p : huesaturation::presets())
        m_presetCombo->addItem(p.name);
    m_presetCombo->addItem(huesaturation::kCustomPresetName());
    presetRow->addWidget(m_presetCombo, 1);
    root->addLayout(presetRow);

    // ── Range / channel ──
    auto* rangeRow = new QHBoxLayout();
    m_rangeCombo = new QComboBox(this);
    m_rangeCombo->addItem(tr("Master"));
    m_rangeCombo->addItem(tr("Reds"));
    m_rangeCombo->addItem(tr("Yellows"));
    m_rangeCombo->addItem(tr("Greens"));
    m_rangeCombo->addItem(tr("Cyans"));
    m_rangeCombo->addItem(tr("Blues"));
    m_rangeCombo->addItem(tr("Magentas"));
    rangeRow->addWidget(m_rangeCombo, 1);
    root->addLayout(rangeRow);

    // ── Hue / Saturation / Lightness controls ──
    struct Ctl { const char* label; int lo, hi; };
    const Ctl ctls[3] = {
        { "Hue:",        -180, 180 },
        { "Saturation:", -100, 100 },
        { "Lightness:",  -100, 100 },
    };
    for (int i = 0; i < 3; ++i) {
        auto* col = new QVBoxLayout();
        col->setSpacing(2);
        col->addWidget(new QLabel(tr(ctls[i].label), this));

        auto* sliderRow = new QHBoxLayout();
        sliderRow->setSpacing(8);

        auto* slider = new GradientSlider(this);
        slider->setRange(ctls[i].lo, ctls[i].hi);
        m_sliders[i] = slider;
        sliderRow->addWidget(slider, 1);

        col->addLayout(sliderRow);
        root->addLayout(col);

        connect(slider, &GradientSlider::valueChanged, this,
                [this, i](int v) { onSliderChanged(i, v); });
        connect(slider, &GradientSlider::sliderReleased, this,
                [this, i]() { onSliderReleased(i); });
        connect(slider, &GradientSlider::spinEdited, this,
                [this, i](int v) { onSpinEdited(i, v); });
    }

    // Initial gradient stops (base hue 0; refreshSliderBaseHue() updates 1 & 2).
    m_sliders[0]->setStops({
        {0.0,       QColor::fromHsv(0,   255, 255)},
        {1.0/6.0,   QColor::fromHsv(60,  255, 255)},
        {2.0/6.0,   QColor::fromHsv(120, 255, 255)},
        {3.0/6.0,   QColor::fromHsv(180, 255, 255)},
        {4.0/6.0,   QColor::fromHsv(240, 255, 255)},
        {5.0/6.0,   QColor::fromHsv(300, 255, 255)},
        {1.0,       QColor::fromHsv(359, 255, 255)},
    });
    m_sliders[1]->setStops({
        {0.0, QColor::fromHsl(0, 0,   128)},
        {1.0, QColor::fromHsl(0, 255, 128)},
    });
    m_sliders[2]->setStops({
        {0.0, QColor(0, 0, 0)},
        {0.5, QColor::fromHsl(0, 255, 128)},
        {1.0, QColor(255, 255, 255)},
    });

    // ── Colorize ──
    m_colorizeCheck = new AppCheckBox();
    m_colorizeCheck->setText(tr("Colorize"));
    m_colorizeCheck->setChecked(false);
    root->addWidget(m_colorizeCheck);

    // ── Eyedroppers (sample the adjustment's input colour on the canvas) ──
    auto* eyeRow = new QHBoxLayout();
    eyeRow->setSpacing(4);
    m_eyeMain = makeToolBtn(QStringLiteral(":/icons/hue-saturation-eyedropper.png"),
                            tr("Sample a colour and select/centre its range"));
    m_eyeAdd = makeToolBtn(QStringLiteral(":/icons/hue-saturation-eyedropper-add.png"),
                           tr("Add sampled hues to the active range (drag to add several)"));
    m_eyeRemove = makeToolBtn(QStringLiteral(":/icons/hue-saturation-eyedropper-remove.png"),
                              tr("Remove sampled hues from the active range (drag to remove several)"));
    eyeRow->addWidget(m_eyeMain);
    eyeRow->addWidget(m_eyeAdd);
    eyeRow->addWidget(m_eyeRemove);
    eyeRow->addStretch();
    root->addLayout(eyeRow);

    // ── Interactive range / spectrum bar ──
    m_rangeBar = new HueRangeBar(this);
    root->addWidget(m_rangeBar);

    root->addStretch();

    // ── Shared adjustment-layer bottom bar ──
    m_bar = new AdjustmentPanelBar(this);
    root->addWidget(m_bar);

    // ── Wiring ──
    connect(m_presetCombo, qOverload<int>(&QComboBox::activated),
            this, &HueSaturationAdjustmentWidget::onPresetActivated);
    connect(m_rangeCombo, qOverload<int>(&QComboBox::activated),
            this, &HueSaturationAdjustmentWidget::onRangeActivated);
    connect(m_colorizeCheck, &QCheckBox::toggled,
            this, &HueSaturationAdjustmentWidget::onColorizeToggled);

    connect(m_eyeMain, &QToolButton::toggled, this,
            [this](bool on) { onEyedropperToggled(1, on); });
    connect(m_eyeAdd, &QToolButton::toggled, this,
            [this](bool on) { onEyedropperToggled(2, on); });
    connect(m_eyeRemove, &QToolButton::toggled, this,
            [this](bool on) { onEyedropperToggled(3, on); });

    connect(m_rangeBar, &HueRangeBar::handlePressed,
            this, &HueSaturationAdjustmentWidget::onHandlePressed);
    connect(m_rangeBar, &HueRangeBar::handleDragged,
            this, &HueSaturationAdjustmentWidget::onHandleDragged);
    connect(m_rangeBar, &HueRangeBar::handleReleased,
            this, &HueSaturationAdjustmentWidget::onHandleReleased);

    connect(m_bar, &AdjustmentPanelBar::resetClicked,
            this, &HueSaturationAdjustmentWidget::onReset);
    connect(m_bar, &AdjustmentPanelBar::clipToggled,
            this, &HueSaturationAdjustmentWidget::onClipToggled);
    connect(m_bar, &AdjustmentPanelBar::previewToggled,
            this, &HueSaturationAdjustmentWidget::onPreviewToggled);
    connect(m_bar, &AdjustmentPanelBar::visibilityToggled,
            this, &HueSaturationAdjustmentWidget::onVisibilityToggled);
    connect(m_bar, &AdjustmentPanelBar::deleteClicked,
            this, &HueSaturationAdjustmentWidget::onDelete);
}

// ── Node binding ─────────────────────────────────────────────────────────────

huesaturation::Range HueSaturationAdjustmentWidget::currentRange() const
{
    if (m_data.colorize)
        return huesaturation::Range::Master;
    return static_cast<huesaturation::Range>(m_rangeCombo->currentIndex());
}

QVariantMap HueSaturationAdjustmentWidget::paramsFromData() const
{
    return m_data.toParams();
}

void HueSaturationAdjustmentWidget::showNode(int flatIndex)
{
    if (m_ctrl && m_flatIndex >= 0 && m_flatIndex != flatIndex && !m_previewOn)
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    // Switching the bound node cancels any armed eyedropper (it referred to the
    // previous adjustment's input).
    disarmEyedroppers();
    m_inGesture = false;
    m_previewOn = true;
    m_flatIndex = flatIndex;
    reloadFromNode();
}

void HueSaturationAdjustmentWidget::reloadFromNode()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer())
        return;

    m_loading = true;
    m_data = huesaturation::HueSaturationData::fromParams(node->adjustment->params);

    m_colorizeCheck->blockSignals(true);
    m_colorizeCheck->setChecked(m_data.colorize);
    m_colorizeCheck->blockSignals(false);

    m_rangeCombo->blockSignals(true);
    m_rangeCombo->setCurrentIndex(static_cast<int>(m_data.activeRange));
    m_rangeCombo->blockSignals(false);

    syncPresetCombo();

    m_bar->setPreviewChecked(true);
    m_bar->setVisibilityChecked(node->visible);
    const bool single = node->parent
        && node->parent->type == LayerTreeNode::Type::Layer;
    m_bar->setClipChecked(single);

    refreshHeader();
    updateColorizeUiState();
    reloadControlsForRange();
    m_loading = false;
}

void HueSaturationAdjustmentWidget::refreshHeader()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node)
        return;
    m_titleLabel->setText(node->name.isEmpty() ? tr("Hue/Saturation") : node->name);

    if (node->layer && !node->layer->maskImage.isNull() && node->layer->maskVisible) {
        QImage thumb = node->layer->maskImage.scaled(
            20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_maskThumb->setPixmap(QPixmap::fromImage(thumb));
        m_maskThumb->setVisible(true);
    } else {
        m_maskThumb->setVisible(false);
    }
}

void HueSaturationAdjustmentWidget::syncPresetCombo()
{
    int idx = m_presetCombo->findText(m_data.presetName);
    if (idx < 0)
        idx = m_presetCombo->findText(huesaturation::kCustomPresetName());
    m_presetCombo->blockSignals(true);
    m_presetCombo->setCurrentIndex(std::max(0, idx));
    m_presetCombo->blockSignals(false);
}

void HueSaturationAdjustmentWidget::updateColorizeUiState()
{
    const bool colorize = m_data.colorize;
    // Colorize is global → the per-hue range selector and eyedroppers don't apply.
    m_rangeCombo->setEnabled(!colorize);
    m_eyeMain->setEnabled(!colorize);
    m_eyeAdd->setEnabled(!colorize);
    m_eyeRemove->setEnabled(!colorize);
    if (colorize) {
        disarmEyedroppers();
        m_data.activeRange = huesaturation::Range::Master;
        m_rangeCombo->blockSignals(true);
        m_rangeCombo->setCurrentIndex(0);
        m_rangeCombo->blockSignals(false);
    }
    refreshRangeBar();
}

void HueSaturationAdjustmentWidget::reloadControlsForRange()
{
    const huesaturation::HslValues& v = m_data.range(currentRange());
    const int vals[3] = { v.hue, v.saturation, v.lightness };
    for (int i = 0; i < 3; ++i) {
        m_sliders[i]->setValue(vals[i]);
    }
    refreshSliderBaseHue();
    refreshRangeBar();
}

void HueSaturationAdjustmentWidget::refreshRangeBar()
{
    const huesaturation::Range r = currentRange();
    const bool band = !m_data.colorize && r != huesaturation::Range::Master;
    m_rangeBar->setActive(band);
    if (band) {
        const huesaturation::HueRange& b = m_data.band(r);
        m_rangeBar->setEdges(b.outerStart, b.innerStart, b.innerEnd, b.outerEnd);
    }
    const huesaturation::HslValues& v = m_data.range(r);
    if (m_data.colorize)
        m_rangeBar->setAdjustment(0, 0, 0);
    else
        m_rangeBar->setAdjustment(v.hue, v.saturation, v.lightness);
}

void HueSaturationAdjustmentWidget::refreshSliderBaseHue()
{
    int baseHue = 0;
    if (m_data.colorize) {
        baseHue = m_data.range(huesaturation::Range::Master).hue;
    } else {
        const huesaturation::Range r = currentRange();
        if (r != huesaturation::Range::Master)
            baseHue = static_cast<int>(wrap360f(m_data.band(r).centerHue));
    }
    m_sliders[1]->setStops({
        {0.0, QColor::fromHsl(baseHue, 0,   128)},
        {1.0, QColor::fromHsl(baseHue, 255, 128)},
    });
    m_sliders[2]->setStops({
        {0.0, QColor(0, 0, 0)},
        {0.5, QColor::fromHsl(baseHue, 255, 128)},
        {1.0, QColor(255, 255, 255)},
    });
}

// ── Param plumbing ───────────────────────────────────────────────────────────

void HueSaturationAdjustmentWidget::applyLive()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    if (m_previewOn) {
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    } else {
        huesaturation::HueSaturationData identity;   // no-op bypass
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, identity.toParams());
    }
}

void HueSaturationAdjustmentWidget::beginGesture()
{
    if (m_inGesture)
        return;
    m_gestureBefore = paramsFromData();
    m_inGesture = true;
    // Preview the drag on the GPU per-layer compositor (cheap) until commit.
    if (m_ctrl)
        m_ctrl->setAdjustmentLiveEdit(true);
}

void HueSaturationAdjustmentWidget::commitGesture(const QString& label)
{
    if (!m_inGesture)
        return;
    m_inGesture = false;
    // Commit while still in live-edit mode so the imageChanged it emits doesn't
    // trigger a full GPU re-upload (adjustment commits change no pixels), then
    // drop live-edit so the next frame builds the final projection off-thread.
    if (m_ctrl && m_flatIndex >= 0)
        m_ctrl->commitAdjustmentParams(m_flatIndex, m_gestureBefore,
                                       paramsFromData(), label);
    if (m_ctrl)
        m_ctrl->setAdjustmentLiveEdit(false);
}

void HueSaturationAdjustmentWidget::applyAndCommit(const QString& label)
{
    applyLive();
    commitGesture(label);
}

void HueSaturationAdjustmentWidget::markCustomPreset()
{
    if (m_data.presetName == huesaturation::kCustomPresetName())
        return;
    m_data.presetName = huesaturation::kCustomPresetName();
    syncPresetCombo();
}

void HueSaturationAdjustmentWidget::setHslValue(int which, int value)
{
    huesaturation::HslValues& v = m_data.range(currentRange());
    if (which == 0)
        v.hue = value;
    else if (which == 1)
        v.saturation = value;
    else
        v.lightness = value;
}

// ── Eyedropper sampling ──────────────────────────────────────────────────────

QImage HueSaturationAdjustmentWidget::captureInputComposite()
{
    if (!m_ctrl || !m_ctrl->document() || m_flatIndex < 0)
        return {};
    Document* doc = m_ctrl->document();
    auto* node = doc->nodeAt(m_flatIndex);
    if (!node)
        return {};

    // Bypass this adjustment so we read the colour that ENTERS it, never its
    // own output (that would make repeated clicks accumulate). For a root-level
    // (stack) adjustment we also hide every node above it, so adjustments/layers
    // stacked on top don't tint the sample. Clipped adjustments keep the simpler
    // bypass (only this node) so the clip target stays the input — splitting the
    // clipped input out of the parent layer would need its own compositor path
    // (documented limitation). Synchronous throwaway image; compositionGeneration
    // is untouched so there is no on-screen flicker.
    std::vector<LayerTreeNode*> hidden;
    auto hide = [&](LayerTreeNode* n) {
        if (n && n->visible) {
            n->visible = false;
            hidden.push_back(n);
        }
    };
    hide(node);
    if (!node->parent) {
        const auto flat = doc->flatten();
        for (int i = 0; i < m_flatIndex && i < static_cast<int>(flat.size()); ++i)
            if (flat[i] && !flat[i]->parent)
                hide(flat[i]);
    }
    node->invalidateEffects();
    QImage input = compositeImage(doc);
    for (auto* n : hidden)
        n->visible = true;
    node->invalidateEffects();
    return input;
}

bool HueSaturationAdjustmentWidget::sampleHue(const QImage& input,
                                              const QPointF& docPos,
                                              float& hueOut) const
{
    if (input.isNull()
        || docPos.x() < 0 || docPos.y() < 0
        || docPos.x() >= input.width() || docPos.y() >= input.height())
        return false;
    const int x = std::clamp(static_cast<int>(std::lround(docPos.x())), 0, input.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(docPos.y())), 0, input.height() - 1);
    const QColor c = input.pixelColor(x, y);
    if (c.alpha() == 0)
        return false;
    const int h = c.hue();   // 0..359, or -1 for achromatic (gray)
    if (h < 0)
        return false;        // gray pixel: no hue to select/refine
    hueOut = static_cast<float>(h);
    return true;
}

void HueSaturationAdjustmentWidget::ensureBandActive(float hueDeg)
{
    if (currentRange() != huesaturation::Range::Master)
        return;
    m_data.activeRange = m_data.nearestBand(hueDeg);
    m_rangeCombo->blockSignals(true);
    m_rangeCombo->setCurrentIndex(static_cast<int>(m_data.activeRange));
    m_rangeCombo->blockSignals(false);
}

// ── Callbacks ────────────────────────────────────────────────────────────────

void HueSaturationAdjustmentWidget::onPresetActivated(int index)
{
    if (m_loading)
        return;
    const QString name = m_presetCombo->itemText(index);
    // Selecting "Custom" by hand keeps the current values.
    if (name == huesaturation::kCustomPresetName())
        return;

    disarmEyedroppers();
    beginGesture();
    const huesaturation::Range prevActive = m_data.activeRange;
    m_data = huesaturation::presetData(name);
    // Keep the user's current band selection unless the preset is colorize.
    if (!m_data.colorize)
        m_data.activeRange = prevActive;

    m_loading = true;
    m_colorizeCheck->blockSignals(true);
    m_colorizeCheck->setChecked(m_data.colorize);
    m_colorizeCheck->blockSignals(false);
    updateColorizeUiState();
    reloadControlsForRange();
    m_loading = false;

    applyAndCommit(tr("Hue/Saturation Preset"));
}

void HueSaturationAdjustmentWidget::onRangeActivated(int index)
{
    if (m_loading)
        return;
    // Switching range only changes which values are displayed — no undo, no
    // composite change (matches the Color Balance tone combo).
    m_data.activeRange = static_cast<huesaturation::Range>(index);
    reloadControlsForRange();
}

void HueSaturationAdjustmentWidget::onSliderChanged(int which, int value)
{
    if (m_loading)
        return;
    beginGesture();
    setHslValue(which, value);
    markCustomPreset();

    if (which == 0)
        refreshSliderBaseHue();
    refreshRangeBar();

    if (m_sliders[which]->isSliderDown())
        applyLive();
    else
        applyAndCommit(tr("Hue/Saturation"));
}

void HueSaturationAdjustmentWidget::onSliderReleased(int)
{
    if (m_loading)
        return;
    commitGesture(tr("Hue/Saturation"));
}

void HueSaturationAdjustmentWidget::onSpinEdited(int which, int value)
{
    if (m_loading)
        return;
    const int lo = which == 0 ? -180 : -100;
    const int hi = which == 0 ? 180 : 100;
    value = std::clamp(value, lo, hi);
    beginGesture();
    setHslValue(which, value);
    markCustomPreset();

    m_sliders[which]->setValue(value);
    if (which == 0)
        refreshSliderBaseHue();
    refreshRangeBar();

    applyAndCommit(tr("Hue/Saturation"));
}

void HueSaturationAdjustmentWidget::onColorizeToggled(bool on)
{
    if (m_loading)
        return;
    beginGesture();
    m_data.colorize = on;
    markCustomPreset();
    m_loading = true;
    updateColorizeUiState();
    reloadControlsForRange();
    m_loading = false;
    applyAndCommit(on ? tr("Colorize On") : tr("Colorize Off"));
}

void HueSaturationAdjustmentWidget::onReset(bool shiftHeld)
{
    if (m_loading || m_flatIndex < 0)
        return;
    disarmEyedroppers();
    beginGesture();
    if (shiftHeld || m_data.colorize) {
        // Full reset: every range to neutral, colorize off, Default/Master.
        m_data.reset();
    } else {
        m_data.resetRange(currentRange());
        m_data.presetName = huesaturation::kCustomPresetName();
    }
    m_loading = true;
    m_colorizeCheck->blockSignals(true);
    m_colorizeCheck->setChecked(m_data.colorize);
    m_colorizeCheck->blockSignals(false);
    m_rangeCombo->blockSignals(true);
    m_rangeCombo->setCurrentIndex(static_cast<int>(m_data.activeRange));
    m_rangeCombo->blockSignals(false);
    syncPresetCombo();
    updateColorizeUiState();
    reloadControlsForRange();
    m_loading = false;
    applyAndCommit((shiftHeld || m_data.isIdentity()) ? tr("Reset Hue/Saturation")
                                                      : tr("Reset Range"));
}

void HueSaturationAdjustmentWidget::onClipToggled(bool single)
{
    if (m_loading || !m_ctrl || m_flatIndex < 0)
        return;
    auto* doc = m_ctrl->document();
    if (!doc)
        return;
    auto* node = doc->nodeAt(m_flatIndex);
    if (!node)
        return;
    const bool isSingle = node->parent
        && node->parent->type == LayerTreeNode::Type::Layer;
    if (single == isSingle)
        return;

    if (single) {
        auto flat = doc->flatten();
        int target = -1;
        for (int i = m_flatIndex + 1; i < static_cast<int>(flat.size()); ++i) {
            if (flat[i] && flat[i]->type == LayerTreeNode::Type::Layer) {
                target = i;
                break;
            }
        }
        if (target < 0) {
            m_bar->setClipChecked(false);   // no layer below — revert
            return;
        }
        m_ctrl->moveAdjustmentToLayer(m_flatIndex, target);
    } else {
        m_ctrl->moveAdjustmentToStack(m_flatIndex, m_flatIndex);
    }
}

void HueSaturationAdjustmentWidget::onPreviewToggled(bool on)
{
    if (m_loading)
        return;
    m_previewOn = on;
    applyLive();   // bypass (identity) when off, real adjustment when on
}

void HueSaturationAdjustmentWidget::onVisibilityToggled(bool visible)
{
    if (m_loading || !m_ctrl || m_flatIndex < 0)
        return;
    m_ctrl->setNodeVisibility(m_flatIndex, visible);
}

void HueSaturationAdjustmentWidget::onDelete()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    disarmEyedroppers();
    const int idx = m_flatIndex;
    m_flatIndex = -1;   // detach before the node disappears
    m_ctrl->removeNode(idx);
}

void HueSaturationAdjustmentWidget::onDocumentChanged()
{
    if (!isVisible() || m_inGesture || m_flatIndex < 0)
        return;
    auto* node = m_ctrl && m_ctrl->document()
                     ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer()
        || node->adjustment->type != QLatin1String("huesaturation"))
        return; // node went away/changed — MainWindow swaps the view on select
    reloadFromNode();
}

// ── Eyedropper UI + canvas callbacks ─────────────────────────────────────────

void HueSaturationAdjustmentWidget::disarmEyedroppers()
{
    bool wasArmed = false;
    for (auto* b : { m_eyeMain, m_eyeAdd, m_eyeRemove }) {
        if (!b)
            continue;
        if (b->isChecked())
            wasArmed = true;
        b->blockSignals(true);
        b->setChecked(false);
        b->blockSignals(false);
    }
    if (wasArmed)
        emit requestImagePick(0);
}

void HueSaturationAdjustmentWidget::onEyedropperToggled(int mode, bool on)
{
    if (m_loading)
        return;
    QToolButton* btns[3] = { m_eyeMain, m_eyeAdd, m_eyeRemove };
    if (on) {
        for (int i = 0; i < 3; ++i) {
            if (i + 1 != mode && btns[i]->isChecked()) {
                btns[i]->blockSignals(true);
                btns[i]->setChecked(false);
                btns[i]->blockSignals(false);
            }
        }
        emit requestImagePick(mode);
    } else {
        emit requestImagePick(0);   // disarm
    }
}

void HueSaturationAdjustmentWidget::onPickCancelled()
{
    // The canvas already disarmed itself (tool switch / Esc); just reflect it.
    for (auto* b : { m_eyeMain, m_eyeAdd, m_eyeRemove }) {
        b->blockSignals(true);
        b->setChecked(false);
        b->blockSignals(false);
    }
}

void HueSaturationAdjustmentWidget::onPickClicked(const QPointF& docPos, int /*mode*/)
{
    if (m_loading || m_flatIndex < 0 || m_data.colorize)
        return;
    const QImage input = captureInputComposite();
    float hue = 0.0f;
    if (!sampleHue(input, docPos, hue))
        return;   // transparent / gray — nothing to select

    beginGesture();
    ensureBandActive(hue);
    if (currentRange() == huesaturation::Range::Master) {
        // nearestBand always returns a band, so this is unreachable; bail safely.
        commitGesture(tr("Hue/Saturation Pick"));
        return;
    }
    huesaturation::HueRange& b = m_data.band(currentRange());
    b.recenter(hue);
    b.sampledColors.push_back(static_cast<int>(hue));
    markCustomPreset();

    m_loading = true;
    reloadControlsForRange();
    m_loading = false;
    applyAndCommit(tr("Hue/Saturation Pick"));
}

void HueSaturationAdjustmentWidget::onPickDragBegan(const QPointF& docPos, int mode)
{
    if (m_loading || m_flatIndex < 0 || m_data.colorize)
        return;
    // Snapshot the bypassed INPUT composite once; the whole drag samples it so
    // dragging never recomposites per move and never reads its own preview.
    m_eyeInputCache = captureInputComposite();
    onPickDragMoved(docPos);   // first sample (also begins the gesture lazily)
    Q_UNUSED(mode);
}

void HueSaturationAdjustmentWidget::onPickDragMoved(const QPointF& docPos)
{
    if (m_flatIndex < 0 || m_data.colorize || m_eyeInputCache.isNull())
        return;
    float hue = 0.0f;
    if (!sampleHue(m_eyeInputCache, docPos, hue))
        return;   // transparent / gray — skip this sample

    if (!m_inGesture) {
        beginGesture();         // begin lazily, only on the first valid sample
        ensureBandActive(hue);
    }
    if (currentRange() == huesaturation::Range::Master)
        return;

    huesaturation::HueRange& b = m_data.band(currentRange());
    if (m_eyeAdd->isChecked())
        b.expandToInclude(hue);
    else
        b.contractToExclude(hue);
    markCustomPreset();

    m_loading = true;
    reloadControlsForRange();
    m_loading = false;
    applyLive();
}

void HueSaturationAdjustmentWidget::onPickDragEnded()
{
    m_eyeInputCache = QImage();
    if (!m_inGesture)
        return;
    commitGesture(m_eyeAdd->isChecked() ? tr("Add to Range")
                                        : tr("Subtract from Range"));
}

// ── Range-handle drag ────────────────────────────────────────────────────────

void HueSaturationAdjustmentWidget::onHandlePressed(int /*idx*/)
{
    if (m_loading || currentRange() == huesaturation::Range::Master)
        return;
    const huesaturation::HueRange& b = m_data.band(currentRange());
    m_handleDragBase[0] = b.outerStart;
    m_handleDragBase[1] = b.innerStart;
    m_handleDragBase[2] = b.innerEnd;
    m_handleDragBase[3] = b.outerEnd;
    beginGesture();
}

void HueSaturationAdjustmentWidget::onHandleDragged(int idx, float deltaDeg)
{
    if (m_loading || currentRange() == huesaturation::Range::Master)
        return;
    huesaturation::HueRange& b = m_data.band(currentRange());
    const float v = m_handleDragBase[idx] + deltaDeg;
    // Keep the invariant outerStart ≤ innerStart ≤ innerEnd ≤ outerEnd by
    // clamping the dragged edge against its current neighbours.
    switch (idx) {
    case 0: b.outerStart = std::min(v, b.innerStart); break;
    case 1: b.innerStart = std::clamp(v, b.outerStart, b.innerEnd); break;
    case 2: b.innerEnd   = std::clamp(v, b.innerStart, b.outerEnd); break;
    case 3: b.outerEnd   = std::max(v, b.innerEnd); break;
    default: break;
    }
    b.centerHue = wrap360f(0.5f * (b.innerStart + b.innerEnd));
    b.supportsWrap = b.outerStart < 0.0f || b.outerEnd > 360.0f;
    markCustomPreset();
    refreshRangeBar();
    refreshSliderBaseHue();
    applyLive();
}

void HueSaturationAdjustmentWidget::onHandleReleased()
{
    if (!m_inGesture)
        return;
    commitGesture(tr("Edit Hue Range"));
}

void HueSaturationAdjustmentWidget::hideEvent(QHideEvent* e)
{
    disarmEyedroppers();
    if (m_ctrl && m_flatIndex >= 0 && !m_previewOn) {
        m_previewOn = true;
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    }
    QWidget::hideEvent(e);
}
