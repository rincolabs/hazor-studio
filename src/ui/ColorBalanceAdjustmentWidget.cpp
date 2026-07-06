#include "ColorBalanceAdjustmentWidget.hpp"
#include "GradientSlider.hpp"

#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AdjustmentPanelBar.hpp"
#include "ui/AppCheckBox.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

#include <algorithm>

// ════════════════════════════════════════════════════════════════════════════
//  ColorBalanceAdjustmentWidget
// ════════════════════════════════════════════════════════════════════════════

namespace {

struct AxisInfo {
    const char* leftLabel;
    const char* rightLabel;
    QColor leftColor;
    QColor rightColor;
};

// Index matches colorbalance::Axis (CyanRed, MagentaGreen, YellowBlue).
const AxisInfo kAxes[colorbalance::kAxisCount] = {
    { "Cyan",    "Red",   QColor(0, 200, 210),   QColor(235, 60, 60) },
    { "Magenta", "Green", QColor(220, 60, 200),  QColor(70, 200, 70) },
    { "Yellow",  "Blue",  QColor(235, 220, 60),  QColor(70, 110, 230) },
};

} // namespace

ColorBalanceAdjustmentWidget::ColorBalanceAdjustmentWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void ColorBalanceAdjustmentWidget::setController(ImageController* ctrl)
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
                   this, &ColorBalanceAdjustmentWidget::onDocumentChanged);
    }
    m_ctrl = ctrl;
    m_flatIndex = -1;
    m_inGesture = false;
    m_previewOn = true;
    if (ctrl) {
        // Undo/redo (and other doc mutations) only emit documentChanged — reload
        // here so the controls reflect the reverted state.
        connect(ctrl, &ImageController::documentChanged,
                this, &ColorBalanceAdjustmentWidget::onDocumentChanged,
                Qt::UniqueConnection);
    }
}

void ColorBalanceAdjustmentWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Header ──
    auto* header = new QHBoxLayout();
    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QPixmap(QStringLiteral(":/icons/color-balance-adjustment.png")));
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setScaledContents(true);
    m_maskThumb = new QLabel(this);
    m_maskThumb->setFixedSize(20, 20);
    m_maskThumb->setScaledContents(true);
    m_maskThumb->setVisible(false);
    m_titleLabel = new QLabel(tr("Color Balance"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    header->addWidget(m_iconLabel);
    header->addWidget(m_maskThumb);
    header->addWidget(m_titleLabel);
    header->addStretch();
    root->addLayout(header);

    // ── Tone ──
    auto* toneRow = new QHBoxLayout();
    toneRow->addWidget(new QLabel(tr("Tone:"), this));
    m_toneCombo = new QComboBox(this);
    m_toneCombo->addItem(tr("Shadows"));
    m_toneCombo->addItem(tr("Midtones"));
    m_toneCombo->addItem(tr("Highlights"));
    m_toneCombo->setCurrentIndex(static_cast<int>(colorbalance::Tone::Midtones));
    toneRow->addWidget(m_toneCombo, 1);
    root->addLayout(toneRow);

    // ── Three colour-axis controls (label row + slider + value box) ──
    for (int a = 0; a < colorbalance::kAxisCount; ++a) {
        const AxisInfo& info = kAxes[a];

        auto* axisRow = new QHBoxLayout();
        axisRow->setSpacing(8);

        auto* sliderCol = new QVBoxLayout();
        sliderCol->setSpacing(2);

        auto* labels = new QHBoxLayout();
        auto* leftLbl = new QLabel(tr(info.leftLabel), this);
        auto* rightLbl = new QLabel(tr(info.rightLabel), this);
        rightLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labels->addWidget(leftLbl);
        labels->addStretch();
        labels->addWidget(rightLbl);
        sliderCol->addLayout(labels);

        auto* slider = new GradientSlider(this);
        slider->setRange(-100, 100);
        slider->setStops({
            {0.0, info.leftColor},
            {0.5, QColor(150, 150, 150)},
            {1.0, info.rightColor},
        });
        sliderCol->addWidget(slider);
        m_sliders[a] = slider;

        axisRow->addLayout(sliderCol, 1);
        root->addLayout(axisRow);

        connect(slider, &GradientSlider::valueChanged, this,
                [this, a](int v) { onSliderChanged(a, v); });
        connect(slider, &GradientSlider::sliderReleased, this,
                [this, a]() { onSliderReleased(a); });
        connect(slider, &GradientSlider::spinEdited, this,
                [this, a](int v) { onSpinEdited(a, v); });
    }

    // ── Preserve Luminosity ──
    m_preserveLumCheck = new AppCheckBox();
    m_preserveLumCheck->setText(tr("Preserve Luminosity"));
    m_preserveLumCheck->setChecked(true);
    root->addWidget(m_preserveLumCheck);

    root->addStretch();

    // ── Shared adjustment-layer bottom bar ──
    m_bar = new AdjustmentPanelBar(this);
    root->addWidget(m_bar);

    // ── Wiring ──
    connect(m_toneCombo, qOverload<int>(&QComboBox::activated),
            this, &ColorBalanceAdjustmentWidget::onToneActivated);
    connect(m_preserveLumCheck, &QCheckBox::toggled,
            this, &ColorBalanceAdjustmentWidget::onPreserveLumToggled);

    connect(m_bar, &AdjustmentPanelBar::resetClicked,
            this, &ColorBalanceAdjustmentWidget::onReset);
    connect(m_bar, &AdjustmentPanelBar::clipToggled,
            this, &ColorBalanceAdjustmentWidget::onClipToggled);
    connect(m_bar, &AdjustmentPanelBar::previewToggled,
            this, &ColorBalanceAdjustmentWidget::onPreviewToggled);
    connect(m_bar, &AdjustmentPanelBar::visibilityToggled,
            this, &ColorBalanceAdjustmentWidget::onVisibilityToggled);
    connect(m_bar, &AdjustmentPanelBar::deleteClicked,
            this, &ColorBalanceAdjustmentWidget::onDelete);
}

// ── Node binding ─────────────────────────────────────────────────────────────

colorbalance::Tone ColorBalanceAdjustmentWidget::currentTone() const
{
    return static_cast<colorbalance::Tone>(m_toneCombo->currentIndex());
}

QVariantMap ColorBalanceAdjustmentWidget::paramsFromData() const
{
    return m_data.toParams();
}

void ColorBalanceAdjustmentWidget::showNode(int flatIndex)
{
    // Restore the previously bound node's real params if we left it bypassed.
    if (m_ctrl && m_flatIndex >= 0 && m_flatIndex != flatIndex && !m_previewOn)
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    m_inGesture = false;
    m_previewOn = true;
    m_flatIndex = flatIndex;
    reloadFromNode();
}

void ColorBalanceAdjustmentWidget::reloadFromNode()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer())
        return;

    m_loading = true;
    m_data = colorbalance::ColorBalanceData::fromParams(node->adjustment->params);

    m_preserveLumCheck->blockSignals(true);
    m_preserveLumCheck->setChecked(m_data.preserveLuminosity);
    m_preserveLumCheck->blockSignals(false);

    m_bar->setPreviewChecked(true);
    m_bar->setVisibilityChecked(node->visible);
    const bool single = node->parent
        && node->parent->type == LayerTreeNode::Type::Layer;
    m_bar->setClipChecked(single);

    refreshHeader();
    reloadControlsForTone();
    m_loading = false;
}

void ColorBalanceAdjustmentWidget::refreshHeader()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node)
        return;
    m_titleLabel->setText(node->name.isEmpty() ? tr("Color Balance") : node->name);

    if (node->layer && !node->layer->maskImage.isNull() && node->layer->maskVisible) {
        QImage thumb = node->layer->maskImage.scaled(
            20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_maskThumb->setPixmap(QPixmap::fromImage(thumb));
        m_maskThumb->setVisible(true);
    } else {
        m_maskThumb->setVisible(false);
    }
}

void ColorBalanceAdjustmentWidget::reloadControlsForTone()
{
    const colorbalance::Tone tone = currentTone();
    for (int a = 0; a < colorbalance::kAxisCount; ++a) {
        const int v = m_data.value(tone, static_cast<colorbalance::Axis>(a));
        m_sliders[a]->setValue(v);
    }
}

// ── Param plumbing ───────────────────────────────────────────────────────────

void ColorBalanceAdjustmentWidget::applyLive()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    if (m_previewOn) {
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    } else {
        colorbalance::ColorBalanceData identity;   // all sliders 0
        identity.preserveLuminosity = m_data.preserveLuminosity;
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, identity.toParams());
    }
}

void ColorBalanceAdjustmentWidget::beginGesture()
{
    if (m_inGesture)
        return;
    m_gestureBefore = paramsFromData();
    m_inGesture = true;
    // Preview the drag on the GPU per-layer compositor (cheap) until commit.
    if (m_ctrl)
        m_ctrl->setAdjustmentLiveEdit(true);
}

void ColorBalanceAdjustmentWidget::commitGesture(const QString& label)
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

void ColorBalanceAdjustmentWidget::applyAndCommit(const QString& label)
{
    applyLive();
    commitGesture(label);
}

// ── Callbacks ────────────────────────────────────────────────────────────────

void ColorBalanceAdjustmentWidget::onToneActivated(int)
{
    if (m_loading)
        return;
    // Switching tone only changes which values are displayed — no undo, no
    // composite change.
    reloadControlsForTone();
}

void ColorBalanceAdjustmentWidget::onSliderChanged(int axis, int value)
{
    if (m_loading)
        return;
    // Capture the pre-change state before mutating, so the committed undo step
    // has a distinct "before". On a drag the first move opens the gesture and
    // later moves are no-ops here (m_inGesture guard).
    beginGesture();
    const colorbalance::Tone tone = currentTone();
    m_data.setValue(tone, static_cast<colorbalance::Axis>(axis), value);

    if (m_sliders[axis]->isSliderDown())
        applyLive();
    else
        applyAndCommit(tr("Color Balance"));
}

void ColorBalanceAdjustmentWidget::onSliderReleased(int)
{
    if (m_loading)
        return;
    commitGesture(tr("Color Balance"));
}

void ColorBalanceAdjustmentWidget::onSpinEdited(int axis, int value)
{
    if (m_loading)
        return;
    value = std::clamp(value, -100, 100);
    beginGesture();
    const colorbalance::Tone tone = currentTone();
    m_data.setValue(tone, static_cast<colorbalance::Axis>(axis), value);

    m_sliders[axis]->setValue(value);
    applyAndCommit(tr("Color Balance"));
}

void ColorBalanceAdjustmentWidget::onPreserveLumToggled(bool on)
{
    if (m_loading)
        return;
    beginGesture();
    m_data.preserveLuminosity = on;
    applyAndCommit(on ? tr("Preserve Luminosity On")
                      : tr("Preserve Luminosity Off"));
}

void ColorBalanceAdjustmentWidget::onReset(bool shiftHeld)
{
    if (m_loading || m_flatIndex < 0)
        return;
    beginGesture();
    if (shiftHeld)
        m_data.reset();             // all tones back to neutral
    else
        m_data.resetTone(currentTone());
    reloadControlsForTone();
    if (shiftHeld) {
        m_preserveLumCheck->blockSignals(true);
        m_preserveLumCheck->setChecked(m_data.preserveLuminosity);
        m_preserveLumCheck->blockSignals(false);
    }
    applyAndCommit(shiftHeld ? tr("Reset Color Balance")
                             : tr("Reset Tone"));
}

void ColorBalanceAdjustmentWidget::onClipToggled(bool single)
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
            m_bar->setClipChecked(false);   // no layer below — revert
            return;
        }
        m_ctrl->moveAdjustmentToLayer(m_flatIndex, target);
    } else {
        m_ctrl->moveAdjustmentToStack(m_flatIndex, m_flatIndex);
    }
    // Active index follows the moved node; MainWindow re-shows the editor.
}

void ColorBalanceAdjustmentWidget::onPreviewToggled(bool on)
{
    if (m_loading)
        return;
    m_previewOn = on;
    applyLive();    // bypass (identity) when off, real balance when on
}

void ColorBalanceAdjustmentWidget::onVisibilityToggled(bool visible)
{
    if (m_loading || !m_ctrl || m_flatIndex < 0)
        return;
    m_ctrl->setNodeVisibility(m_flatIndex, visible);
}

void ColorBalanceAdjustmentWidget::onDelete()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    const int idx = m_flatIndex;
    m_flatIndex = -1;   // detach before the node disappears
    m_ctrl->removeNode(idx);
}

void ColorBalanceAdjustmentWidget::onDocumentChanged()
{
    if (!isVisible() || m_inGesture || m_flatIndex < 0)
        return;
    auto* node = m_ctrl && m_ctrl->document()
                     ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer()
        || node->adjustment->type != QLatin1String("colorbalance"))
        return; // node went away/changed — MainWindow swaps the view on select
    reloadFromNode();
}

void ColorBalanceAdjustmentWidget::hideEvent(QHideEvent* e)
{
    // Never leave a node stuck in bypass when the editor goes away.
    if (m_ctrl && m_flatIndex >= 0 && !m_previewOn) {
        m_previewOn = true;
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    }
    QWidget::hideEvent(e);
}
