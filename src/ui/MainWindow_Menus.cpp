#include "MainWindow.hpp"
#include "AboutDialog.hpp"
#include "controller/ImageController.hpp"
#include "renderer/CanvasView.hpp"
#include "core/ColorEngine.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/LayerEffect.hpp"
#include "core/Layer.hpp"
#include "colorpicker/ColorPickerDialog.hpp"
#include "ui/LayerStylesDialog.hpp"
#include "ui/ResizeLayerDialog.hpp"
#include "ui/AiUpscaleDialog.hpp"
#include "ui/ImageSizeDialog.hpp"
#include "ui/CanvasSizeDialog.hpp"
#include "ui/dialogs/ToolDialog.hpp"
#include "ui/dialogs/AdjustColorDialog.hpp"
#include "ui/dialogs/GaussianBlurDialog.hpp"
#include "ui/dialogs/SharpenDialog.hpp"
#include "ui/dialogs/MedianBlurDialog.hpp"
#include "ui/dialogs/BoxBlurDialog.hpp"
#include "ui/dialogs/BilateralBlurDialog.hpp"
#include "ui/dialogs/MotionBlurDialog.hpp"
#include "ui/dialogs/RadialBlurDialog.hpp"
#include "ui/dialogs/ZoomBlurDialog.hpp"
#include "ui/dialogs/EdgeDetectDialog.hpp"
#include "ui/dialogs/PosterizeDialog.hpp"
#include "ui/dialogs/ThresholdDialog.hpp"
#include "ui/dialogs/NoiseReduceDialog.hpp"
#include "ui/dialogs/RemoveBgDialog.hpp"
#include "colorpicker/ColorPickerDialog.hpp"
#include "core/SolidColorData.hpp"

#include <QMessageBox>

// ── execToolDialog helper ─────────────────────────────────────────

void MainWindow::execToolDialog(ToolDialog* dlg, const QString& toolName,
                                 const QVariantMap& defaultParams)
{
    if (!m_ctrl || !m_canvas) { delete dlg; return; }

    m_canvas->clearPreview();

    connect(dlg, &ToolDialog::previewChanged, this,
            [this, toolName](const QVariantMap& params) {
        QVariantMap numParams;
        for (auto it = params.begin(); it != params.end(); ++it)
            numParams[it.key()] = it.value().toDouble();
        m_canvas->showPreview(toolName, numParams);
    });

    connect(dlg, &QDialog::accepted, this, [this, dlg, toolName, defaultParams]() {
        QVariantMap params = dlg->collectParams();
        m_canvas->clearPreview();

        if (dlg->mode() == ToolDialog::Mode::Adjustment) {
            auto node = std::make_unique<LayerTreeNode>();
            node->type = LayerTreeNode::Type::Adjustment;
            node->name = dlg->windowTitle();
            node->setBaseOpacity(1.0f);
            node->setBaseVisible(true);
            node->setBaseBlendMode(BlendMode::Normal);

            QVariantMap numParams;
            for (auto it = params.begin(); it != params.end(); ++it)
                numParams[it.key()] = it.value().toDouble();

            node->effects.push_back(LayerEffect(toolName, numParams, defaultParams));
            node->layer = std::make_shared<Layer>();
            node->layer->name = node->name;
            node->layer->cpuImage = QImage(m_doc->size, QImage::Format_RGBA8888);
            node->layer->cpuImage.fill(Qt::transparent);
            node->layer->owner = node.get();

            m_doc->roots.push_back(std::move(node));
            m_doc->activeFlatIndex = 0;
            m_canvas->syncLayersToGpu();
            m_canvas->update();
            refreshLayerPanel();
        } else {
            ImageController::JsonMap numParams;
            for (auto it = params.begin(); it != params.end(); ++it)
                numParams[it.key().toStdString()] = it.value().toDouble();
            m_ctrl->executeTool(toolName.toStdString(), numParams);
            m_canvas->syncLayersToGpu();
            m_canvas->update();
        }

        dlg->deleteLater();
    });

    connect(dlg, &QDialog::rejected, this, [this, dlg]() {
        m_canvas->clearPreview();
        dlg->deleteLater();
    });

    dlg->setWindowFlags(dlg->windowFlags() | Qt::Tool);
    dlg->show();
}

// ── Layer Menu Handlers ──────────────────────────────────────────

void MainWindow::onMenuAddLayer()
{
    if (m_ctrl && m_doc) {
        m_ctrl->executeTool("add_layer", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuRemoveLayer()
{
    if (!m_ctrl || !m_doc) return;
    int idx = m_ctrl->activeLayerIndex();
    if (idx < 0) return;

    if (!m_ctrl->isLayerEmpty(idx)) {
        auto ret = QMessageBox::question(this, tr("Delete Layer"),
            tr("This layer is not empty. Delete it anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    m_ctrl->removeSelectedNodes();
    m_canvas->syncLayersToGpu();
    m_canvas->update();
    refreshLayerPanel();
    refreshPropertiesPanel();
    updateLayerMenuState();
}

void MainWindow::onMenuDuplicateLayer()
{
    if (m_ctrl && m_doc) {
        int idx = m_ctrl->activeLayerIndex();
        if (idx < 0) return;
        m_ctrl->executeTool("duplicate_layer", {{"index", static_cast<double>(idx)}});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuFillLayer()
{
    if (!m_ctrl || !m_doc) return;
    auto* layer = m_ctrl->activeLayer();
    if (!layer) return;

    QColor initial = m_colorEngine ? m_colorEngine->foregroundColor() : Qt::white;
    ColorPickerDialog dlg(initial, ColorPickerMode::Foreground, this);
    if (dlg.exec() == QDialog::Accepted) {
        QColor c = dlg.selectedColor();
        m_ctrl->executeTool("fill_layer", {
            {"red", static_cast<double>(c.red())},
            {"green", static_cast<double>(c.green())},
            {"blue", static_cast<double>(c.blue())},
            {"alpha", static_cast<double>(c.alpha())}
        });
        m_canvas->syncLayersToGpu();
        m_canvas->update();
    }
}

void MainWindow::createSolidColorLayer()
{
    if (!m_ctrl || !m_doc || m_doc->size.isEmpty())
        return;

    // Initial suggestion: the active foreground colour (white fallback).
    QColor initial = m_colorEngine ? m_colorEngine->foregroundColor() : QColor(Qt::white);
    if (!initial.isValid())
        initial = QColor(Qt::white);

    ColorPickerDialog dlg(initial, ColorPickerMode::Foreground, this);
    if (dlg.exec() != QDialog::Accepted)
        return;   // cancel = no layer (no ghost layer left behind)

    const QColor chosen = dlg.selectedColor();
    m_ctrl->addAdjustmentLayer(QStringLiteral("solidcolor"),
                               solidcolor::SolidColorData::paramsFromColor(chosen));
    if (m_canvas) { m_canvas->syncLayersToGpu(); m_canvas->update(); }
    refreshLayerPanel();
    refreshPropertiesPanel();
    updateLayerMenuState();
}

void MainWindow::onMenuMergeVisible()
{
    if (m_ctrl && m_doc && m_doc->flatCount() >= 2) {
        m_ctrl->executeTool("merge_visible", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuMergeDown()
{
    if (m_ctrl && m_doc && m_doc->activeFlatIndex >= 0
        && m_doc->activeFlatIndex < m_doc->flatCount() - 1) {
        m_ctrl->executeTool("merge_down", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuMergeLayers()
{
    if (m_ctrl && m_doc && m_doc->flatCount() >= 2) {
        m_ctrl->executeTool("merge_layers", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuFlattenImage()
{
    if (m_ctrl && m_doc && m_doc->flatCount() >= 1) {
        m_ctrl->executeTool("flatten_image", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuRasterizeLayer()
{
    if (m_ctrl && m_doc) {
        int idx = m_ctrl->activeLayerIndex();
        if (idx < 0) return;
        m_ctrl->executeTool("rasterize_layer", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuLayerStyles()
{
    showLayerStylesDialog(m_ctrl ? m_ctrl->activeLayerIndex() : -1);
}

void MainWindow::showLayerStylesDialog(int idx)
{
    if (!m_ctrl || !m_doc || !m_canvas || idx < 0) return;
    auto* node = m_doc->nodeAt(idx);
    if (!node || !node->layer) return;

    m_ctrl->setActiveNode(idx);

    const std::vector<LayerEffect> before = node->effects;
    auto* dlg = new LayerStylesDialog(before, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::NonModal);
    dlg->setModal(false);

    connect(dlg, &LayerStylesDialog::stylesChanged, this, [this, dlg, idx]() {
        if (!m_ctrl || !m_canvas) return;
        m_ctrl->previewLayerEffects(idx, dlg->styles());
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
    });

    connect(dlg, &QDialog::accepted, this, [this, dlg, idx, before]() {
        if (!m_ctrl || !m_canvas) return;
        m_ctrl->commitLayerEffects(idx, before, dlg->styles());
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        refreshHistoryPanel();
        updateLayerMenuState();
    });

    connect(dlg, &QDialog::rejected, this, [this, idx, before]() {
        if (!m_ctrl || !m_canvas) return;
        m_ctrl->previewLayerEffects(idx, before);
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onMenuLayerMaskAdd()
{
    if (m_ctrl) {
        m_ctrl->executeTool("layer_mask_add", {});
        m_canvas->update();
        refreshLayerPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuLayerMaskRemove()
{
    if (m_ctrl) {
        m_ctrl->executeTool("layer_mask_remove", {});
        m_canvas->update();
        refreshLayerPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuLayerMaskApply()
{
    if (m_ctrl) {
        m_ctrl->executeTool("layer_mask_apply", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuLayerMaskToggle()
{
    if (m_ctrl) {
        int idx = m_ctrl->activeLayerIndex();
        if (idx < 0) return;
        if (m_ctrl->isLayerMaskEnabled(idx))
            m_ctrl->executeTool("layer_mask_disable", {});
        else
            m_ctrl->executeTool("layer_mask_enable", {});
        m_canvas->update();
        refreshLayerPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onMenuFlipHorizontal()
{
    if (m_ctrl && m_doc) {
        m_ctrl->executeTool("flip_horizontal", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
    }
}

void MainWindow::onMenuFlipVertical()
{
    if (m_ctrl && m_doc) {
        m_ctrl->executeTool("flip_vertical", {});
        m_canvas->syncLayersToGpu();
        m_canvas->update();
    }
}

void MainWindow::onMenuResizeLayer()
{
    if (!m_ctrl || !m_doc) return;
    auto* layer = m_ctrl->activeLayer();
    if (!layer) return;

    int origW = layer->cpuImage.width();
    int origH = layer->cpuImage.height();

    ResizeLayerDialog dlg(origW, origH, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_ctrl->executeTool("resize_layer", {
            {"width", static_cast<double>(dlg.width())},
            {"height", static_cast<double>(dlg.height())}
        });
        m_canvas->syncLayersToGpu();
        m_canvas->update();
    }
}

// ── Image Menu Handlers ──────────────────────────────────────────

void MainWindow::onImageResizeImage()
{
    if (!m_ctrl || !m_doc)
        return;

    ImageSizeDialog dlg(m_doc->size, m_doc->resolutionDpi, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const ImageSizeSettings s = dlg.settings();
    ImageResizeOptions options;
    options.targetSize = s.pixelSize;
    options.resampleImage = s.resampleImage;
    options.scaleStyles = s.scaleStyles;
    options.interpolation = s.interpolation;
    options.updateResolution = true;
    options.resolutionDpi = s.resolutionDpi;

    const qint64 area = static_cast<qint64>(m_doc->size.width()) * m_doc->size.height();
    if (area >= m_doc->perfConfig.autoTileMinArea)
        m_ctrl->resizeImageAsync(options);
    else
        m_ctrl->resizeImage(options);
}

void MainWindow::onImageResizeCanvas()
{
    if (!m_ctrl || !m_doc)
        return;

    const QColor bgColor = m_colorEngine ? m_colorEngine->backgroundColor() : Qt::white;
    CanvasSizeDialog dlg(m_doc->size, m_doc->resolutionDpi, bgColor, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const CanvasSizeSettings s = dlg.settings();
    CanvasResizeOptions options;
    options.targetSize = s.targetSize;
    options.anchor = s.anchor;
    options.fillExtension = s.fillExtension;
    options.extensionColor = s.extensionColor;

    const qint64 area = static_cast<qint64>(m_doc->size.width()) * m_doc->size.height();
    if (area >= m_doc->perfConfig.autoTileMinArea)
        m_ctrl->resizeCanvasAsync(options);
    else
        m_ctrl->resizeCanvas(options);
}

void MainWindow::onImageColorAdjustments()
{
    auto* dlg = new AdjustColorDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "adjust_color", {{"brightness", 0.0}, {"contrast", 0.0},
        {"saturation", 0.0}, {"hue", 0.0}, {"auto_contrast", 0.0}});
}

void MainWindow::onImageAutoContrast()
{
    if (!m_ctrl || !m_doc) return;
    m_ctrl->executeTool("auto_contrast", {});
    m_canvas->syncLayersToGpu();
    m_canvas->update();
}

void MainWindow::onImageGaussianBlur()
{
    auto* dlg = new GaussianBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "gaussian_blur", {{"radius", 3.0}});
}

void MainWindow::onImageSharpen()
{
    auto* dlg = new SharpenDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "sharpen", {{"strength", 1.0}});
}

void MainWindow::onImageMedianBlur()
{
    auto* dlg = new MedianBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "median_blur", {{"kernel_size", 5.0}});
}

void MainWindow::onImageBoxBlur()
{
    auto* dlg = new BoxBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "box_blur", {{"radius", 3.0}});
}

void MainWindow::onImageBilateralBlur()
{
    auto* dlg = new BilateralBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "bilateral_blur",
        {{"diameter", 9.0}, {"sigma_color", 75.0}, {"sigma_space", 75.0}});
}

void MainWindow::onImageMotionBlur()
{
    auto* dlg = new MotionBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "motion_blur", {{"length", 15.0}, {"angle", 0.0}});
}

void MainWindow::onImageRadialBlur()
{
    auto* dlg = new RadialBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "radial_blur",
        {{"amount", 0.25}, {"samples", 12.0}, {"center_x", 0.5}, {"center_y", 0.5}});
}

void MainWindow::onImageZoomBlur()
{
    auto* dlg = new ZoomBlurDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "zoom_blur",
        {{"amount", 0.25}, {"samples", 12.0}, {"center_x", 0.5}, {"center_y", 0.5}});
}

void MainWindow::onImageEdgeDetect()
{
    auto* dlg = new EdgeDetectDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "edge_detect", {{"threshold1", 50.0}, {"threshold2", 150.0}});
}

void MainWindow::onImageGrayscale()
{
    if (!m_ctrl || !m_doc) return;
    m_ctrl->executeTool("grayscale", {});
    m_canvas->syncLayersToGpu();
    m_canvas->update();
}

void MainWindow::onImageInvert()
{
    if (!m_ctrl || !m_doc) return;
    m_ctrl->executeTool("invert_colors", {});
    m_canvas->syncLayersToGpu();
    m_canvas->update();
}

void MainWindow::onImagePosterize()
{
    auto* dlg = new PosterizeDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "posterize", {{"levels", 8.0}});
}

void MainWindow::onImageThreshold()
{
    auto* dlg = new ThresholdDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "threshold", {{"value", 128.0}, {"adaptive", 0.0}});
}

void MainWindow::onImageNoiseReduce()
{
    auto* dlg = new NoiseReduceDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "noise_reduce", {{"strength", 2.0}, {"preserve_edges", 0.5}});
}

void MainWindow::onImageRemoveBg()
{
    auto* dlg = new RemoveBgDialog(ToolDialog::Mode::Direct, this);
    execToolDialog(dlg, "remove_background", {{"mode", 0.0}, {"threshold", 20.0}, {"feather", 5.0}});
}

void MainWindow::onAiUpscaleDocument()
{
    showAiUpscaleDialog(UpscaleTarget::CurrentDocument);
}

void MainWindow::onAiUpscaleLayer()
{
    showAiUpscaleDialog(UpscaleTarget::CurrentLayer);
}

void MainWindow::showAiUpscaleDialog(UpscaleTarget target)
{
    if (!m_ctrl || !m_doc)
        return;

    AiUpscaleDialog dlg(m_ctrl->upscaleBackendStatus(), target, this);
    connect(&dlg, &AiUpscaleDialog::openModelManagerRequested,
            this, &MainWindow::onOpenAiSettings);
    connect(&dlg, &AiUpscaleDialog::backendPathChanged, this, [this]() {
        showViewportStatusMessage(tr("Real-ESRGAN backend path updated."), 2500);
    });

    if (dlg.exec() != QDialog::Accepted)
        return;

    UpscaleOptions options = dlg.options();
    if (options.target == UpscaleTarget::CurrentDocument
        && options.output == UpscaleOutputMode::ReplaceDocument) {
        QMessageBox::warning(this, tr("AI Upscale"),
            tr("Replace Document is disabled until full document snapshots are available. Use New Document."));
        return;
    }
    if (options.target == UpscaleTarget::CurrentLayer
        && options.output == UpscaleOutputMode::ReplaceLayer) {
        const auto answer = QMessageBox::question(this, tr("AI Upscale"),
            tr("Replace the current layer with the upscaled result?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_ctrl->upscale(options);
}

void MainWindow::onMenuColorPicker()
{
    if (!m_colorEngine) return;

    auto* dlg = new ColorPickerDialog(
        m_colorEngine->foregroundColor(),
        ColorPickerMode::Foreground,
        this);

    connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& color) {
        if (m_colorEngine) m_colorEngine->setForegroundColor(color);
    });
    connect(dlg, &ColorPickerDialog::swatchAddRequested, this, [this](const QColor& color) {
        if (m_colorEngine) m_colorEngine->addSwatchColor(color);
    });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void MainWindow::onAboutDialog()
{
    AboutDialog dlg(this);
    dlg.exec();
}
