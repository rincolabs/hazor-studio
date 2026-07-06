#include "CurvesEditorWidget.hpp"

#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "histogram/HistogramGenerator.hpp"
#include "io/ImageIO.hpp"
#include "ui/AdjustmentPanelBar.hpp"
#include "ui/IconUtils.hpp"

#include <QtConcurrent/QtConcurrent>
#include <memory>

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

// ════════════════════════════════════════════════════════════════════════════
//  CurveGraphWidget
// ════════════════════════════════════════════════════════════════════════════

CurveGraphWidget::CurveGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(180);
}

void CurveGraphWidget::setPoints(const std::vector<QPoint>& pts)
{
    m_points = curves::sanitize(pts);
    if (m_selected >= static_cast<int>(m_points.size()))
        m_selected = -1;
    update();
}

void CurveGraphWidget::setSelectedIndex(int idx)
{
    m_selected = (idx >= 0 && idx < static_cast<int>(m_points.size())) ? idx : -1;
    update();
}

void CurveGraphWidget::setChannelColor(const QColor& c)
{
    m_color = c;
    update();
}

void CurveGraphWidget::setHistogram(const std::array<int, 256>& bins)
{
    m_hist = bins;
    m_histMax = 0;
    for (int v : bins)
        m_histMax = std::max(m_histMax, v);
    m_hasHist = m_histMax > 0;
    update();
}

void CurveGraphWidget::clearHistogram()
{
    m_hasHist = false;
    m_histMax = 0;
    update();
}

QRect CurveGraphWidget::plotRect() const
{
    // Small inset so the 1px grid border (and the corner control points) aren't
    // flush against the very edge, but the grid still sits close to the container.
    const int m = 2;
    return rect().adjusted(m, m, -m, -m);
}

QPointF CurveGraphWidget::toWidget(const QPoint& p) const
{
    const QRect r = plotRect();
    const double x = r.left() + p.x() / 255.0 * r.width();
    const double y = r.bottom() - p.y() / 255.0 * r.height();
    return QPointF(x, y);
}

QPoint CurveGraphWidget::toCurve(const QPointF& wp) const
{
    const QRect r = plotRect();
    double fx = r.width() > 0 ? (wp.x() - r.left()) / r.width() : 0.0;
    double fy = r.height() > 0 ? (r.bottom() - wp.y()) / r.height() : 0.0;
    int cx = std::clamp(static_cast<int>(std::lround(fx * 255.0)), 0, 255);
    int cy = std::clamp(static_cast<int>(std::lround(fy * 255.0)), 0, 255);
    return QPoint(cx, cy);
}

int CurveGraphWidget::hitTest(const QPointF& wp) const
{
    const double radius = 8.0;
    int best = -1;
    double bestD = radius * radius;
    for (int i = 0; i < static_cast<int>(m_points.size()); ++i) {
        const QPointF c = toWidget(m_points[i]);
        const double dx = c.x() - wp.x();
        const double dy = c.y() - wp.y();
        const double d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

// void CurveGraphWidget::paintEvent(QPaintEvent*)
// {
//     QPainter p(this);
//     p.setRenderHint(QPainter::Antialiasing, true);
//     const QRect r = plotRect();

//     auto* t = ThemeManager::instance()->current();
//     // Background — uniform plot color across the whole widget (no dark band),
//     // so the only frame is the 1px grid border drawn below.
//     p.fillRect(rect(), t->colorBackgroundPrimary);
//     // p.fillRect(rect(), QColor(120, 120, 120, 110));

//     // Histogram backdrop
//     if (m_hasHist && m_histMax > 0) {
//         p.setPen(Qt::NoPen);
//         p.setBrush(QColor(120, 120, 120, 110));
//         // p.setBrush(t->colorBackgroundPrimary);
//         QPainterPath hp;
//         hp.moveTo(r.left(), r.bottom());
//         for (int i = 0; i < 256; ++i) {
//             const double x = r.left() + i / 255.0 * r.width();
//             const double h = (m_hist[i] / static_cast<double>(m_histMax)) * r.height();
//             hp.lineTo(x, r.bottom() - h);
//         }
//         hp.lineTo(r.right(), r.bottom());
//         hp.closeSubpath();
//         p.drawPath(hp);
//     }

//     // Grid (4×4)
//     // p.setPen(QColor(60, 60, 60));
//     p.setPen(QPen(t->colorBackgroundSecondary, 1));
//     for (int i = 1; i < 4; ++i) {
//         const int x = r.left() + r.width() * i / 4;
//         const int y = r.top() + r.height() * i / 4;
//         p.drawLine(x, r.top(), x, r.bottom());
//         p.drawLine(r.left(), y, r.right(), y);
//     }
//     // p.setPen(QColor(80, 80, 80));
//     p.setPen(QPen(t->colorBackgroundTertiary, 1));
//     p.drawRect(r);

//     // Baseline diagonal
//     p.setPen(QPen(QColor(70, 70, 70), 1, Qt::DashLine));
//     p.drawLine(toWidget(QPoint(0, 0)), toWidget(QPoint(255, 255)));

//     // Curve
//     std::array<unsigned char, 256> lut{};
//     curves::lutFromPoints(m_points, lut);
//     QPainterPath path;
//     for (int i = 0; i < 256; ++i) {
//         const QPointF wp = toWidget(QPoint(i, lut[i]));
//         if (i == 0)
//             path.moveTo(wp);
//         else
//             path.lineTo(wp);
//     }
//     p.setPen(QPen(m_color, 2));
//     p.drawPath(path);

//     // Control points — hidden for dense freehand/smoothed curves (Pencil mode
//     // or many points) where individual dots would be noise.
//     const bool showDots = m_mode == Mode::Points && m_points.size() <= 48;
//     if (showDots) {
//         for (int i = 0; i < static_cast<int>(m_points.size()); ++i) {
//             const QPointF c = toWidget(m_points[i]);
//             const bool sel = (i == m_selected);
//             p.setPen(QPen(QColor(20, 20, 20), 1));
//             p.setBrush(sel ? QColor(255, 255, 255) : m_color);
//             const double rad = sel ? 4.5 : 3.5;
//             p.drawEllipse(c, rad, rad);
//         }
//     }
// }

void CurveGraphWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = plotRect();
    auto* t = ThemeManager::instance()->current();

    // Background
    p.fillRect(rect(), t->colorBackgroundSecondary);

    // Histogram backdrop
    if (m_hasHist && m_histMax > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(120, 120, 120, 110));
        p.setBrush(QBrush(t->colorHistogramBackdrop));

        QPainterPath hp;
        hp.moveTo(r.left(), r.bottom());

        for (int i = 0; i < 256; ++i) {
            const double x =
                r.left() + (static_cast<double>(i) / 255.0) * r.width();

            const double h =
                (m_hist[i] / static_cast<double>(m_histMax)) * r.height();

            hp.lineTo(x, r.bottom() - h);
        }

        hp.lineTo(r.right(), r.bottom());
        hp.closeSubpath();

        p.drawPath(hp);
    }

    // Grid (4x4)
    p.setPen(QPen(t->colorBorder, 1));
    p.setBrush(Qt::NoBrush);

    int gridSize = 8;
    for (int i = 1; i < gridSize; ++i) {
        const int x = r.left() + (r.width() * i) / gridSize;
        const int y = r.top() + (r.height() * i) / gridSize;

        p.drawLine(x, r.top(), x, r.bottom());
        p.drawLine(r.left(), y, r.right(), y);
    }

    // Border
    p.setPen(QPen(t->colorBackgroundPrimary, 1));
    p.drawRect(r);

    // Baseline diagonal
    p.setPen(QPen(QColor(70, 70, 70), 1, Qt::DashLine));
    p.drawLine(
        toWidget(QPoint(0, 0)),
        toWidget(QPoint(255, 255)));

    // Curve
    std::array<unsigned char, 256> lut{};
    curves::lutFromPoints(m_points, lut);

    QPainterPath path;
    for (int i = 0; i < 256; ++i) {
        const QPointF wp = toWidget(QPoint(i, lut[i]));

        if (i == 0)
            path.moveTo(wp);
        else
            path.lineTo(wp);
    }

    p.setPen(QPen(m_color, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Control points
    const bool showDots =
        (m_mode == Mode::Points) &&
        (m_points.size() <= 48);

    if (showDots) {
        for (int i = 0; i < static_cast<int>(m_points.size()); ++i) {
            const QPointF c = toWidget(m_points[i]);
            const bool selected = (i == m_selected);

            p.setPen(QPen(QColor(20, 20, 20), 1));
            p.setBrush(
                selected
                    ? QColor(255, 255, 255)
                    : m_color);

            const double radius = selected ? 4.5 : 3.5;
            p.drawEllipse(c, radius, radius);
        }
    }
}


void CurveGraphWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    setFocus();
    const QPointF wp = e->position();

    if (m_mode == Mode::Pencil) {
        // Freehand: seed the sample buffer from the current curve, then paint.
        std::array<unsigned char, 256> lut{};
        curves::lutFromPoints(m_points, lut);
        for (int i = 0; i < 256; ++i)
            m_pencilBuf[i] = lut[i];
        m_pencilLastX = -1;
        m_dragging = true;
        m_selected = -1;
        emit editBegan();
        pencilStroke(toCurve(wp));
        emit pointsChanged();
        update();
        return;
    }

    int hit = hitTest(wp);
    if (hit < 0) {
        // Add a new point at the clicked x, between its neighbors.
        const QPoint cp = toCurve(wp);
        emit editBegan();
        int insertAt = 0;
        while (insertAt < static_cast<int>(m_points.size())
               && m_points[insertAt].x() < cp.x())
            ++insertAt;
        // Avoid duplicate x with neighbors.
        if (insertAt > 0 && m_points[insertAt - 1].x() == cp.x())
            hit = insertAt - 1;
        else if (insertAt < static_cast<int>(m_points.size())
                 && m_points[insertAt].x() == cp.x())
            hit = insertAt;
        else {
            m_points.insert(m_points.begin() + insertAt, cp);
            hit = insertAt;
        }
        m_selected = hit;
        m_dragging = true;
        emit selectionChanged(m_selected);
        emit pointsChanged();
        update();
        return;
    }
    m_selected = hit;
    m_dragging = true;
    emit editBegan();
    emit selectionChanged(m_selected);
    update();
}

void CurveGraphWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_mode == Mode::Pencil) {
        if (!m_dragging)
            return;
        pencilStroke(toCurve(e->position()));
        emit pointsChanged();
        update();
        return;
    }
    if (!m_dragging || m_selected < 0)
        return;
    const int last = static_cast<int>(m_points.size()) - 1;
    QPoint cp = toCurve(e->position());

    if (m_selected == 0 || m_selected == last) {
        // Endpoints move only vertically (output); X is controlled by the
        // black/white input sliders.
        m_points[m_selected].setY(cp.y());
    } else {
        const int minX = m_points[m_selected - 1].x() + 1;
        const int maxX = m_points[m_selected + 1].x() - 1;
        cp.setX(std::clamp(cp.x(), minX, maxX));
        m_points[m_selected] = cp;
    }
    emit pointsChanged();
    update();
}

void CurveGraphWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_dragging) {
        m_dragging = false;
        emit editCommitted();
    }
}

void CurveGraphWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int hit = hitTest(e->position());
    const int last = static_cast<int>(m_points.size()) - 1;
    if (hit > 0 && hit < last) {
        m_dragging = false;
        emit editBegan();
        removePoint(hit);
        emit pointsChanged();
        emit selectionChanged(m_selected);
        emit editCommitted();
    }
}

void CurveGraphWidget::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        && m_selected > 0
        && m_selected < static_cast<int>(m_points.size()) - 1) {
        emit editBegan();
        removePoint(m_selected);
        emit pointsChanged();
        emit selectionChanged(m_selected);
        emit editCommitted();
        return;
    }
    QWidget::keyPressEvent(e);
}

void CurveGraphWidget::removePoint(int idx)
{
    const int last = static_cast<int>(m_points.size()) - 1;
    if (idx <= 0 || idx >= last)
        return; // never remove endpoints
    m_points.erase(m_points.begin() + idx);
    m_selected = -1;
    update();
}

void CurveGraphWidget::pencilStroke(const QPoint& cp)
{
    // Write the freehand output at cp.x, interpolating across any x skipped
    // since the last sample, then rebuild m_points as a dense (256-sample)
    // curve so the model/LUT reproduces the drawing exactly.
    const int x = std::clamp(cp.x(), 0, 255);
    const int y = std::clamp(cp.y(), 0, 255);
    if (m_pencilLastX < 0) {
        m_pencilBuf[x] = y;
    } else {
        const int x0 = m_pencilLastX;
        const int y0 = m_pencilBuf[x0];
        const int lo = std::min(x0, x);
        const int hi = std::max(x0, x);
        for (int i = lo; i <= hi; ++i) {
            const double t = (hi == lo) ? 0.0
                                        : double(i - x0) / double(x - x0);
            m_pencilBuf[i] = std::clamp(
                static_cast<int>(std::lround(y0 + (y - y0) * t)), 0, 255);
        }
    }
    m_pencilLastX = x;

    std::vector<QPoint> pts;
    pts.reserve(256);
    for (int i = 0; i < 256; ++i)
        pts.emplace_back(i, m_pencilBuf[i]);
    m_points = pts;
}

// ════════════════════════════════════════════════════════════════════════════
//  CurvesEditorWidget
// ════════════════════════════════════════════════════════════════════════════

namespace {

QColor channelLineColor(curves::Channel c)
{
    switch (c) {
    case curves::Channel::RGB:   return QColor(225, 225, 225);
    case curves::Channel::Red:   return QColor(230, 90, 90);
    case curves::Channel::Green: return QColor(90, 200, 90);
    case curves::Channel::Blue:  return QColor(90, 140, 230);
    case curves::Channel::Alpha: return QColor(180, 180, 180);
    }
    return QColor(225, 225, 225);
}

QToolButton* makeToolBtn(const QString& icon, const QString& tip, bool checkable)
{
    auto* b = new QToolButton();
    b->setIcon(makeIcon(icon));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setIconSize(QSize(24, 24));
    return b;
}

} // namespace

CurvesEditorWidget::CurvesEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    m_histWatcher = new QFutureWatcher<HistJob>(this);
    connect(m_histWatcher, &QFutureWatcher<HistJob>::finished,
            this, &CurvesEditorWidget::onHistogramReady);
}

void CurvesEditorWidget::setController(ImageController* ctrl)
{
    if (m_ctrl == ctrl)
        return;
    // Leaving the previous controller (switching documents/tabs): undo any live
    // bypass still applied to the bound node, drop its documentChanged
    // subscription, and detach so a later showNode() rebinds cleanly against the
    // new document instead of reading a stale flat index from the old one.
    if (m_ctrl) {
        if (m_flatIndex >= 0 && !m_previewOn)
            m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
        disconnect(m_ctrl, &ImageController::documentChanged,
                   this, &CurvesEditorWidget::onDocumentChanged);
    }
    m_ctrl = ctrl;
    m_flatIndex = -1;
    m_inGesture = false;
    m_previewOn = true;
    if (ctrl) {
        // Undo/redo (and other doc mutations) only emit documentChanged, not
        // layerChanged — reload here so the graph reflects the reverted state.
        connect(ctrl, &ImageController::documentChanged,
                this, &CurvesEditorWidget::onDocumentChanged,
                Qt::UniqueConnection);
    }
}

void CurvesEditorWidget::onDocumentChanged()
{
    if (!isVisible() || m_inGesture || m_flatIndex < 0)
        return;
    auto* node = m_ctrl && m_ctrl->document()
                     ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer()
        || node->adjustment->type != QLatin1String("curves"))
        return; // node went away/changed — MainWindow swaps the view on select
    reloadFromNode();
}

void CurvesEditorWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Header ──
    auto* header = new QHBoxLayout();
    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QPixmap(":/icons/curves-layer.png"));
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setScaledContents(true);
    m_maskThumb = new QLabel(this);
    m_maskThumb->setFixedSize(20, 20);
    m_maskThumb->setScaledContents(true);
    m_maskThumb->setVisible(false);
    m_titleLabel = new QLabel(tr("Curves"), this);
    m_titleLabel->setStyleSheet("font-weight: bold;");
    header->addWidget(m_iconLabel);
    header->addWidget(m_maskThumb);
    header->addWidget(m_titleLabel);
    header->addStretch();
    root->addLayout(header);

    // ── Preset ──
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem(tr("Custom"));
    for (const auto& p : curves::presets())
        m_presetCombo->addItem(p.id);
    presetRow->addWidget(m_presetCombo, 1);
    root->addLayout(presetRow);

    // ── Channel + Auto ──
    auto* chanRow = new QHBoxLayout();
    m_targetBtn = makeToolBtn(":/icons/curves-target.png",
                              tr("On-image adjustment: click the image to add a "
                                 "point at that tone"), true);
    m_channelCombo = new QComboBox(this);
    m_channelCombo->addItem(tr("RGB"));
    m_channelCombo->addItem(tr("Red"));
    m_channelCombo->addItem(tr("Green"));
    m_channelCombo->addItem(tr("Blue"));
    // Alpha is intentionally omitted: the pipeline does not curve alpha.
    m_autoBtn = new QPushButton(tr("Auto"), this);
    m_autoBtn->setToolTip(tr("Auto contrast from the image histogram"));
    chanRow->addWidget(m_targetBtn);
    chanRow->addWidget(m_channelCombo, 1);
    chanRow->addWidget(m_autoBtn);
    root->addLayout(chanRow);

    // ── Tool column (eyedroppers + modes) beside the graph ──
    auto* graphRow = new QHBoxLayout();
    auto* tools = new QVBoxLayout();
    tools->setSpacing(2);
    m_eyeBlack = makeToolBtn(":/icons/curves-eyedropper-black.png",
                             tr("Set black point: click a pixel in the image"), true);
    m_eyeGray = makeToolBtn(":/icons/curves-eyedropper-gray.png",
                            tr("Set gray point: click a neutral pixel"), true);
    m_eyeWhite = makeToolBtn(":/icons/curves-eyedropper-white.png",
                             tr("Set white point: click a pixel in the image"), true);
    m_editCurveBtn = makeToolBtn(":/icons/curves-edit-curve.png",
                                 tr("Edit points"), true);
    m_editCurveBtn->setChecked(true);
    m_pencilBtn = makeToolBtn(":/icons/curves-pencil.png",
                              tr("Draw the curve freehand"), true);
    m_smoothBtn = makeToolBtn(":/icons/curves-smooth.png",
                              tr("Smooth the current curve"), false);
    m_histWarnBtn = makeToolBtn(":/icons/curves-histogram-warning.png",
                                tr("Histogram out of date — click to recalculate"),
                                false);
    m_histWarnBtn->setVisible(false); // indicator: shown only when stale
    tools->addWidget(m_eyeBlack);
    tools->addWidget(m_eyeGray);
    tools->addWidget(m_eyeWhite);
    tools->addSpacing(6);
    tools->addWidget(m_editCurveBtn);
    tools->addWidget(m_pencilBtn);
    tools->addWidget(m_smoothBtn);
    tools->addStretch();
    tools->addWidget(m_histWarnBtn);
    graphRow->addLayout(tools);

    m_graph = new CurveGraphWidget(this);
    graphRow->addWidget(m_graph, 1);
    root->addLayout(graphRow, 1);

    // ── Black / white input sliders ──
    auto* sliderRow = new QHBoxLayout();
    m_blackSlider = new QSlider(Qt::Horizontal, this);
    m_blackSlider->setRange(0, 254);
    m_whiteSlider = new QSlider(Qt::Horizontal, this);
    m_whiteSlider->setRange(1, 255);
    sliderRow->addWidget(m_blackSlider, 1);
    sliderRow->addWidget(m_whiteSlider, 1);
    root->addLayout(sliderRow);

    // ── Input / Output ──
    auto* ioRow = new QHBoxLayout();
    ioRow->addWidget(new QLabel(tr("Input:"), this));
    m_inputSpin = new QSpinBox(this);
    m_inputSpin->setRange(0, 255);
    ioRow->addWidget(m_inputSpin);
    ioRow->addSpacing(10);
    ioRow->addWidget(new QLabel(tr("Output:"), this));
    m_outputSpin = new QSpinBox(this);
    m_outputSpin->setRange(0, 255);
    ioRow->addWidget(m_outputSpin);
    ioRow->addStretch();
    root->addLayout(ioRow);

    // ── Bottom toolbar (shared adjustment-layer bar) ──
    m_bar = new AdjustmentPanelBar(this);
    root->addWidget(m_bar);

    // ── Wiring ──
    connect(m_graph, &CurveGraphWidget::editBegan,
            this, &CurvesEditorWidget::onGraphEditBegan);
    connect(m_graph, &CurveGraphWidget::pointsChanged,
            this, &CurvesEditorWidget::onGraphPointsChanged);
    connect(m_graph, &CurveGraphWidget::editCommitted,
            this, &CurvesEditorWidget::onGraphEditCommitted);
    connect(m_graph, &CurveGraphWidget::selectionChanged,
            this, &CurvesEditorWidget::onGraphSelectionChanged);

    connect(m_presetCombo, qOverload<int>(&QComboBox::activated),
            this, &CurvesEditorWidget::onPresetActivated);
    connect(m_channelCombo, qOverload<int>(&QComboBox::activated),
            this, &CurvesEditorWidget::onChannelActivated);

    connect(m_blackSlider, &QSlider::valueChanged,
            this, &CurvesEditorWidget::onBlackSlider);
    connect(m_whiteSlider, &QSlider::valueChanged,
            this, &CurvesEditorWidget::onWhiteSlider);
    connect(m_blackSlider, &QSlider::sliderReleased,
            this, &CurvesEditorWidget::onSliderReleased);
    connect(m_whiteSlider, &QSlider::sliderReleased,
            this, &CurvesEditorWidget::onSliderReleased);

    connect(m_inputSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &CurvesEditorWidget::onInputEdited);
    connect(m_outputSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &CurvesEditorWidget::onOutputEdited);

    connect(m_autoBtn, &QPushButton::clicked, this, &CurvesEditorWidget::onAuto);
    connect(m_editCurveBtn, &QToolButton::clicked, this, &CurvesEditorWidget::onEditMode);
    connect(m_pencilBtn, &QToolButton::clicked, this, &CurvesEditorWidget::onPencilMode);
    connect(m_smoothBtn, &QToolButton::clicked, this, &CurvesEditorWidget::onSmooth);
    connect(m_histWarnBtn, &QToolButton::clicked, this, &CurvesEditorWidget::onHistRefresh);

    connect(m_eyeBlack, &QToolButton::toggled, this,
            [this](bool on) { onEyedropper(1, on); });
    connect(m_eyeGray, &QToolButton::toggled, this,
            [this](bool on) { onEyedropper(2, on); });
    connect(m_eyeWhite, &QToolButton::toggled, this,
            [this](bool on) { onEyedropper(3, on); });
    connect(m_targetBtn, &QToolButton::toggled, this,
            [this](bool on) { onEyedropper(4, on); });

    connect(m_bar, &AdjustmentPanelBar::resetClicked,
            this, &CurvesEditorWidget::onReset);
    connect(m_bar, &AdjustmentPanelBar::clipToggled,
            this, &CurvesEditorWidget::onClipToggled);
    connect(m_bar, &AdjustmentPanelBar::previewToggled,
            this, &CurvesEditorWidget::onPreviewToggled);
    connect(m_bar, &AdjustmentPanelBar::visibilityToggled,
            this, &CurvesEditorWidget::onVisibilityToggled);
    connect(m_bar, &AdjustmentPanelBar::deleteClicked,
            this, &CurvesEditorWidget::onDelete);
}

// ── Node binding ─────────────────────────────────────────────────────────────

curves::Channel CurvesEditorWidget::currentChannel() const
{
    return static_cast<curves::Channel>(m_channelCombo->currentIndex());
}

QVariantMap CurvesEditorWidget::paramsFromData() const
{
    return m_data.toParams();
}

void CurvesEditorWidget::showNode(int flatIndex)
{
    // Restore the previously bound node's real curve if we left it bypassed.
    if (m_ctrl && m_flatIndex >= 0 && m_flatIndex != flatIndex && !m_previewOn) {
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    }
    disarmPicks();
    m_inGesture = false;
    m_previewOn = true;
    m_flatIndex = flatIndex;
    reloadFromNode();
}

void CurvesEditorWidget::reloadFromNode()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer())
        return;

    m_loading = true;
    m_data = curves::CurvesData::fromParams(node->adjustment->params);

    // Preset combo
    int presetIdx = m_presetCombo->findText(m_data.preset);
    m_presetCombo->blockSignals(true);
    m_presetCombo->setCurrentIndex(presetIdx >= 0 ? presetIdx : 0);
    m_presetCombo->blockSignals(false);

    // Channel stays where the user left it; default RGB on first load.
    if (m_channelCombo->currentIndex() < 0)
        m_channelCombo->setCurrentIndex(0);

    m_bar->setPreviewChecked(true);
    m_bar->setVisibilityChecked(node->visible);

    const bool single = node->parent
        && node->parent->type == LayerTreeNode::Type::Layer;
    m_bar->setClipChecked(single);

    refreshHeader();
    refreshHistogram();
    updateChannelView();
    updateHistWarning();
    m_loading = false;
}

void CurvesEditorWidget::refreshHeader()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node)
        return;
    m_titleLabel->setText(node->name.isEmpty() ? tr("Curves") : node->name);

    if (node->layer && !node->layer->maskImage.isNull() && node->layer->maskVisible) {
        QImage thumb = node->layer->maskImage.scaled(
            20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_maskThumb->setPixmap(QPixmap::fromImage(thumb));
        m_maskThumb->setVisible(true);
    } else {
        m_maskThumb->setVisible(false);
    }
}

void CurvesEditorWidget::refreshHistogram()
{
    Document* doc = m_ctrl ? m_ctrl->document() : nullptr;
    if (!doc) {
        m_graph->clearHistogram();
        return;
    }
    // Recompute only when the bound node changed; the backdrop histogram stays
    // stable while the user edits curves (avoids a full composite per commit).
    if (m_histNodeIndex == m_flatIndex && m_hasHistogram)
        return;

    // The backdrop needs a full-document composite — far too heavy for the UI
    // thread on a large document (froze the panel when a Curves layer was
    // created/selected). Snapshot the tree copy-on-write (cheap, shares pixel
    // buffers; a later edit detaches them) and composite + bin off the UI thread.
    const unsigned long long token = ++m_histToken;
    const int nodeIndex = m_flatIndex;
    const unsigned long long generation = doc->compositionGeneration;

    auto snap = std::make_shared<Document>();
    snap->size = doc->size;
    snap->roots.reserve(doc->roots.size());
    for (const auto& r : doc->roots)
        if (r) snap->roots.push_back(r->shallowClone());

    auto future = QtConcurrent::run(
        [snap, token, nodeIndex, generation]() -> HistJob {
            HistJob job;
            job.token = token;
            job.nodeIndex = nodeIndex;
            job.generation = generation;
            QImage composite = compositeImage(snap.get());
            if (composite.isNull())
                return job;
            HistogramGenerator::Options opts;
            opts.autoDownsample = true;
            HistogramData hd = HistogramGenerator::generate(composite, nullptr, opts);
            job.r = hd.red;
            job.g = hd.green;
            job.b = hd.blue;
            job.lum = hd.luminance;
            job.valid = hd.valid;
            return job;
        });
    m_histWatcher->setFuture(future);
}

bool CurvesEditorWidget::computeHistogramSync()
{
    Document* doc = m_ctrl ? m_ctrl->document() : nullptr;
    if (!doc)
        return false;
    QImage composite = compositeImage(doc);
    if (composite.isNull())
        return false;
    HistogramGenerator::Options opts;
    opts.autoDownsample = true;
    HistogramData hd = HistogramGenerator::generate(composite, nullptr, opts);
    m_histR = hd.red;
    m_histG = hd.green;
    m_histB = hd.blue;
    m_histLum = hd.luminance;
    m_hasHistogram = hd.valid;
    m_histNodeIndex = m_flatIndex;
    m_histGeneration = doc->compositionGeneration;
    updateHistWarning();
    return m_hasHistogram;
}

void CurvesEditorWidget::onHistogramReady()
{
    const HistJob job = m_histWatcher->result();
    // Drop stale results: the user switched node / a newer request superseded it.
    if (job.token != m_histToken || job.nodeIndex != m_flatIndex)
        return;
    m_histR = job.r;
    m_histG = job.g;
    m_histB = job.b;
    m_histLum = job.lum;
    m_hasHistogram = job.valid;
    m_histNodeIndex = job.nodeIndex;
    m_histGeneration = job.generation;
    updateHistWarning();
    // The graph was showing an empty backdrop until now — refresh it.
    updateChannelView();
}

void CurvesEditorWidget::updateHistWarning()
{
    // The warning is purely an indicator: it appears when the composite changed
    // since the backdrop histogram was computed (clicking it recalculates).
    bool stale = false;
    if (m_ctrl && m_ctrl->document() && m_flatIndex >= 0 && m_hasHistogram)
        stale = m_ctrl->document()->compositionGeneration != m_histGeneration;
    if (m_histWarnBtn)
        m_histWarnBtn->setVisible(stale);
}

void CurvesEditorWidget::updateChannelView()
{
    const curves::Channel ch = currentChannel();
    const auto& pts = m_data.points(ch);

    m_graph->blockSignals(true);
    m_graph->setChannelColor(channelLineColor(ch));
    m_graph->setPoints(pts);
    if (m_hasHistogram) {
        switch (ch) {
        case curves::Channel::Red:   m_graph->setHistogram(m_histR); break;
        case curves::Channel::Green: m_graph->setHistogram(m_histG); break;
        case curves::Channel::Blue:  m_graph->setHistogram(m_histB); break;
        case curves::Channel::Alpha: m_graph->clearHistogram(); break;
        case curves::Channel::RGB:   m_graph->setHistogram(m_histLum); break;
        }
    } else {
        m_graph->clearHistogram();
    }
    // Default selection: the first endpoint.
    m_graph->setSelectedIndex(m_graph->selectedIndex() >= 0
                                  ? std::min(m_graph->selectedIndex(),
                                             static_cast<int>(pts.size()) - 1)
                                  : 0);
    m_graph->blockSignals(false);

    updateSliderRange();
    updateInputOutputFields();
}

void CurvesEditorWidget::updateSliderRange()
{
    const auto& pts = m_data.points(currentChannel());
    if (pts.size() < 2)
        return;
    const int blackX = pts.front().x();
    const int whiteX = pts.back().x();
    m_blackSlider->blockSignals(true);
    m_whiteSlider->blockSignals(true);
    m_blackSlider->setRange(0, whiteX - 1);
    m_whiteSlider->setRange(blackX + 1, 255);
    m_blackSlider->setValue(blackX);
    m_whiteSlider->setValue(whiteX);
    m_blackSlider->blockSignals(false);
    m_whiteSlider->blockSignals(false);
}

void CurvesEditorWidget::updateInputOutputFields()
{
    const int sel = m_graph->selectedIndex();
    const auto& pts = m_data.points(currentChannel());
    const bool valid = sel >= 0 && sel < static_cast<int>(pts.size());
    m_inputSpin->setEnabled(valid);
    m_outputSpin->setEnabled(valid);
    if (!valid)
        return;
    m_inputSpin->blockSignals(true);
    m_outputSpin->blockSignals(true);
    m_inputSpin->setValue(pts[sel].x());
    m_outputSpin->setValue(pts[sel].y());
    m_inputSpin->blockSignals(false);
    m_outputSpin->blockSignals(false);
}

// ── Param plumbing ───────────────────────────────────────────────────────────

void CurvesEditorWidget::applyLive()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    if (m_previewOn)
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    else {
        curves::CurvesData identity;          // all channels identity
        identity.preset = m_data.preset;
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, identity.toParams());
    }
    // The composite just changed → the cached backdrop histogram is now stale.
    updateHistWarning();
}

void CurvesEditorWidget::beginGesture()
{
    if (m_inGesture)
        return;
    m_gestureBefore = paramsFromData();
    m_inGesture = true;
    // Preview the drag on the GPU per-layer compositor (cheap) until commit.
    if (m_ctrl)
        m_ctrl->setAdjustmentLiveEdit(true);
}

void CurvesEditorWidget::commitGesture(const QString& label)
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

void CurvesEditorWidget::applyAndCommit(const QString& label)
{
    applyLive();
    commitGesture(label);
}

void CurvesEditorWidget::markCustomPreset()
{
    m_data.preset = QStringLiteral("Custom");
    m_presetCombo->blockSignals(true);
    m_presetCombo->setCurrentIndex(0);
    m_presetCombo->blockSignals(false);
}

// ── Graph callbacks ──────────────────────────────────────────────────────────

void CurvesEditorWidget::onGraphEditBegan()
{
    if (m_loading)
        return;
    if (!m_previewOn) {
        m_previewOn = true;
        m_bar->setPreviewChecked(true);
    }
    beginGesture();
}

void CurvesEditorWidget::onGraphPointsChanged()
{
    if (m_loading)
        return;
    m_data.points(currentChannel()) = m_graph->points();
    markCustomPreset();
    updateSliderRange();
    updateInputOutputFields();
    applyLive();
}

void CurvesEditorWidget::onGraphEditCommitted()
{
    if (m_loading)
        return;
    commitGesture(tr("Edit Curve"));
}

void CurvesEditorWidget::onGraphSelectionChanged(int)
{
    if (m_loading)
        return;
    updateInputOutputFields();
}

// ── Control callbacks ────────────────────────────────────────────────────────

void CurvesEditorWidget::onPresetActivated(int index)
{
    if (m_loading)
        return;
    if (index <= 0)
        return; // "Custom": no curve change
    const QString id = m_presetCombo->itemText(index);
    const curves::Preset* preset = curves::findPreset(id);
    if (!preset)
        return;

    beginGesture();
    m_data.resetAll();
    m_data.points(curves::Channel::RGB) = curves::sanitize(preset->rgb);
    m_data.preset = id;
    // Show the master channel where the preset lives.
    m_channelCombo->blockSignals(true);
    m_channelCombo->setCurrentIndex(0);
    m_channelCombo->blockSignals(false);
    m_graph->setSelectedIndex(0);
    updateChannelView();
    applyAndCommit(tr("Curves Preset: %1").arg(id));
}

void CurvesEditorWidget::onChannelActivated(int)
{
    if (m_loading)
        return;
    m_graph->setSelectedIndex(0);
    updateChannelView();
}

void CurvesEditorWidget::onBlackSlider(int value)
{
    if (m_loading)
        return;
    auto& pts = m_data.points(currentChannel());
    if (pts.size() < 2 || value >= pts.back().x())
        return;
    beginGesture();
    pts.front().setX(value);
    markCustomPreset();
    m_graph->blockSignals(true);
    m_graph->setPoints(pts);
    m_graph->setSelectedIndex(0);
    m_graph->blockSignals(false);
    updateInputOutputFields();
    applyLive();
}

void CurvesEditorWidget::onWhiteSlider(int value)
{
    if (m_loading)
        return;
    auto& pts = m_data.points(currentChannel());
    if (pts.size() < 2 || value <= pts.front().x())
        return;
    beginGesture();
    pts.back().setX(value);
    markCustomPreset();
    m_graph->blockSignals(true);
    m_graph->setPoints(pts);
    m_graph->setSelectedIndex(static_cast<int>(pts.size()) - 1);
    m_graph->blockSignals(false);
    updateInputOutputFields();
    applyLive();
}

void CurvesEditorWidget::onSliderReleased()
{
    if (m_loading)
        return;
    updateSliderRange();
    commitGesture(tr("Adjust Curve Levels"));
}

void CurvesEditorWidget::onInputEdited(int value)
{
    if (m_loading)
        return;
    const int sel = m_graph->selectedIndex();
    auto& pts = m_data.points(currentChannel());
    if (sel < 0 || sel >= static_cast<int>(pts.size()))
        return;
    const int last = static_cast<int>(pts.size()) - 1;
    int minX = (sel == 0) ? 0 : pts[sel - 1].x() + 1;
    int maxX = (sel == last) ? 255 : pts[sel + 1].x() - 1;
    if (sel == 0) maxX = pts[last].x() - 1;
    if (sel == last) minX = pts.front().x() + 1;
    value = std::clamp(value, minX, maxX);

    beginGesture();
    pts[sel].setX(value);
    markCustomPreset();
    m_graph->blockSignals(true);
    m_graph->setPoints(pts);
    m_graph->setSelectedIndex(sel);
    m_graph->blockSignals(false);
    updateSliderRange();
    applyAndCommit(tr("Set Curve Input"));
}

void CurvesEditorWidget::onOutputEdited(int value)
{
    if (m_loading)
        return;
    const int sel = m_graph->selectedIndex();
    auto& pts = m_data.points(currentChannel());
    if (sel < 0 || sel >= static_cast<int>(pts.size()))
        return;
    value = std::clamp(value, 0, 255);

    beginGesture();
    pts[sel].setY(value);
    markCustomPreset();
    m_graph->blockSignals(true);
    m_graph->setPoints(pts);
    m_graph->setSelectedIndex(sel);
    m_graph->blockSignals(false);
    applyAndCommit(tr("Set Curve Output"));
}

void CurvesEditorWidget::onReset(bool shiftHeld)
{
    if (m_loading)
        return;
    beginGesture();
    if (shiftHeld)
        m_data.resetAll();
    else
        m_data.resetChannel(currentChannel());
    m_data.preset = m_data.isIdentity() ? QStringLiteral("Default")
                                        : QStringLiteral("Custom");
    int presetIdx = m_presetCombo->findText(m_data.preset);
    m_presetCombo->blockSignals(true);
    m_presetCombo->setCurrentIndex(presetIdx >= 0 ? presetIdx : 0);
    m_presetCombo->blockSignals(false);
    m_graph->setSelectedIndex(0);
    updateChannelView();
    applyAndCommit(shiftHeld ? tr("Reset All Curves") : tr("Reset Curve"));
}

void CurvesEditorWidget::onClipToggled(bool single)
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
        // Find the nearest pixel/text/shape layer below to clip onto.
        auto flat = doc->flatten();
        int target = -1;
        for (int i = m_flatIndex + 1; i < static_cast<int>(flat.size()); ++i) {
            if (flat[i] && flat[i]->type == LayerTreeNode::Type::Layer) {
                target = i;
                break;
            }
        }
        if (target < 0) {
            // No layer below — revert the toggle.
            m_bar->setClipChecked(false);
            return;
        }
        m_ctrl->moveAdjustmentToLayer(m_flatIndex, target);
    } else {
        m_ctrl->moveAdjustmentToStack(m_flatIndex, m_flatIndex);
    }
    // Active index follows the moved node; MainWindow re-shows the editor.
}

void CurvesEditorWidget::onPreviewToggled(bool on)
{
    if (m_loading)
        return;
    m_previewOn = on;
    applyLive(); // bypass (identity) when off, real curve when on
}

void CurvesEditorWidget::onVisibilityToggled(bool visible)
{
    if (m_loading || !m_ctrl || m_flatIndex < 0)
        return;
    m_ctrl->setNodeVisibility(m_flatIndex, visible);
}

void CurvesEditorWidget::onDelete()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    const int idx = m_flatIndex;
    m_flatIndex = -1; // detach before the node disappears
    m_ctrl->removeNode(idx);
}

// ── Auto / modes / smooth / histogram / eyedroppers ──────────────────────────

namespace {
// Black/white input levels for a histogram, clipping `clip` of the population
// at each end (auto contrast).
bool autoLevels(const std::array<int, 256>& bins, int& lo, int& hi)
{
    long total = 0;
    for (int v : bins) total += v;
    if (total <= 0) return false;
    const long thresh = std::max(1L, static_cast<long>(total * 0.001));
    long cum = 0; lo = 0;
    for (int i = 0; i < 256; ++i) { cum += bins[i]; if (cum >= thresh) { lo = i; break; } }
    cum = 0; hi = 255;
    for (int i = 255; i >= 0; --i) { cum += bins[i]; if (cum >= thresh) { hi = i; break; } }
    return hi > lo;
}

void insertOrSetPoint(std::vector<QPoint>& pts, int x, int y)
{
    x = std::clamp(x, 0, 255);
    y = std::clamp(y, 0, 255);
    for (auto& p : pts) {
        if (p.x() == x) { p.setY(y); return; }
    }
    int at = 0;
    while (at < static_cast<int>(pts.size()) && pts[at].x() < x) ++at;
    pts.insert(pts.begin() + at, QPoint(x, y));
}
} // namespace

void CurvesEditorWidget::onAuto()
{
    if (m_loading || m_flatIndex < 0)
        return;
    // Auto is an explicit action that needs the bins now: if the async backdrop
    // histogram has not landed yet, compute it synchronously as a fallback.
    if (!m_hasHistogram)
        computeHistogramSync();
    if (!m_hasHistogram)
        return;

    const curves::Channel ch = currentChannel();
    const std::array<int, 256>* bins = &m_histLum;
    switch (ch) {
    case curves::Channel::Red:   bins = &m_histR; break;
    case curves::Channel::Green: bins = &m_histG; break;
    case curves::Channel::Blue:  bins = &m_histB; break;
    case curves::Channel::Alpha: return; // alpha not curved
    case curves::Channel::RGB:   bins = &m_histLum; break;
    }
    int lo = 0, hi = 255;
    if (!autoLevels(*bins, lo, hi))
        return;

    beginGesture();
    m_data.points(ch) = curves::sanitize({ QPoint(lo, 0), QPoint(hi, 255) });
    markCustomPreset();
    m_graph->setSelectedIndex(0);
    updateChannelView();
    applyAndCommit(tr("Auto Contrast"));
}

void CurvesEditorWidget::onEditMode()
{
    m_editCurveBtn->blockSignals(true);
    m_editCurveBtn->setChecked(true);
    m_editCurveBtn->blockSignals(false);
    m_pencilBtn->blockSignals(true);
    m_pencilBtn->setChecked(false);
    m_pencilBtn->blockSignals(false);
    m_graph->setMode(CurveGraphWidget::Mode::Points);
}

void CurvesEditorWidget::onPencilMode()
{
    m_pencilBtn->blockSignals(true);
    m_pencilBtn->setChecked(true);
    m_pencilBtn->blockSignals(false);
    m_editCurveBtn->blockSignals(true);
    m_editCurveBtn->setChecked(false);
    m_editCurveBtn->blockSignals(false);
    m_graph->setMode(CurveGraphWidget::Mode::Pencil);
}

void CurvesEditorWidget::onSmooth()
{
    if (m_loading || m_flatIndex < 0)
        return;
    auto& pts = m_data.points(currentChannel());
    if (pts.size() < 3)
        return; // nothing meaningful to smooth (extremes only)

    beginGesture();
    // [1,2,1]/4 passes on the interior points' output — preserves the two
    // extremes and the x positions exactly, so no overshoot and no clipping.
    std::vector<QPoint> cur = pts;
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<QPoint> next = cur;
        for (int i = 1; i + 1 < static_cast<int>(cur.size()); ++i) {
            const int y = (cur[i - 1].y() + 2 * cur[i].y() + cur[i + 1].y()) / 4;
            next[i].setY(std::clamp(y, 0, 255));
        }
        cur.swap(next);
    }
    pts = curves::sanitize(cur);
    markCustomPreset();
    m_graph->setSelectedIndex(-1);
    updateChannelView();
    applyAndCommit(tr("Smooth Curve"));
}

void CurvesEditorWidget::onHistRefresh()
{
    if (m_flatIndex < 0)
        return;
    m_histNodeIndex = -2;
    refreshHistogram();
    updateChannelView();
}

int CurvesEditorWidget::sampledChannelValue(const QColor& c) const
{
    switch (currentChannel()) {
    case curves::Channel::Red:   return c.red();
    case curves::Channel::Green: return c.green();
    case curves::Channel::Blue:  return c.blue();
    case curves::Channel::Alpha: return c.alpha();
    case curves::Channel::RGB:
    default:
        return qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
    }
}

void CurvesEditorWidget::onEyedropper(int mode, bool on)
{
    if (m_loading)
        return;
    QToolButton* btns[4] = { m_eyeBlack, m_eyeGray, m_eyeWhite, m_targetBtn };
    if (on) {
        for (int i = 0; i < 4; ++i) {
            if (i + 1 != mode && btns[i]->isChecked()) {
                btns[i]->blockSignals(true);
                btns[i]->setChecked(false);
                btns[i]->blockSignals(false);
            }
        }
        emit requestImagePick(mode);
    } else {
        emit requestImagePick(0); // disarm
    }
}

QColor CurvesEditorWidget::sampleInputColor(const QPointF& docPos)
{
    if (!m_ctrl || !m_ctrl->document() || m_flatIndex < 0)
        return {};
    Document* doc = m_ctrl->document();
    auto* node = doc->nodeAt(m_flatIndex);
    if (!node)
        return {};

    // Composite with THIS adjustment hidden so the read is the curve's input.
    // Synchronous, throwaway image — does not touch the on-screen projection
    // (compositionGeneration is untouched), so there is no visual flicker.
    const bool savedVisible = node->visible;
    node->visible = false;
    node->invalidateEffects();
    QImage input = compositeImage(doc);
    node->visible = savedVisible;
    node->invalidateEffects();

    if (input.isNull()
        || docPos.x() < 0 || docPos.y() < 0
        || docPos.x() >= input.width() || docPos.y() >= input.height())
        return {};
    const int x = std::clamp(static_cast<int>(std::lround(docPos.x())), 0, input.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(docPos.y())), 0, input.height() - 1);
    const QColor c = input.pixelColor(x, y);
    if (c.alpha() == 0)
        return {}; // transparent input — nothing to sample
    return c;
}

void CurvesEditorWidget::pickInputColor(const QPointF& docPos, int mode)
{
    applyPickedColor(sampleInputColor(docPos), mode);
}

void CurvesEditorWidget::applyPickedColor(const QColor& color, int mode)
{
    for (auto* b : { m_eyeBlack, m_eyeGray, m_eyeWhite, m_targetBtn }) {
        b->blockSignals(true);
        b->setChecked(false);
        b->blockSignals(false);
    }
    if (!color.isValid() || m_flatIndex < 0)
        return;

    const int lum = qRound(0.299 * color.red() + 0.587 * color.green()
                           + 0.114 * color.blue());

    beginGesture();
    QString label;
    if (mode == 1 || mode == 3) {
        // Black/White set the RGB master endpoint: sampled tone → 0 (black) or
        // → 255 (white). Show the master channel so the change is visible.
        auto& m = m_data.points(curves::Channel::RGB);
        if (m.size() >= 2) {
            if (mode == 1) {
                m.front().setX(std::clamp(lum, 0, m.back().x() - 1));
                m.front().setY(0);
            } else {
                m.back().setX(std::clamp(lum, m.front().x() + 1, 255));
                m.back().setY(255);
            }
        }
        m_channelCombo->blockSignals(true);
        m_channelCombo->setCurrentIndex(0);
        m_channelCombo->blockSignals(false);
        label = (mode == 1) ? tr("Set Black Point") : tr("Set White Point");
    } else { // gray: neutralize the dominant by mapping each R/G/B → luminance
        insertOrSetPoint(m_data.points(curves::Channel::Red),   color.red(),   lum);
        insertOrSetPoint(m_data.points(curves::Channel::Green), color.green(), lum);
        insertOrSetPoint(m_data.points(curves::Channel::Blue),  color.blue(),  lum);
        for (auto c : { curves::Channel::Red, curves::Channel::Green, curves::Channel::Blue })
            m_data.points(c) = curves::sanitize(m_data.points(c));
        label = tr("Set Gray Point");
    }

    m_data.points(curves::Channel::RGB) = curves::sanitize(m_data.points(curves::Channel::RGB));
    markCustomPreset();
    m_graph->setSelectedIndex(0);
    updateChannelView();
    applyAndCommit(label);
}

int CurvesEditorWidget::ensurePointAt(int x)
{
    auto& pts = m_data.points(currentChannel());
    x = std::clamp(x, 0, 255);
    const int tol = 6;
    int best = -1, bestD = tol + 1;
    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        const int dx = pts[i].x() - x;
        const int d = dx < 0 ? -dx : dx;
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best >= 0)
        return best;
    std::array<unsigned char, 256> lut{};
    curves::lutFromPoints(pts, lut);
    int at = 0;
    while (at < static_cast<int>(pts.size()) && pts[at].x() < x) ++at;
    pts.insert(pts.begin() + at, QPoint(x, lut[x]));
    return at;
}

void CurvesEditorWidget::targetBegan(const QPointF& docPos)
{
    m_targetPointIndex = -1; // a failed begin must not let the drag move a point
    if (m_flatIndex < 0 || currentChannel() == curves::Channel::Alpha)
        return;
    const QColor color = sampleInputColor(docPos);
    if (!color.isValid())
        return;
    const int v = sampledChannelValue(color);
    beginGesture();
    m_targetPointIndex = ensurePointAt(v);
    const auto& pts = m_data.points(currentChannel());
    m_targetStartOutput = (m_targetPointIndex >= 0
                           && m_targetPointIndex < static_cast<int>(pts.size()))
        ? pts[m_targetPointIndex].y() : 0;
    markCustomPreset();
    m_graph->setSelectedIndex(m_targetPointIndex);
    updateChannelView();
    applyLive();
}

void CurvesEditorWidget::targetDragged(int dyPixels)
{
    auto& pts = m_data.points(currentChannel());
    if (m_targetPointIndex < 0 || m_targetPointIndex >= static_cast<int>(pts.size()))
        return;
    // ~0.6 output units per pixel: a comfortable drag spans the full range.
    const int newY = std::clamp(
        m_targetStartOutput + static_cast<int>(std::lround(dyPixels * 0.6)), 0, 255);
    pts[m_targetPointIndex].setY(newY);
    m_graph->blockSignals(true);
    m_graph->setPoints(pts);
    m_graph->setSelectedIndex(m_targetPointIndex);
    m_graph->blockSignals(false);
    updateInputOutputFields();
    applyLive();
}

void CurvesEditorWidget::targetEnded()
{
    commitGesture(tr("Target Adjustment"));
    m_targetPointIndex = -1;
}

void CurvesEditorWidget::hideEvent(QHideEvent* e)
{
    // Never leave a node stuck in bypass when the editor goes away.
    if (m_ctrl && m_flatIndex >= 0 && !m_previewOn) {
        m_previewOn = true;
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    }
    // Disarm any pending canvas eyedropper/target pick.
    disarmPicks();
    QWidget::hideEvent(e);
}

void CurvesEditorWidget::disarmPicks()
{
    bool any = false;
    for (auto* b : { m_eyeBlack, m_eyeGray, m_eyeWhite, m_targetBtn }) {
        if (b && b->isChecked()) {
            b->blockSignals(true);
            b->setChecked(false);
            b->blockSignals(false);
            any = true;
        }
    }
    if (any)
        emit requestImagePick(0);
}
