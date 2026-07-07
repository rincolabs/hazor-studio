#include "ImageController.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "engine/ImageEngine.hpp"
#include "Commands.hpp"
#include "text/TextRenderer.hpp"
#include "engine/ShapeRenderer.hpp"
#include "shape/ShapeCommands.hpp"
#include "async/AsyncJobSystem.hpp"
#include "processing/FilterProcessor.hpp"

#include <QImage>
#include <QPainter>
#include <QImageReader>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <GL/gl.h>
#include <QDebug>
#include <QSet>
#include <algorithm>
#include <algorithm>
#include <exception>
#include <cmath>
#include <variant>
#include <unordered_set>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

static LayerTreeNode* nodeAtOrWarn(Document* doc, int flatIndex)
{
    auto* node = doc->nodeAt(flatIndex);
    if (!node) qWarning("nodeAt(%d) returned null", flatIndex);
    return node;
}

static Layer* layerAtOrWarn(Document* doc, int flatIndex)
{
    auto* node = doc->nodeAt(flatIndex);
    if (!node) { qWarning("layerAt(%d): node null", flatIndex); return nullptr; }
    if (node->type != LayerTreeNode::Type::Layer) {
        qWarning("layerAt(%d): node is not a Layer type", flatIndex);
        return nullptr;
    }
    if (!node->layer) {
        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->owner = node;
    }
    return node->layer.get();
}

static QString progressLabelForTool(const std::string& toolName)
{
    if (toolName == "fill_bucket") return QObject::tr("Paint Bucket");
    if (toolName == "adjust_color") return QObject::tr("Applying Color Adjustment");
    if (toolName == "adjust_brightness") return QObject::tr("Applying Brightness");
    if (toolName == "adjust_contrast") return QObject::tr("Applying Contrast");
    if (toolName == "adjust_saturation") return QObject::tr("Applying Saturation");
    if (toolName == "adjust_hue") return QObject::tr("Applying Hue");
    if (toolName == "auto_contrast") return QObject::tr("Applying Auto Contrast");
    if (toolName == "gaussian_blur") return QObject::tr("Applying Gaussian Blur");
    if (toolName == "median_blur") return QObject::tr("Applying Median Blur");
    if (toolName == "box_blur") return QObject::tr("Applying Box Blur");
    if (toolName == "bilateral_blur") return QObject::tr("Applying Bilateral Blur");
    if (toolName == "motion_blur") return QObject::tr("Applying Motion Blur");
    if (toolName == "radial_blur") return QObject::tr("Applying Radial Blur");
    if (toolName == "zoom_blur") return QObject::tr("Applying Zoom Blur");
    if (toolName == "sharpen") return QObject::tr("Applying Sharpen");
    if (toolName == "edge_detect") return QObject::tr("Applying Edge Detect");
    if (toolName == "grayscale") return QObject::tr("Applying Grayscale");
    if (toolName == "invert_colors") return QObject::tr("Applying Invert");
    if (toolName == "noise_reduce") return QObject::tr("Applying Noise Reduction");
    if (toolName == "posterize") return QObject::tr("Applying Posterize");
    if (toolName == "threshold") return QObject::tr("Applying Threshold");
    if (toolName == "remove_background") return QObject::tr("Removing Background");
    return QObject::tr("Processing");
}

bool ImageController::executeToolChain(
    const std::vector<std::pair<std::string, JsonMap>>& chain)
{
    if (chain.empty() || !m_doc) return false;

    std::vector<std::pair<std::string, QVariantMap>> qChain;
    for (const auto& [name, params] : chain) {
        QVariantMap qp;
        for (const auto& [k, v] : params) {
            if (std::holds_alternative<double>(v))
                qp[QString::fromStdString(k)] = QVariant(std::get<double>(v));
            else if (std::holds_alternative<std::string>(v))
                qp[QString::fromStdString(k)] = QVariant(QString::fromStdString(std::get<std::string>(v)));
        }
        qChain.emplace_back(name, std::move(qp));
    }

    auto* layer = activeLayer();
    if (!layer) return false;
    if (!checkDestructiveOp(layer)) return false;

    if (layer->rasterStorage.isEnabled()) {
        layer->cpuImage = layer->compositeImage();
        layer->rasterStorage.clear();
        layer->textureOutdated = true;
    }
    QImage before = layer->cpuImage.copy();
    QTransform beforeT = m_doc->nodeAt(m_doc->activeFlatIndex)
                         ? m_doc->nodeAt(m_doc->activeFlatIndex)->transform
                         : QTransform();

    QImage result = processing::FilterProcessor::processBatch(layer->cpuImage, qChain);
    if (result.isNull()) return false;

    layer->cpuImage = std::move(result);
    if (layer->tiledSystem)
        layer->tileManager.markAllDirty();
    layer->textureOutdated = true;
    syncLayerToGpu(layer);
    emit imageChanged();

    QTransform afterT = m_doc->nodeAt(m_doc->activeFlatIndex)
                        ? m_doc->nodeAt(m_doc->activeFlatIndex)->transform
                        : QTransform();
    m_history.push(std::make_unique<FilterCommand>(
        m_doc, m_doc->activeFlatIndex,
        std::move(before), beforeT,
        layer->cpuImage.copy(), afterT,
        QString::fromStdString(chain.front().first)));
    emit toolExecuted(QString::fromStdString(chain.front().first), true);
    return true;
}

bool ImageController::executeTool(const std::string& toolName, const JsonMap& params)
{
    if (!m_doc) return false;

    auto get = [&](const std::string& key, double def = 0.0) -> double {
        auto it = params.find(key);
        if (it == params.end()) return def;
        if (std::holds_alternative<double>(it->second))
            return std::get<double>(it->second);
        return def;
    };

    auto getStr = [&](const std::string& key, const std::string& def = "") -> std::string {
        auto it = params.find(key);
        if (it == params.end()) return def;
        if (std::holds_alternative<std::string>(it->second))
            return std::get<std::string>(it->second);
        return def;
    };

    auto getInt = [&](const std::string& key, int def = 0) -> int {
        return static_cast<int>(std::round(get(key, def)));
    };

    auto* layer = activeLayer();
    bool success = true;
    auto shouldUseDocumentProgress = [&]() -> bool {
        if (!m_doc)
            return false;
        const qint64 area = static_cast<qint64>(m_doc->size.width()) * m_doc->size.height();
        return area >= m_doc->perfConfig.autoTileMinArea || m_doc->flatCount() >= 4;
    };
    auto shouldUseLayerProgress = [&](Layer* targetLayer) -> bool {
        if (!m_doc || !targetLayer)
            return false;
        const qint64 area = static_cast<qint64>(targetLayer->cpuImage.width())
                          * targetLayer->cpuImage.height();
        return area >= m_doc->perfConfig.autoTileMinArea;
    };

    static const std::unordered_set<std::string> kDestructiveTools = {
        "fill_layer", "fill_bucket", "delete_selected",
        "adjust_color", "adjust_brightness", "adjust_contrast",
        "adjust_saturation", "adjust_hue",
        "gaussian_blur", "median_blur", "sharpen", "edge_detect",
        "box_blur", "bilateral_blur", "motion_blur", "radial_blur", "zoom_blur",
        "grayscale", "invert_colors", "noise_reduce", "posterize",
        "threshold", "remove_background",
        "rotate", "flip_horizontal", "flip_vertical", "resize_layer", "crop"
    };
    if (toolName == "set_clone_source") {
        emit cloneSourceRequested(QPointF(get("x"), get("y")));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "set_clone_sample_mode") {
        emit cloneSampleModeRequested(std::clamp(getInt("mode", 1), 0, 2));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "set_clone_aligned") {
        emit cloneAlignedRequested(getInt("aligned", 1) != 0);
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "begin_clone_stroke") {
        emit cloneStrokeBeginRequested(QPointF(get("x"), get("y")));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "update_clone_stroke") {
        emit cloneStrokeUpdateRequested(QPointF(get("x"), get("y")));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "end_clone_stroke") {
        emit cloneStrokeEndRequested();
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "set_healing_source") {
        emit healingSourceRequested(QPointF(get("x"), get("y")));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "set_healing_sample_mode") {
        emit healingSampleModeRequested(std::clamp(getInt("mode", 1), 0, 2));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "set_healing_aligned") {
        emit healingAlignedRequested(getInt("aligned", 1) != 0);
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "set_healing_diffusion") {
        emit healingDiffusionRequested(std::clamp(static_cast<float>(get("diffusion", 0.5)), 0.0f, 1.0f));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "begin_healing_stroke") {
        emit healingStrokeBeginRequested(QPointF(get("x"), get("y")));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "update_healing_stroke") {
        emit healingStrokeUpdateRequested(QPointF(get("x"), get("y")));
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    if (toolName == "end_healing_stroke") {
        emit healingStrokeEndRequested();
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }
    // Flip Layer is a transform operation for spatial nodes: a world reflection
    // folded into the node transforms. That keeps horizontal/vertical flips
    // visually correct even when a raster layer is already rotated. The unit
    // flip bypasses the raster-only destructive guard (it edits no pixels);
    // Lock Position is enforced per node inside flipNodesAsUnit.
    const bool isFlipTool =
        (toolName == "flip_horizontal" || toolName == "flip_vertical");
    auto* activeNodeForFlip = m_doc ? m_doc->activeNode() : nullptr;
    const bool flipMultiSelect = m_doc && m_doc->selectedFlatIndices.size() > 1;
    const bool flipActiveGroup = activeNodeForFlip
        && activeNodeForFlip->type == LayerTreeNode::Type::Group;
    const bool flipActiveLayer = activeNodeForFlip
        && activeNodeForFlip->type == LayerTreeNode::Type::Layer
        && layer;
    const bool isUnitFlip = isFlipTool
        && (flipMultiSelect || flipActiveGroup || flipActiveLayer);
    if (layer && kDestructiveTools.count(toolName) && !isUnitFlip
        && !checkDestructiveOp(layer))
        return false;

    // Transform-class tools additionally honour Lock Position (they reposition /
    // reorient / resize the layer's pixels, not just recolour them).
    static const std::unordered_set<std::string> kPositionTools = {
        "rotate", "flip_horizontal", "flip_vertical", "resize_layer"
    };
    // Unit-flip enforces Lock Position per target inside flipNodesAsUnit (a
    // multi-selection may mix locked and unlocked nodes), so it is exempt here.
    if (!isUnitFlip && layer && layer->owner && kPositionTools.count(toolName)
        && layer->owner->isPositionLocked()) {
        emit operationBlocked(tr("This layer's position is locked."));
        emit toolExecuted(QString::fromStdString(toolName), false);
        return false;
    }

    // Mask-mutating tools are blocked only by the master Lock All (Lock Image
    // Pixels deliberately leaves the mask editable). Visibility-only mask tools
    // (toggle/enable/disable) and the read-only mask_copy are intentionally
    // excluded. Targets the same node the tool will act on.
    static const std::unordered_set<std::string> kMaskMutationTools = {
        "layer_mask_add", "layer_mask_remove", "layer_mask_apply",
        "mask_invert", "mask_density", "mask_feather",
        "selection_to_mask", "mask_paste", "mask_clear"
    };
    if (kMaskMutationTools.count(toolName)) {
        int mIdx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        auto* mNode = m_doc ? m_doc->nodeAt(mIdx) : nullptr;
        if (mNode && mNode->isFullyLocked()) {
            emit operationBlocked(tr("This layer is fully locked."));
            emit toolExecuted(QString::fromStdString(toolName), false);
            return false;
        }
    }

    // Generative fill is dispatched asynchronously to an image-gen provider
    // (src/ai/); the result is applied when the provider finishes. Available to
    // the chat agent and MCP for free via this entry point.
    if (toolName == "generative_fill") {
        const std::string m = getStr("mode", "selection");
        GenFillMode mode = (m == "new_layer" || m == "new")
            ? GenFillMode::FillAsNewLayer : GenFillMode::FillSelection;
        double strength = params.count("strength") ? get("strength") : -1.0;
        int steps = params.count("steps") ? getInt("steps") : -1;
        int seed = params.count("seed") ? getInt("seed") : -2;
        generativeFill(QString::fromStdString(getStr("prompt")), mode,
                       QString::fromStdString(getStr("negative_prompt")),
                       strength, steps, seed);
        emit toolExecuted(QString::fromStdString(toolName), true);
        return true;
    }

    auto captureBefore = [&](int idx) -> std::pair<QImage, QTransform> {
        auto* l = layerAtOrWarn(m_doc, idx);
        if (!l) return {};
        auto* n = m_doc->nodeAt(idx);
        return {l->cpuImage.copy(), n ? n->transform : QTransform()};
    };

    auto makeFilterCommand = [&](const QString& name, int idx,
                                 QImage before, QTransform beforeT) -> std::unique_ptr<Command> {
        auto* l = layerAtOrWarn(m_doc, idx);
        if (!l) return nullptr;
        auto* n = m_doc->nodeAt(idx);
        QImage after = l->cpuImage.copy();
        QTransform afterT = n ? n->transform : QTransform();
        return std::make_unique<FilterCommand>(
            m_doc, idx, std::move(before), beforeT,
            std::move(after), afterT, name);
    };

    auto pushFilter = [&](const QString& name, int idx,
                           QImage before, QTransform beforeT) {
        if (auto cmd = makeFilterCommand(name, idx, std::move(before), beforeT))
            m_history.push(std::move(cmd));
    };

    // Mirrors the layer mask in step with a whole-layer flip. Instead of
    // resampling, the mask buffer is mirrored and its origin is reflected about
    // the layer's base bounds, so a mask larger than the base (expanded layers)
    // stays pixel-aligned with the flipped content. Returns the undo command.
    auto mirrorLayerMask = [&](Layer* l, bool horizontal) -> std::unique_ptr<Command> {
        if (!l || l->maskImage.isNull()) return nullptr;
        QImage before = l->maskImage.copy();
        const QPoint beforeOrigin = l->maskOrigin;
        l->maskImage = l->maskImage.mirrored(horizontal, !horizontal);
        if (horizontal)
            l->maskOrigin.setX(l->cpuImage.width() - beforeOrigin.x()
                               - l->maskImage.width());
        else
            l->maskOrigin.setY(l->cpuImage.height() - beforeOrigin.y()
                               - l->maskImage.height());
        l->maskThumbDirty = true;
        syncLayerMaskToGpu(l);
        return std::make_unique<MaskEditCommand>(
            m_doc, m_doc->activeFlatIndex, std::move(before), l->maskImage.copy(),
            tr("Flip Layer Mask"), beforeOrigin, l->maskOrigin);
    };

    // Non-destructive flip path for layers / groups / multi-selections:
    // reflects every target about the shared union-bbox centre so the selection
    // mirrors as one rigid area (and a rotated layer flips the way it looks on
    // screen). Targets the whole layer selection when multi-selected, else the
    // active node. See ImageController::flipNodesAsUnit.
    auto flipAsUnit = [&](bool horizontal) {
        std::vector<int> targets;
        if (m_doc->selectedFlatIndices.size() > 1)
            targets.assign(m_doc->selectedFlatIndices.begin(),
                           m_doc->selectedFlatIndices.end());
        else
            targets.push_back(m_doc->activeFlatIndex);
        flipNodesAsUnit(targets, horizontal);
    };

    bool asyncDispatched = false;
    const bool selectionActive = m_doc->selection.active() && !m_doc->selection.isEmpty();
    const bool canAsyncWithSelection = toolName == "delete_selected";
    // delete_selected with the mask as the edit target clears the MASK (not RGB
    // pixels) within the selection — a cheap edit that must stay on the
    // synchronous path (the async clear only knows how to zero RGBA).
    const bool maskDeleteCase = toolName == "delete_selected" && isEditingMask()
        && layer && !layer->maskImage.isNull() && selectionActive;
    if (layer && isHeavyTool(toolName)
        && (!selectionActive || canAsyncWithSelection)
        && (toolName != "delete_selected" || shouldUseLayerProgress(layer))
        && !maskDeleteCase)
    {
        auto* n = m_doc->nodeAt(m_doc->activeFlatIndex);
        auto* aj = AsyncJobSystem::instance();
        if (n && aj) {
            // Flush rasterStorage tiles into cpuImage so the async job
            // operates on the full positioned image, not a stale/empty cpuImage.
            const bool flushedRaster = layer->rasterStorage.isEnabled();
            if (flushedRaster) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto job = std::make_shared<AsyncJob>();
            job->toolName = toolName;
            job->targetFlatIndex = m_doc->activeFlatIndex;
            job->beforeImage = layer->cpuImage.copy();
            job->beforeTransform = n->transform;
            job->sourceImage = layer->cpuImage.copy();
            job->weakLayer = layer;
            job->retileRasterStorage = flushedRaster;
            if (toolName == "delete_selected") {
                auto countOpaque = [](const QImage& img) -> int {
                    if (img.isNull()) return -1;
                    QImage a = img.convertToFormat(QImage::Format_RGBA8888);
                    int cnt = 0;
                    for (int y = 0; y < a.height(); ++y) {
                        const QRgb* r = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                        for (int x = 0; x < a.width(); ++x)
                            if (qAlpha(r[x]) > 0) ++cnt;
                    }
                    return cnt;
                };
                const int opaqueAfterFlush = countOpaque(layer->cpuImage);
                // For rasterStorage layers the dab lives in per-tile textures,
                // NOT layer->textureId, so syncLayerFromGpu() would overwrite the
                // freshly composited cpuImage with a stale/empty single-layer
                // texture and wipe the dab. Only sync from GPU when we did NOT
                // already composite the pixels from rasterStorage.
                if (!flushedRaster)
                    syncLayerFromGpu(layer);
                const int opaqueAfterSync = countOpaque(layer->cpuImage);
                job->beforeImage = layer->cpuImage.copy();
                job->sourceImage = layer->cpuImage.copy();
                cv::Mat layerMask = makeLayerMask(layer);
                const int maskNonZero = layerMask.empty() ? -1 : cv::countNonZero(layerMask);
                const int maskTotal = layerMask.empty() ? 0 : layerMask.rows * layerMask.cols;
                if (!layerMask.empty())
                    job->maskImage = QImage(layerMask.data, layerMask.cols, layerMask.rows,
                                            static_cast<int>(layerMask.step),
                                            QImage::Format_Grayscale8).copy();
                if (job->maskImage.isNull()) {
                    qWarning() << "[DeleteSel] ASYNC aborting: maskImage is null";
                    return false;
                }
            }

            for (const auto& [k, v] : params) {
                if (std::holds_alternative<double>(v))
                    job->params[QString::fromStdString(k)] = QVariant(std::get<double>(v));
                else if (std::holds_alternative<std::string>(v))
                    job->params[QString::fromStdString(k)] = QVariant(QString::fromStdString(std::get<std::string>(v)));
            }

            // delete_selected must NOT use the progressive tile path: that path
            // runs applyImageEngine() per tile, which has no delete_selected
            // case and ignores job->maskImage — clearing the whole layer (or
            // nothing) instead of just the selected region, with no usable
            // before/after diff for undo. The non-progressive worker applies
            // the selection mask correctly, so keep delete_selected on it.
            if (layer->tiledSystem
                && toolName != "fill_bucket"
                && toolName != "delete_selected"
                && processing::FilterProcessor::isTileable(toolName))
            {
                job->type = AsyncJobType::FilterApplyProgressive;
                QRect dirty = layer->dirtyRegion.boundingRect();
                if (dirty.isEmpty())
                    dirty = QRect(0, 0, layer->cpuImage.width(), layer->cpuImage.height());
                auto tiles = layer->tileManager.visibleTiles(dirty);
                job->tileRects.clear();
                for (auto* t : tiles)
                    if (t) job->tileRects.push_back(t->bounds);
                job->kernelRadius = processing::FilterProcessor::kernelRadius(
                    toolName, job->params);
                job->tileSize = layer->tileManager.tileSize();
                job->batchSize = m_doc->perfConfig.progressiveBatchSize;
                job->totalTiles = static_cast<int>(job->tileRects.size());
                job->viewportCenterX = m_doc->size.width() / 2;
                job->viewportCenterY = m_doc->size.height() / 2;
            }

            uint64_t id = aj->enqueue(job);
            m_pendingAsyncJobs[id] = std::move(job);
            emit progressOperationStarted(id, progressLabelForTool(toolName), true);
            emit progressOperationProgressChanged(id,
                m_pendingAsyncJobs[id]->type == AsyncJobType::FilterApplyProgressive ? 0 : -1);
            success = true;
            asyncDispatched = true;
        }
    }

    if (asyncDispatched)
        return true;

    if (!asyncDispatched) try {
    if (toolName == "new_document") {
        int w = getInt("width", 1024);
        int h = getInt("height", 768);
        m_doc->size = QSize(w, h);
        m_doc->selection.resize(w, h);
        m_doc->roots.clear();
        newLayer();
    }
    else if (toolName == "resize_document" || toolName == "resize_image") {
        ImageResizeOptions resizeOptions;
        resizeOptions.targetSize = QSize(
            getInt("width", m_doc->size.width()),
            getInt("height", m_doc->size.height()));
        resizeOptions.resampleImage = get("resample", 1.0) > 0.5;
        resizeOptions.scaleStyles = get("scale_styles", 1.0) > 0.5;

        const std::string interpolation = getStr("interpolation", "bicubic_automatic");
        if (interpolation == "nearest")
            resizeOptions.interpolation = ResizeInterpolation::Nearest;
        else if (interpolation == "bilinear")
            resizeOptions.interpolation = ResizeInterpolation::Bilinear;
        else if (interpolation == "bicubic")
            resizeOptions.interpolation = ResizeInterpolation::Bicubic;
        else if (interpolation == "lanczos")
            resizeOptions.interpolation = ResizeInterpolation::Lanczos;
        else
            resizeOptions.interpolation = ResizeInterpolation::BicubicAutomatic;

        auto itResolution = params.find("resolution");
        if (itResolution != params.end()
            && std::holds_alternative<double>(itResolution->second)) {
            resizeOptions.updateResolution = true;
            resizeOptions.resolutionDpi = std::max(1.0, std::get<double>(itResolution->second));
        }

        success = shouldUseDocumentProgress()
            ? resizeImageAsync(resizeOptions)
            : resizeImage(resizeOptions);
    }
    else if (toolName == "resize_canvas") {
        CanvasResizeOptions resizeOptions;
        resizeOptions.targetSize = QSize(
            getInt("width", m_doc->size.width()),
            getInt("height", m_doc->size.height()));
        resizeOptions.fillExtension = get("fill_extension", 0.0) > 0.5;

        const std::string anchor = getStr("anchor", "center");
        if (anchor == "top_left")
            resizeOptions.anchor = CanvasAnchor::TopLeft;
        else if (anchor == "top_center")
            resizeOptions.anchor = CanvasAnchor::TopCenter;
        else if (anchor == "top_right")
            resizeOptions.anchor = CanvasAnchor::TopRight;
        else if (anchor == "middle_left")
            resizeOptions.anchor = CanvasAnchor::MiddleLeft;
        else if (anchor == "middle_right")
            resizeOptions.anchor = CanvasAnchor::MiddleRight;
        else if (anchor == "bottom_left")
            resizeOptions.anchor = CanvasAnchor::BottomLeft;
        else if (anchor == "bottom_center")
            resizeOptions.anchor = CanvasAnchor::BottomCenter;
        else if (anchor == "bottom_right")
            resizeOptions.anchor = CanvasAnchor::BottomRight;
        else
            resizeOptions.anchor = CanvasAnchor::Center;

        const std::string colorValue = getStr("extension_color", "");
        if (!colorValue.empty()) {
            QColor color(QString::fromStdString(colorValue));
            if (color.isValid())
                resizeOptions.extensionColor = color;
        }

        success = shouldUseDocumentProgress()
            ? resizeCanvasAsync(resizeOptions)
            : resizeCanvas(resizeOptions);
    }
    else if (toolName == "add_layer") {
        newLayer();
    }
    else if (toolName == "remove_layer") {
        removeSelectedNodes();
    }
    else if (toolName == "duplicate_layer") {
        int idx = getInt("index", m_doc->activeFlatIndex);
        duplicateNode(idx);
    }
    else if (toolName == "set_layer_opacity") {
        int idx = getInt("index", m_doc->activeFlatIndex);
        setNodeOpacity(idx, static_cast<float>(get("opacity", 1.0)));
    }
    else if (toolName == "set_layer_visibility") {
        int idx = getInt("index", m_doc->activeFlatIndex);
        setNodeVisibility(idx, get("visible", 1.0) > 0.5);
    }
    else if (toolName == "set_layer_blend_mode") {
        int idx = getInt("index", m_doc->activeFlatIndex);
        if (idx < 0) idx = m_doc->activeFlatIndex;
        setNodeBlendMode(idx, static_cast<BlendMode>(getInt("mode", 0)));
    }
    else if (toolName == "fill_layer") {
        if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            QColor c = QColor(
                getInt("red", 255),
                getInt("green", 255),
                getInt("blue", 255),
                getInt("alpha", 255));

            if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
                cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
                cv::Mat filled(cvImg.size(), cvImg.type(),
                    cv::Scalar(c.blue(), c.green(), c.red(), c.alpha()));
                cv::Mat layerMask = makeLayerMask(layer);
                if (!layerMask.empty())
                    blendByMask(cvImg, filled, layerMask);  // feather-aware
                else
                    cvImg = filled;
                layer->cpuImage = ImageEngine::toQImage(cvImg);
                markLayerDirty(layer);
                layer->textureOutdated = true;
                syncLayerToGpu(layer);
                emit imageChanged();
            } else {
                fillActiveLayer(c);
            }
            pushFilter("fill_layer", m_doc->activeFlatIndex, before.first, before.second);
        }
    }
    else if (toolName == "fill_bucket") {
        if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            QColor c = QColor(
                getInt("red", 255),
                getInt("green", 255),
                getInt("blue", 255),
                getInt("alpha", 255));
            int x = getInt("x", layer->cpuImage.width() / 2);
            int y = getInt("y", layer->cpuImage.height() / 2);
            float tol = static_cast<float>(get("tolerance", 0.0));
            tol = std::clamp(tol, 0.0f, 1.0f);

            if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
                cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
                cv::Mat filled(cvImg.size(), cvImg.type(),
                    ImageEngine::qColorToScalar(c));
                cv::Mat layerMask = makeLayerMask(layer);
                if (!layerMask.empty())
                    blendByMask(cvImg, filled, layerMask);  // feather-aware
                else
                    cvImg = filled;
                layer->cpuImage = ImageEngine::toQImage(cvImg);
            } else {
                cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
                cv::Mat result = ImageEngine::fillRegion(cvImg, x, y,
                    ImageEngine::qColorToScalar(c), tol);
                layer->cpuImage = ImageEngine::toQImage(result);
            }

            markLayerDirty(layer);
            layer->textureOutdated = true;
            syncLayerToGpu(layer);
            emit imageChanged();
            pushFilter("fill_bucket", m_doc->activeFlatIndex, before.first, before.second);
        }
    }
    else if (toolName == "adjust_color") {
        if (!layer) { success = false; }
        else {
            float brightness = static_cast<float>(get("brightness", 0.0));
            float contrast = static_cast<float>(get("contrast", 0.0));
            float saturation = static_cast<float>(get("saturation", 0.0));
            float hue = static_cast<float>(get("hue", 0.0));
            bool autoContrastEnabled = getInt("auto_contrast", 0) != 0;

            applyFilterWithSelection(layer, toolName,
                [&](const cv::Mat& img) -> cv::Mat {
                    cv::Mat result = img.clone();
                    if (brightness != 0.0f)
                        result = ImageEngine::adjustBrightness(result, brightness);
                    if (contrast != 0.0f)
                        result = ImageEngine::adjustContrast(result, contrast);
                    if (saturation != 0.0f)
                        result = ImageEngine::adjustSaturation(result, saturation);
                    if (hue != 0.0f)
                        result = ImageEngine::adjustHue(result, hue);
                    if (autoContrastEnabled)
                        result = ImageEngine::autoContrast(result);
                    return result;
                });
        }
    }
    else if (toolName == "adjust_brightness" || toolName == "adjust_contrast" ||
             toolName == "adjust_saturation" || toolName == "adjust_hue" ||
             toolName == "gaussian_blur" || toolName == "sharpen" ||
             toolName == "median_blur" || toolName == "edge_detect") {
        if (!layer) { success = false; }
        else if (canApplyTiled(layer, toolName)) {
            applyFilterTiled(layer, toolName, params);
        }
        else {
            applyFilterWithSelection(layer, toolName,
                [&](const cv::Mat& img) -> cv::Mat {
                    if (toolName == "adjust_brightness")
                        return ImageEngine::adjustBrightness(img, static_cast<float>(get("value", 0.0)));
                    else if (toolName == "adjust_contrast")
                        return ImageEngine::adjustContrast(img, static_cast<float>(get("value", 0.0)));
                    else if (toolName == "adjust_saturation")
                        return ImageEngine::adjustSaturation(img, static_cast<float>(get("value", 0.0)));
                    else if (toolName == "adjust_hue")
                        return ImageEngine::adjustHue(img, static_cast<float>(get("value", 0.0)));
                    else if (toolName == "gaussian_blur")
                        return ImageEngine::gaussianBlur(img, static_cast<float>(get("radius", 3.0)));
                    else if (toolName == "sharpen")
                        return ImageEngine::sharpen(img, static_cast<float>(get("strength", 1.0)));
                    else if (toolName == "median_blur")
                        return ImageEngine::medianBlur(img, getInt("kernel_size", 5));
                    else if (toolName == "edge_detect")
                        return ImageEngine::edgeDetect(img,
                            static_cast<float>(get("threshold1", 50.0)),
                            static_cast<float>(get("threshold2", 150.0)));
                    return img;
                });
        }
    }
    else if (toolName == "box_blur" || toolName == "bilateral_blur" ||
             toolName == "motion_blur" || toolName == "radial_blur" ||
             toolName == "zoom_blur") {
        // Sync fallback path: reached when a selection is active or the layer is
        // not tiled (the no-selection async path is handled above). radial/zoom
        // are non-tileable globals, so they always land here when sync.
        if (!layer) { success = false; }
        else if (canApplyTiled(layer, toolName)) {
            applyFilterTiled(layer, toolName, params);
        }
        else {
            applyFilterWithSelection(layer, toolName,
                [&](const cv::Mat& img) -> cv::Mat {
                    if (toolName == "box_blur")
                        return ImageEngine::boxBlur(img, getInt("radius", 3));
                    else if (toolName == "bilateral_blur")
                        return ImageEngine::bilateralBlur(img, getInt("diameter", 9),
                            get("sigma_color", 75.0), get("sigma_space", 75.0));
                    else if (toolName == "motion_blur")
                        return ImageEngine::motionBlur(img, getInt("length", 15),
                            get("angle", 0.0));
                    else if (toolName == "radial_blur")
                        return ImageEngine::radialBlur(img, get("amount", 0.25),
                            get("center_x", 0.5), get("center_y", 0.5),
                            getInt("samples", 12));
                    else if (toolName == "zoom_blur")
                        return ImageEngine::zoomBlur(img, get("amount", 0.25),
                            get("center_x", 0.5), get("center_y", 0.5),
                            getInt("samples", 12));
                    return img;
                });
        }
    }
    else if (toolName == "crop") {
        if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
            cv::Mat result = ImageEngine::crop(cvImg,
                getInt("x", 0), getInt("y", 0),
                getInt("width", cvImg.cols), getInt("height", cvImg.rows));
            if (!result.empty()) {
                layer->cpuImage = ImageEngine::toQImage(result);
                markLayerDirty(layer);
                syncLayerToGpu(layer);
                emit imageChanged();
                pushFilter("crop", m_doc->activeFlatIndex, before.first, before.second);
            }
        }
    }
    else if (toolName == "rotate") {
        if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
            cv::Mat result = ImageEngine::rotate(cvImg, static_cast<float>(get("angle", 0.0)));
            if (!result.empty()) {
                layer->cpuImage = ImageEngine::toQImage(result);
                markLayerDirty(layer);
                syncLayerToGpu(layer);
                emit imageChanged();
                pushFilter("rotate", m_doc->activeFlatIndex, before.first, before.second);
            }
        }
    }
    else if (toolName == "flip_horizontal") {
        if (isUnitFlip) {
            flipAsUnit(true);   // layer / group / multi-select -> unit transform
        }
        else if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
            cv::Mat result = ImageEngine::flipHorizontal(cvImg);
            std::unique_ptr<Command> maskCmd;
            if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
                cv::Mat layerMask = makeLayerMask(layer);
                if (!layerMask.empty())
                    blendByMask(cvImg, result, layerMask);  // feather-aware
                else
                    cvImg = result;
                layer->cpuImage = ImageEngine::toQImage(cvImg);
            } else {
                layer->cpuImage = ImageEngine::toQImage(result);
                // Whole-layer flip: the layer mask flips with the content.
                maskCmd = mirrorLayerMask(layer, true);
            }
            if (layer->tiledSystem)
                layer->enableTiling(layer->tileManager.tileSize());
            markLayerDirty(layer);
            layer->textureOutdated = true;
            syncLayerToGpu(layer);
            emit imageChanged();
            if (maskCmd) {
                auto comp = std::make_unique<CompositeCommand>(tr("Flip Horizontal"));
                if (auto fc = makeFilterCommand("flip_horizontal", m_doc->activeFlatIndex,
                                                before.first, before.second))
                    comp->add(std::move(fc));
                comp->add(std::move(maskCmd));
                m_history.push(std::move(comp));
            } else {
                pushFilter("flip_horizontal", m_doc->activeFlatIndex, before.first, before.second);
            }
        }
    }
    else if (toolName == "flip_vertical") {
        if (isUnitFlip) {
            flipAsUnit(false);   // layer / group / multi-select -> unit transform
        }
        else if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
            cv::Mat result = ImageEngine::flipVertical(cvImg);
            std::unique_ptr<Command> maskCmd;
            if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
                cv::Mat layerMask = makeLayerMask(layer);
                if (!layerMask.empty())
                    blendByMask(cvImg, result, layerMask);  // feather-aware
                else
                    cvImg = result;
                layer->cpuImage = ImageEngine::toQImage(cvImg);
            } else {
                layer->cpuImage = ImageEngine::toQImage(result);
                // Whole-layer flip: the layer mask flips with the content.
                maskCmd = mirrorLayerMask(layer, false);
            }
            if (layer->tiledSystem)
                layer->enableTiling(layer->tileManager.tileSize());
            markLayerDirty(layer);
            layer->textureOutdated = true;
            syncLayerToGpu(layer);
            emit imageChanged();
            if (maskCmd) {
                auto comp = std::make_unique<CompositeCommand>(tr("Flip Vertical"));
                if (auto fc = makeFilterCommand("flip_vertical", m_doc->activeFlatIndex,
                                                before.first, before.second))
                    comp->add(std::move(fc));
                comp->add(std::move(maskCmd));
                m_history.push(std::move(comp));
            } else {
                pushFilter("flip_vertical", m_doc->activeFlatIndex, before.first, before.second);
            }
        }
    }
    else if (toolName == "resize_layer") {
        if (!layer) { success = false; }
        else {
            if (layer->rasterStorage.isEnabled()) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            }
            auto before = captureBefore(m_doc->activeFlatIndex);
            int oldW = layer->cpuImage.width();
            int oldH = layer->cpuImage.height();
            int w = getInt("width", oldW);
            int h = getInt("height", oldH);
            cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
            cv::Mat result = ImageEngine::resize(cvImg, w, h);
            layer->cpuImage = ImageEngine::toQImage(result);
            float sx = static_cast<float>(w) / oldW;
            float sy = static_cast<float>(h) / oldH;
            auto* resizeNode = m_doc->nodeAt(m_doc->activeFlatIndex);
            if (resizeNode) resizeNode->transform.scale(sx, sy);
            if (layer->tiledSystem) {
                layer->enableTiling(layer->tileManager.tileSize());
            }
            markLayerDirty(layer);
            syncLayerToGpu(layer);
            emit imageChanged();
            pushFilter("resize_layer", m_doc->activeFlatIndex, before.first, before.second);
        }
    }
    else if (toolName == "grayscale" || toolName == "invert_colors" ||
             toolName == "auto_contrast" || toolName == "noise_reduce" ||
             toolName == "posterize" || toolName == "threshold" ||
             toolName == "remove_background") {
        if (!layer) { success = false; }
        else if (canApplyTiled(layer, toolName)) {
            applyFilterTiled(layer, toolName, params);
        }
        else {
            applyFilterWithSelection(layer, toolName,
                [&](const cv::Mat& img) -> cv::Mat {
                    if (toolName == "grayscale")
                        return ImageEngine::grayscale(img);
                    else if (toolName == "invert_colors")
                        return ImageEngine::invertColors(img);
                    else if (toolName == "auto_contrast")
                        return ImageEngine::autoContrast(img);
                    else if (toolName == "noise_reduce")
                        return ImageEngine::noiseReduce(img, static_cast<float>(get("strength", 2)));
                    else if (toolName == "posterize")
                        return ImageEngine::posterize(img, getInt("levels", 8));
                    else if (toolName == "threshold")
                        return ImageEngine::threshold(img, get("value", 128));
                    else if (toolName == "remove_background")
                        return ImageEngine::removeBackground(img);
                    return img;
                });
        }
    }
    else if (toolName == "merge_visible") {
        if (m_doc && m_doc->flatCount() >= 2) {
            if (anyFullyLockedLayer(/*visibleOnly=*/true)) {
                emit operationBlocked(tr("A visible layer is fully locked."));
                success = false;
            } else {
                success = mergeVisibleLayersAsync();
            }
        } else { success = false; }
    }
    else if (toolName == "merge_down") {
        int src = m_doc ? m_doc->activeFlatIndex : -1;
        // The layer below src is not necessarily src+1: src may own clipped
        // adjustment children that occupy the following flat slots. Resolve the
        // next real layer below src, in src's own container (mergeDownTargetFlat
        // never crosses a group boundary).
        int dst = (src >= 0) ? mergeDownTargetFlat(src) : -1;
        if (m_doc && src >= 0 && dst >= 0) {
            auto* sn = m_doc->nodeAt(src);
            auto* dn = m_doc->nodeAt(dst);
            auto effectivelyVisible = [](const LayerTreeNode* n) {
                for (const LayerTreeNode* p = n; p; p = p->parent)
                    if (!p->visible) return false;
                return n != nullptr;
            };
            if ((sn && sn->isPixelEditingLocked()) || (dn && dn->isPixelEditingLocked())) {
                emit operationBlocked(tr("One of the layers being merged is locked."));
                success = false;
            } else if (!effectivelyVisible(sn) || !effectivelyVisible(dn)) {
                // A hidden endpoint contributes nothing to the composite; the
                // merge would silently destroy its pixels.
                emit operationBlocked(tr("Hidden layers cannot be merged."));
                success = false;
            } else {
                success = mergeLayersAsync(src, dst);
            }
        } else { success = false; }
    }
    else if (toolName == "merge_layers") {
        if (anyFullyLockedLayer(/*visibleOnly=*/true)) {
            emit operationBlocked(tr("A visible layer is fully locked."));
            success = false;
        } else {
            success = mergeVisibleLayersAsync();
        }
    }
    else if (toolName == "flatten_image") {
        if (anyFullyLockedLayer(/*visibleOnly=*/false)) {
            emit operationBlocked(tr("A layer is fully locked; unable to flatten."));
            success = false;
        } else if (shouldUseDocumentProgress())
            success = flattenImageAsync();
        else
            flattenImage();
    }
    else if (toolName == "rasterize_layer") {
        int idx = m_doc ? m_doc->activeFlatIndex : -1;
        if (idx >= 0) {
            rasterizeNode(idx);
        } else { success = false; }
    }
    else if (toolName == "layer_mask_add") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) addLayerMask(idx);
        else success = false;
    }
    else if (toolName == "layer_mask_remove") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) removeLayerMask(idx);
        else success = false;
    }
    else if (toolName == "layer_mask_apply") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) {
            applyLayerMask(idx);
            syncLayerToGpu(layerAtOrWarn(m_doc, idx));
        } else { success = false; }
    }
    else if (toolName == "layer_mask_toggle") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) toggleLayerMask(idx);
        else success = false;
    }
    else if (toolName == "layer_mask_disable") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) setLayerMaskEnabled(idx, false);
        else success = false;
    }
    else if (toolName == "layer_mask_enable") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) setLayerMaskEnabled(idx, true);
        else success = false;
    }
    else if (toolName == "mask_invert") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) invertLayerMask(idx);
        else success = false;
    }
    else if (toolName == "mask_density") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        float density = static_cast<float>(get("density", 1.0));
        if (idx >= 0 && idx < fc) setMaskDensity(idx, density);
        else success = false;
    }
    else if (toolName == "mask_feather") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        float radius = static_cast<float>(get("radius", 5.0));
        if (idx >= 0 && idx < fc) setMaskFeather(idx, radius);
        else success = false;
    }
    else if (toolName == "selection_to_mask") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) createMaskFromSelection(idx);
        else success = false;
    }
    else if (toolName == "mask_copy") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        if (idx >= 0) copyLayerMask(idx);
        else success = false;
    }
    else if (toolName == "mask_paste") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        if (idx >= 0) pasteLayerMask(idx);
        else success = false;
    }
    else if (toolName == "mask_clear") {
        int idx = getInt("index", m_doc ? m_doc->activeFlatIndex : -1);
        int fc = m_doc ? m_doc->flatCount() : 0;
        if (idx >= 0 && idx < fc) clearLayerMask(idx);
        else success = false;
    }
    else if (toolName == "zoom") {
        m_doc->zoom = std::clamp(static_cast<float>(get("level", 1.0f)), 0.01f, 100.0f);
        emit imageChanged();
    }
    else if (toolName == "reset_view") {
        m_doc->zoom = 1.0f;
        m_doc->panOffset = QPointF(0, 0);
        emit imageChanged();
    }
    else if (toolName == "select_all") {
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.clear();
        m_doc->selection.setRect(QRectF(0, 0, m_doc->size.width(), m_doc->size.height()),
                                  SelectMode::Replace);
        m_doc->selection.setActive(true);
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_all"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "deselect") {
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.clear();
        m_doc->selection.setActive(false);
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "deselect"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_invert") {
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.invert();
        m_doc->selection.setActive(true);
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_invert"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_rect") {
        int x = getInt("x", 0);
        int y = getInt("y", 0);
        int w = getInt("width", m_doc->size.width());
        int h = getInt("height", m_doc->size.height());
        SelectMode mode = static_cast<SelectMode>(getInt("mode", 0));
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        if (mode == SelectMode::Replace)
            m_doc->selection.clear();
        m_doc->selection.setRect(QRectF(x, y, w, h), mode);
        m_doc->selection.setActive(true);
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_rect"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_magic_wand") {
        // x,y are document coords — same convention as select_rect; the
        // doc→layer mapping for transformed layers happens here/inside
        // setMagicWand, never at the caller.
        int x = getInt("x", 0);
        int y = getInt("y", 0);
        float tolerance = static_cast<float>(get("tolerance", 32.0));
        bool contiguous = getInt("contiguous", 1) != 0;
        SelectMode mode = static_cast<SelectMode>(getInt("mode", 0));

        auto* layer = m_doc->activeLayer();
        // rasterStorage layers keep the truth in tiles; cpuImage is stale.
        QImage srcImage = layer ? layer->compositeImage() : QImage();
        if (srcImage.isNull()) { success = false; }
        else {
            int lw = srcImage.width();
            int lh = srcImage.height();
            double dw = static_cast<double>(m_doc->size.width());
            double dh = static_cast<double>(m_doc->size.height());

            auto* magicNode = m_doc->nodeAt(m_doc->activeFlatIndex);
            const QTransform t = magicNode ? magicNode->accumulatedTransform() : QTransform();
            bool hasTransform = (t.m11() != 1.0 || t.m22() != 1.0 ||
                                 t.m31() != 0.0 || t.m32() != 0.0 ||
                                 t.m12() != 0.0 || t.m21() != 0.0);

            // Validate the seed before the Replace-clear below: a rejected
            // click must not turn into an accidental deselect. (The
            // transformed path clamps its seed, so it can't be rejected.)
            if (!hasTransform
                && (x < 0 || y < 0
                    || x >= std::min(m_doc->size.width(), lw)
                    || y >= std::min(m_doc->size.height(), lh))) {
                success = false;
            }
            else {
            QImage before = m_doc->selection.image().copy();
            bool beforeActive = m_doc->selection.active();

            if (mode == SelectMode::Replace)
                m_doc->selection.clear();

            if (hasTransform) {
                int sx = std::clamp(x, 0, m_doc->size.width() - 1);
                int sy = std::clamp(y, 0, m_doc->size.height() - 1);

                QTransform invT = t.inverted();
                double invAff[6] = {
                    lw * invT.m11() / dw,
                    -lw * invT.m21() / dh,
                    lw * 0.5 * (1.0 - invT.m11() + invT.m21() + invT.m31()),
                    -lh * invT.m12() / dw,
                    lh * invT.m22() / dh,
                    lh * 0.5 * (1.0 + invT.m12() - invT.m22() - invT.m32())
                };

                m_doc->selection.setMagicWand(sx, sy, tolerance, contiguous, mode,
                    srcImage.constBits(), lw, lh, invAff);
            } else {
                m_doc->selection.setMagicWand(x, y, tolerance, contiguous, mode,
                    srcImage.constBits(), lw, lh);
            }

            m_doc->selection.setActive(!m_doc->selection.isEmpty());

            // Skip the no-op command when the click changed nothing (e.g. a
            // rejected seed) instead of polluting the history.
            QImage after = m_doc->selection.image().copy();
            if (after != before || m_doc->selection.active() != beforeActive) {
                m_history.push(std::make_unique<SelectionCommand>(
                    m_doc, before, std::move(after),
                    beforeActive, m_doc->selection.active(),
                    "select_magic_wand"));
            }
            emit selectionChanged();
            emit imageChanged();
            }
        }
    }
    else if (toolName == "select_feather") {
        float radius = static_cast<float>(get("radius", 5.0));
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.feather(radius);
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_feather"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_grow") {
        int pixels = getInt("pixels", 5);
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.grow(pixels);
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_grow"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_shrink") {
        int pixels = getInt("pixels", 5);
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.shrink(pixels);
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_shrink"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_border") {
        int pixels = getInt("pixels", 5);
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.border(pixels);
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_border"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "select_smooth") {
        float radius = static_cast<float>(get("radius", 3.0));
        QImage before = m_doc->selection.image().copy();
        bool beforeActive = m_doc->selection.active();
        m_doc->selection.smooth(radius);
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
        m_history.push(std::make_unique<SelectionCommand>(
            m_doc, before, m_doc->selection.image().copy(),
            beforeActive, m_doc->selection.active(),
            "select_smooth"));
        emit selectionChanged();
        emit imageChanged();
    }
    else if (toolName == "copy") {
        copy();
    }
    else if (toolName == "paste") {
        paste();
    }
    else if (toolName == "crop_to_selection") {
        if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
            QRectF selB = m_doc->selection.bounds();
            int cropX = static_cast<int>(selB.x());
            int cropY = static_cast<int>(selB.y());
            int cropW = static_cast<int>(selB.width());
            int cropH = static_cast<int>(selB.height());
            if (cropW <= 0 || cropH <= 0) { success = false; }
            else {
                const QRect cropRect(cropX, cropY, cropW, cropH);
                if (shouldUseDocumentProgress())
                    success = cropDocumentAsync(cropRect);
                else
                    cropDocument(cropRect);
            }
        } else { success = false; }
    }
    else if (toolName == "select_save_channel") {
        QString name = QString::fromStdString(getStr("name", "Channel"));
        if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
            QImage before = m_doc->selection.image().copy();
            bool beforeActive = m_doc->selection.active();
            m_doc->saveSelectionToChannel(name);
            m_history.push(std::make_unique<SelectionCommand>(
                m_doc, before, m_doc->selection.image().copy(),
                beforeActive, m_doc->selection.active(),
                "select_save_channel"));
            emit selectionChanged();
            emit imageChanged();
        } else { success = false; }
    }
    else if (toolName == "select_load_channel") {
        int index = getInt("index", 0);
        SelectMode mode = static_cast<SelectMode>(getInt("mode", 0));
        if (index >= 0 && index < static_cast<int>(m_doc->channels.size())) {
            QImage before = m_doc->selection.image().copy();
            bool beforeActive = m_doc->selection.active();
            m_doc->loadChannelToSelection(index, mode);
            m_history.push(std::make_unique<SelectionCommand>(
                m_doc, before, m_doc->selection.image().copy(),
                beforeActive, m_doc->selection.active(),
                "select_load_channel"));
            emit selectionChanged();
            emit imageChanged();
        }
    }
    else if (toolName == "float_selection") {
        bool cut = getInt("cut", 0) != 0;
        if (!m_doc->selection.active() || m_doc->selection.isEmpty()) {
            success = false;
        } else {
            int dw = m_doc->size.width();
            int dh = m_doc->size.height();

            auto* layer = m_doc->activeLayer();
            cv::Mat cropped;
            QPointF floatDocPos;

            if (layer && !layer->cpuImage.isNull()) {
                auto* node = m_doc->nodeAt(m_doc->activeFlatIndex);
                QTransform accumT = node ? node->accumulatedTransform() : QTransform();
                const bool rotatedOrSheared = accumT.m12() != 0.0 || accumT.m21() != 0.0;

                if (rotatedOrSheared) {
                    // Layer-space AABB extraction cannot represent a rotation;
                    // bake the transform by extracting in doc space.
                    if (!extractSelectedDocRegion(layer, cropped, floatDocPos))
                        success = false;
                } else {
                    cv::Mat layerMask = makeLayerMask(layer);
                    if (layerMask.empty()) { success = false; }
                    else {
                        cv::Mat layerImg = ImageEngine::toCvMat(layer->cpuImage);
                        cv::Mat masked = cv::Mat::zeros(layerImg.size(), layerImg.type());
                        layerImg.copyTo(masked, layerMask);

                        cv::Rect layerBounds = cv::boundingRect(layerMask);
                        if (layerBounds.width <= 0 || layerBounds.height <= 0) { success = false; }
                        else {
                            cropped = masked(layerBounds).clone();

                            double lw = static_cast<double>(layer->cpuImage.width());
                            double lh = static_cast<double>(layer->cpuImage.height());
                            double a00 = dw * accumT.m11() / lw;
                            double a01 = -dw * accumT.m21() / lh;
                            double a02 = dw * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31());
                            double a10 = -dh * accumT.m12() / lw;
                            double a11 = dh * accumT.m22() / lh;
                            double a12 = dh * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32());
                            double dx = a00 * layerBounds.x + a01 * layerBounds.y + a02;
                            double dy = a10 * layerBounds.x + a11 * layerBounds.y + a12;
                            floatDocPos = QPointF(dx, dy);
                        }
                    }
                }
            } else {
                auto flat = m_doc->flatten();
                std::vector<cv::Mat> mats;
                std::vector<float> ops;
                std::vector<bool> vis;
                for (int i = static_cast<int>(flat.size()) - 1; i >= 0; --i) {
                    auto* n = flat[i];
                    if (!n->layer || !n->visible) continue;
                    auto* l = n->layer.get();
                    int lw = l->cpuImage.width();
                    int lh = l->cpuImage.height();
                    if (lw <= 0 || lh <= 0) continue;

                    QTransform accumT = n->accumulatedTransform();
                    double a00 = dw * accumT.m11() / lw;
                    double a01 = -dw * accumT.m21() / lh;
                    double a02 = dw * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31());
                    double a10 = -dh * accumT.m12() / lw;
                    double a11 = dh * accumT.m22() / lh;
                    double a12 = dh * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32());
                    cv::Mat xform = (cv::Mat_<double>(2,3) << a00, a01, a02, a10, a11, a12);

                    cv::Mat layerMat = ImageEngine::toCvMat(l->cpuImage);
                    cv::Mat canvas(dh, dw, CV_8UC4, cv::Scalar(0, 0, 0, 0));
                    cv::warpAffine(layerMat, canvas, xform, canvas.size(),
                                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
                    mats.push_back(std::move(canvas));
                    ops.push_back(n->opacity);
                    vis.push_back(true);
                }

                QImage srcImage = ImageEngine::toQImage(
                    ImageEngine::compositeLayers(mats, ops, vis, m_doc->size));

                cv::Mat full = ImageEngine::toCvMat(srcImage);
                cv::Mat mask(dh, dw, CV_8UC1,
                             const_cast<uchar*>(m_doc->selection.constBits()),
                             static_cast<size_t>(m_doc->selection.image().bytesPerLine()));
                cv::Mat masked = cv::Mat::zeros(dh, dw, full.type());
                full.copyTo(masked, mask);

                QRectF selBounds = m_doc->selection.bounds();
                cv::Rect cropRect(static_cast<int>(selBounds.x()),
                                  static_cast<int>(selBounds.y()),
                                  static_cast<int>(selBounds.width()),
                                  static_cast<int>(selBounds.height()));
                cropRect &= cv::Rect(0, 0, dw, dh);
                cropped = masked(cropRect).clone();
                floatDocPos = QPointF(static_cast<float>(cropRect.x),
                                      static_cast<float>(cropRect.y));
            }

            if (cropped.empty()) { success = false; }
            else {

                auto comp = std::make_unique<CompositeCommand>("float_selection");

                int flatIdx = m_doc->activeFlatIndex;
                auto* floatSrcNode = m_doc->nodeAt(flatIdx);
                if (cut && layer) {
                    QImage beforeImg = layer->cpuImage.copy();
                    QTransform beforeT = floatSrcNode ? floatSrcNode->transform : QTransform();

                    cv::Mat layerImg = ImageEngine::toCvMat(layer->cpuImage);
                    cv::Mat layerMask = makeLayerMask(layer);
                    if (!layerMask.empty())
                        layerImg.setTo(cv::Scalar(0, 0, 0, 0), layerMask);

                    layer->cpuImage = ImageEngine::toQImage(layerImg);
                    markLayerDirty(layer);
                    syncLayerToGpu(layer);

                    comp->add(std::make_unique<FilterCommand>(
                        m_doc, flatIdx,
                        std::move(beforeImg), beforeT,
                        layer->cpuImage.copy(), floatSrcNode ? floatSrcNode->transform : QTransform(),
                        "float_cut_original"));
                }

                auto newNode = std::make_unique<LayerTreeNode>();
                newNode->type = LayerTreeNode::Type::Layer;
                newNode->name = QString("Floated Selection");
                newNode->layer = std::make_shared<Layer>();
                newNode->layer->name = newNode->name;
                newNode->layer->cpuImage = ImageEngine::toQImage(cropped);
                newNode->layer->owner = newNode.get();

                // Place the floated pixels at their original document
                // position (same translate+scale convention as paste()).
                {
                    const int imgW = newNode->layer->cpuImage.width();
                    const int imgH = newNode->layer->cpuImage.height();
                    const float fdw = static_cast<float>(dw);
                    const float fdh = static_cast<float>(dh);
                    const float centerX = static_cast<float>(floatDocPos.x()) + imgW * 0.5f;
                    const float centerY = static_cast<float>(floatDocPos.y()) + imgH * 0.5f;
                    QTransform t;
                    t.translate(2.0f * centerX / fdw - 1.0f,
                                1.0f - 2.0f * centerY / fdh);
                    t.scale(static_cast<float>(imgW) / fdw,
                            static_cast<float>(imgH) / fdh);
                    newNode->transform = t;
                }
                newNode->layer->resetTransform = newNode->transform;
                newNode->layer->hasResetTransform = true;

                int insertIdx = std::max(0, flatIdx);
                comp->add(std::make_unique<AddLayerCommand>(
                    m_doc, insertIdx, std::move(newNode), "float_add_layer"));

                m_history.push(std::move(comp));
                emit layerChanged(m_doc->activeFlatIndex);
                emit activeLayerChanged(m_doc->activeFlatIndex);
                emit imageChanged();
            }
        }
    }
    else if (toolName == "delete_selected") {
        auto* layer = m_doc->activeLayer();
        if (!layer || !m_doc->selection.active() || m_doc->selection.isEmpty()) {
            if (!layer)
                qWarning() << "delete_selected: no active layer (flatIndex=" << m_doc->activeFlatIndex << ")";
            if (!m_doc->selection.active())
                qWarning() << "delete_selected: selection not active";
            if (m_doc->selection.isEmpty())
                qWarning() << "delete_selected: selection is empty";
            success = false;
        } else if (maskDeleteCase) {
            // Mask is the edit target: Delete clears the MASK (black = hides)
            // within the selection, mapped into mask space — RGB pixels stay
            // intact. Undoable as a single mask edit.
            QImage before = layer->maskImage.copy();
            const QPoint beforeOrigin = layer->maskOrigin;
            cv::Mat coverage = selectionInMaskSpace(layer);
            if (coverage.empty()) {
                success = false;
            } else {
                cv::Mat maskMat(layer->maskImage.height(),
                                layer->maskImage.width(), CV_8UC1,
                                layer->maskImage.bits(),
                                static_cast<size_t>(layer->maskImage.bytesPerLine()));
                maskMat.setTo(0, coverage);   // coverage>0 → black (hidden)
                layer->maskThumbDirty = true;
                if (layer->owner) layer->owner->invalidateEffects();
                syncLayerMaskToGpu(layer);
                m_history.push(std::make_unique<MaskEditCommand>(
                    m_doc, m_doc->activeFlatIndex, std::move(before),
                    layer->maskImage.copy(), tr("Delete Mask Selection"),
                    beforeOrigin, layer->maskOrigin));
                emit layerChanged(m_doc->activeFlatIndex);
                emit imageChanged();
                if (m_doc) ++m_doc->compositionGeneration;
            }
        } else {
            auto* delNode = m_doc->nodeAt(m_doc->activeFlatIndex);

            // Make cpuImage authoritative BEFORE capturing the undo snapshot.
            // rasterStorage layers keep their pixels in per-tile textures, so
            // composite them into cpuImage; non-raster layers may have pending
            // GPU-FBO paint, so read that back. Doing this first means `before`
            // reflects the real pixels (fixes broken undo) and the masked clear
            // operates on the actual dab instead of a stale/empty image.
            const bool flushedRaster = layer->rasterStorage.isEnabled();
            if (flushedRaster) {
                layer->cpuImage = layer->compositeImage();
                layer->rasterStorage.clear();
                layer->textureOutdated = true;
            } else {
                syncLayerFromGpu(layer);
            }

            QImage before = layer->cpuImage.copy();
            QTransform beforeT = delNode ? delNode->transform : QTransform();

            cv::Mat cvImg = ImageEngine::toCvMat(layer->cpuImage);
            cv::Mat layerMask = makeLayerMask(layer);
            const int maskNonZero = layerMask.empty() ? -1 : cv::countNonZero(layerMask);
            const int maskTotal = layerMask.empty() ? 0 : layerMask.rows * layerMask.cols;
            if (!layerMask.empty()) {
                cvImg.setTo(cv::Scalar(0, 0, 0, 0), layerMask);
            } else {
                qWarning() << "[DeleteSel] SYNC: mask EMPTY -> nothing cleared";
            }

            layer->cpuImage = ImageEngine::toQImage(cvImg);
            // Keep the dab-layer representation: the flush above flattened the
            // tiles only so the masked clear could run on the composited pixels.
            // Re-tile from the result so the layer stays a rasterStorage layer —
            // otherwise the transform outline stops tracking the valid-pixel
            // bounds (visualFrameForNode only uses contentImageBounds for
            // rasterStorage layers) and snaps to the full layer base.
            if (flushedRaster)
                layer->replaceRasterStorageWithImage(layer->cpuImage);
            markLayerDirty(layer);
            if (delNode) delNode->invalidateEffects();
            syncLayerToGpu(layer);
            emit imageChanged();

            m_history.push(std::make_unique<FilterCommand>(
                m_doc, m_doc->activeFlatIndex,
                std::move(before), beforeT,
                layer->cpuImage.copy(), delNode ? delNode->transform : QTransform(),
                "delete_selected"));
        }
    }
    else if (toolName == "create_text") {
        QString text = QString::fromStdString(getStr("text", "Text"));

        TextBox tbox;
        tbox.width = static_cast<float>(get("width", 0.0));
        tbox.height = static_cast<float>(get("height", 0.0));

        QPointF canvasPos(static_cast<qreal>(get("x", 0.0)),
                          static_cast<qreal>(get("y", 0.0)));

        float size = static_cast<float>(get("size", 32.0));
        if (size < 8.0f) size = 32.0f;

        createTextLayer(text, tbox, canvasPos, size);
    }
    else if (toolName == "edit_text") {
        int idx = getInt("index", m_doc->activeFlatIndex);
        auto* node = m_doc->nodeAt(idx);
        if (!node || !node->layer || !node->layer->textData) {
            success = false;
        } else {
            TextLayerData newData = *node->layer->textData;
            newData.text = QString::fromStdString(
                getStr("text", newData.text.toStdString()));
            newData.dirty = true;
            updateTextLayer(idx, newData);
        }
    }
    else if (toolName == "set_text_style") {
        int idx = getInt("index", m_doc->activeFlatIndex);
        auto* node = m_doc->nodeAt(idx);
        if (!node || !node->layer || !node->layer->textData) {
            success = false;
        } else {
            TextSpan style;
            style.start = getInt("start", 0);
            style.end = getInt("end",
                static_cast<int>(node->layer->textData->text.size()));

            float sz = static_cast<float>(get("size", 0.0));
            if (sz > 0.0f)
                style.fontSize = sz;
            else
                style.fontSize = node->layer->textData->spans.empty()
                    ? 32.0f : node->layer->textData->spans.back().fontSize;

            style.bold = getInt("bold", 0) > 0;
            style.italic = getInt("italic", 0) > 0;

            bool toSelection = (get("start", -1.0) >= 0);
            applyTextStyle(idx, style, toSelection);
        }
    }
    else {
        success = false;
    }

    } catch (const std::exception& e) {
        qWarning() << "executeTool: exception in" << QString::fromStdString(toolName) << ":" << e.what();
        success = false;
    } catch (...) {
        qWarning() << "executeTool: unknown exception in" << QString::fromStdString(toolName);
        success = false;
    }

    emit toolExecuted(QString::fromStdString(toolName), success);
    return success;
}
