#include "SolidColorAdjustmentWidget.hpp"

#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AdjustmentPanelBar.hpp"
#include "ui/colorpicker/ColorPickerDialog.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

// ════════════════════════════════════════════════════════════════════════════
//  ColorSwatchButton
// ════════════════════════════════════════════════════════════════════════════

ColorSwatchButton::ColorSwatchButton(QWidget* parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(48);
    setToolTip(tr("Click to choose a colour"));
}

void ColorSwatchButton::setColor(const QColor& c)
{
    if (m_color == c)
        return;
    m_color = c;
    update();
}

void ColorSwatchButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);

    // Checkerboard only matters where the colour is translucent.
    if (m_color.alpha() < 255) {
        const int cell = 8;
        const QColor c1(200, 200, 200), c2(150, 150, 150);
        for (int y = 0; y * cell < r.height() + cell; ++y) {
            for (int x = 0; x * cell < r.width() + cell; ++x) {
                p.fillRect(r.left() + x * cell, r.top() + y * cell, cell, cell,
                           ((x + y) & 1) ? c1 : c2);
            }
        }
    }

    p.fillRect(r, m_color);
    p.setPen(QPen(ThemeManager::instance()->current()->colorBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

// ════════════════════════════════════════════════════════════════════════════
//  SolidColorAdjustmentWidget
// ════════════════════════════════════════════════════════════════════════════

SolidColorAdjustmentWidget::SolidColorAdjustmentWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void SolidColorAdjustmentWidget::setController(ImageController* ctrl)
{
    if (m_ctrl == ctrl)
        return;
    // Leaving the previous controller (switching documents/tabs): undo any live
    // bypass still applied to the bound node, drop its documentChanged
    // subscription, and detach so a later showNode() rebinds cleanly.
    if (m_ctrl) {
        if (m_flatIndex >= 0 && !m_previewOn)
            m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
        disconnect(m_ctrl, &ImageController::documentChanged,
                   this, &SolidColorAdjustmentWidget::onDocumentChanged);
    }
    m_ctrl = ctrl;
    m_flatIndex = -1;
    m_inGesture = false;
    m_previewOn = true;
    if (ctrl) {
        connect(ctrl, &ImageController::documentChanged,
                this, &SolidColorAdjustmentWidget::onDocumentChanged,
                Qt::UniqueConnection);
    }
}

void SolidColorAdjustmentWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Header ──
    auto* header = new QHBoxLayout();
    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QPixmap(QStringLiteral(":/icons/layer-adjustments.png")));
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setScaledContents(true);
    m_maskThumb = new QLabel(this);
    m_maskThumb->setFixedSize(20, 20);
    m_maskThumb->setScaledContents(true);
    m_maskThumb->setVisible(false);
    m_titleLabel = new QLabel(tr("Solid Color"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    header->addWidget(m_iconLabel);
    header->addWidget(m_maskThumb);
    header->addWidget(m_titleLabel);
    header->addStretch();
    root->addLayout(header);

    // ── Large swatch ──
    m_swatch = new ColorSwatchButton(this);
    root->addWidget(m_swatch);

    // ── Hex field ──
    auto* hexRow = new QHBoxLayout();
    hexRow->addWidget(new QLabel(tr("Hex:"), this));
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setMaxLength(7);
    m_hexEdit->setPlaceholderText(QStringLiteral("#RRGGBB"));
    hexRow->addWidget(m_hexEdit, 1);
    root->addLayout(hexRow);

    // ── RGB spin boxes ──
    auto* rgbRow = new QHBoxLayout();
    auto makeSpin = [this](const QString& label) {
        auto* col = new QVBoxLayout();
        col->setSpacing(2);
        col->addWidget(new QLabel(label, this));
        auto* spin = new QSpinBox(this);
        spin->setRange(0, 255);
        col->addWidget(spin);
        return std::make_pair(col, spin);
    };
    auto [rCol, rSpin] = makeSpin(tr("R"));
    auto [gCol, gSpin] = makeSpin(tr("G"));
    auto [bCol, bSpin] = makeSpin(tr("B"));
    m_rSpin = rSpin; m_gSpin = gSpin; m_bSpin = bSpin;
    rgbRow->addLayout(rCol);
    rgbRow->addLayout(gCol);
    rgbRow->addLayout(bCol);
    root->addLayout(rgbRow);

    root->addStretch();

    // ── Shared adjustment-layer bottom bar ──
    m_bar = new AdjustmentPanelBar(this);
    root->addWidget(m_bar);

    // ── Wiring ──
    connect(m_swatch, &QAbstractButton::clicked,
            this, &SolidColorAdjustmentWidget::openColorPicker);
    connect(m_hexEdit, &QLineEdit::editingFinished,
            this, &SolidColorAdjustmentWidget::onHexEdited);
    connect(m_rSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &SolidColorAdjustmentWidget::onRgbSpinEdited);
    connect(m_gSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &SolidColorAdjustmentWidget::onRgbSpinEdited);
    connect(m_bSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &SolidColorAdjustmentWidget::onRgbSpinEdited);

    connect(m_bar, &AdjustmentPanelBar::resetClicked,
            this, &SolidColorAdjustmentWidget::onReset);
    connect(m_bar, &AdjustmentPanelBar::clipToggled,
            this, &SolidColorAdjustmentWidget::onClipToggled);
    connect(m_bar, &AdjustmentPanelBar::previewToggled,
            this, &SolidColorAdjustmentWidget::onPreviewToggled);
    connect(m_bar, &AdjustmentPanelBar::visibilityToggled,
            this, &SolidColorAdjustmentWidget::onVisibilityToggled);
    connect(m_bar, &AdjustmentPanelBar::deleteClicked,
            this, &SolidColorAdjustmentWidget::onDelete);
}

// ── Node binding ─────────────────────────────────────────────────────────────

void SolidColorAdjustmentWidget::showNode(int flatIndex)
{
    if (m_ctrl && m_flatIndex >= 0 && m_flatIndex != flatIndex && !m_previewOn)
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    m_inGesture = false;
    m_previewOn = true;
    m_flatIndex = flatIndex;
    reloadFromNode();
}

void SolidColorAdjustmentWidget::reloadFromNode()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer())
        return;

    m_loading = true;
    m_data = solidcolor::SolidColorData::fromParams(node->adjustment->params);

    m_bar->setPreviewChecked(true);
    m_bar->setVisibilityChecked(node->visible);
    const bool single = node->parent
        && node->parent->type == LayerTreeNode::Type::Layer;
    m_bar->setClipChecked(single);

    refreshHeader();
    syncControls();
    m_loading = false;
}

void SolidColorAdjustmentWidget::refreshHeader()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    auto* node = m_ctrl->document() ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node)
        return;
    m_titleLabel->setText(node->name.isEmpty() ? tr("Solid Color") : node->name);

    if (node->layer && !node->layer->maskImage.isNull() && node->layer->maskVisible) {
        QImage thumb = node->layer->maskImage.scaled(
            20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_maskThumb->setPixmap(QPixmap::fromImage(thumb));
        m_maskThumb->setVisible(true);
    } else {
        m_maskThumb->setVisible(false);
    }
}

void SolidColorAdjustmentWidget::syncControls()
{
    const QColor c = m_data.color;
    m_swatch->setColor(c);

    m_hexEdit->blockSignals(true);
    m_hexEdit->setText(c.name(QColor::HexRgb).toUpper());
    m_hexEdit->blockSignals(false);

    const std::pair<QSpinBox*, int> spins[] = {
        { m_rSpin, c.red() }, { m_gSpin, c.green() }, { m_bSpin, c.blue() }
    };
    for (const auto& s : spins) {
        s.first->blockSignals(true);
        s.first->setValue(s.second);
        s.first->blockSignals(false);
    }
}

// ── Param plumbing ───────────────────────────────────────────────────────────

void SolidColorAdjustmentWidget::setColorLive(const QColor& c)
{
    // Keep the layer's stored alpha — the internal picker / RGB spins only edit
    // the opaque colour (alpha-aware editing is a forward-compatible extension).
    m_data.color = QColor(c.red(), c.green(), c.blue(), m_data.color.alpha());
    m_loading = true;
    syncControls();
    m_loading = false;
    applyLive();
}

void SolidColorAdjustmentWidget::applyLive()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    if (m_previewOn) {
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    } else {
        // Bypass: an alpha-0 fill reveals the layers below unchanged.
        QColor transparent = m_data.color;
        transparent.setAlpha(0);
        m_ctrl->updateAdjustmentParamsLive(
            m_flatIndex, solidcolor::SolidColorData::paramsFromColor(transparent));
    }
}

void SolidColorAdjustmentWidget::beginGesture()
{
    if (m_inGesture)
        return;
    m_gestureBefore = paramsFromData();
    m_inGesture = true;
    if (m_ctrl)
        m_ctrl->setAdjustmentLiveEdit(true);
}

void SolidColorAdjustmentWidget::commitGesture(const QString& label)
{
    if (!m_inGesture)
        return;
    m_inGesture = false;
    if (m_ctrl && m_flatIndex >= 0)
        m_ctrl->commitAdjustmentParams(m_flatIndex, m_gestureBefore,
                                       paramsFromData(), label);
    if (m_ctrl)
        m_ctrl->setAdjustmentLiveEdit(false);
}

void SolidColorAdjustmentWidget::applyAndCommit(const QString& label)
{
    applyLive();
    commitGesture(label);
}

// ── Colour picker ────────────────────────────────────────────────────────────

void SolidColorAdjustmentWidget::openColorPicker()
{
    if (m_loading || !m_ctrl || m_flatIndex < 0)
        return;

    const QColor before = m_data.color;
    beginGesture();   // captures m_gestureBefore + enters live-edit

    auto* dlg = new ColorPickerDialog(before, ColorPickerMode::Foreground, this);
    connect(dlg, &ColorPickerDialog::colorPreviewChanged, this,
            [this](const QColor& c) { setColorLive(c); });

    const int res = dlg->exec();
    if (res == QDialog::Accepted) {
        setColorLive(dlg->selectedColor());
        commitGesture(tr("Solid Color"));   // single undo step before→after
    } else {
        // Cancel: restore the previous colour without recording an undo step.
        m_data.color = before;
        m_loading = true;
        syncControls();
        m_loading = false;
        applyLive();
        m_inGesture = false;
        if (m_ctrl)
            m_ctrl->setAdjustmentLiveEdit(false);
    }
    delete dlg;
}

// ── Callbacks ────────────────────────────────────────────────────────────────

void SolidColorAdjustmentWidget::onHexEdited()
{
    if (m_loading || m_flatIndex < 0)
        return;
    QString text = m_hexEdit->text().trimmed();
    if (!text.startsWith('#'))
        text.prepend('#');
    QColor c(text);
    if (!c.isValid()) {
        syncControls();   // revert the field to the current colour
        return;
    }
    if (QColor(c.red(), c.green(), c.blue(), m_data.color.alpha()) == m_data.color) {
        syncControls();
        return;
    }
    beginGesture();
    m_data.color = QColor(c.red(), c.green(), c.blue(), m_data.color.alpha());
    m_loading = true;
    syncControls();
    m_loading = false;
    applyAndCommit(tr("Solid Color"));
}

void SolidColorAdjustmentWidget::onRgbSpinEdited()
{
    if (m_loading || m_flatIndex < 0)
        return;
    const QColor next(m_rSpin->value(), m_gSpin->value(), m_bSpin->value(),
                      m_data.color.alpha());
    if (next == m_data.color)
        return;
    beginGesture();
    m_data.color = next;
    m_loading = true;
    syncControls();
    m_loading = false;
    applyAndCommit(tr("Solid Color"));
}

void SolidColorAdjustmentWidget::onReset(bool /*shiftHeld*/)
{
    if (m_loading || m_flatIndex < 0)
        return;
    const QColor white(255, 255, 255, m_data.color.alpha());
    if (white == m_data.color)
        return;
    beginGesture();
    m_data.color = white;
    m_loading = true;
    syncControls();
    m_loading = false;
    applyAndCommit(tr("Reset Solid Color"));
}

void SolidColorAdjustmentWidget::onClipToggled(bool single)
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

void SolidColorAdjustmentWidget::onPreviewToggled(bool on)
{
    if (m_loading)
        return;
    m_previewOn = on;
    applyLive();
}

void SolidColorAdjustmentWidget::onVisibilityToggled(bool visible)
{
    if (m_loading || !m_ctrl || m_flatIndex < 0)
        return;
    m_ctrl->setNodeVisibility(m_flatIndex, visible);
}

void SolidColorAdjustmentWidget::onDelete()
{
    if (!m_ctrl || m_flatIndex < 0)
        return;
    const int idx = m_flatIndex;
    m_flatIndex = -1;   // detach before the node disappears
    m_ctrl->removeNode(idx);
}

void SolidColorAdjustmentWidget::onDocumentChanged()
{
    if (!isVisible() || m_inGesture || m_flatIndex < 0)
        return;
    auto* node = m_ctrl && m_ctrl->document()
                     ? m_ctrl->document()->nodeAt(m_flatIndex) : nullptr;
    if (!node || !node->isAdjustmentLayer()
        || node->adjustment->type != QLatin1String("solidcolor"))
        return; // node went away/changed — MainWindow swaps the view on select
    reloadFromNode();
}

void SolidColorAdjustmentWidget::hideEvent(QHideEvent* e)
{
    // Never leave a node stuck in bypass when the editor goes away.
    if (m_ctrl && m_flatIndex >= 0 && !m_previewOn) {
        m_previewOn = true;
        m_ctrl->updateAdjustmentParamsLive(m_flatIndex, paramsFromData());
    }
    QWidget::hideEvent(e);
}
