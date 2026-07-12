#include "ImageController.hpp"
#include "core/AdjustmentTypes.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "engine/ImageEngine.hpp"
#include "Commands.hpp"
#include "animation/AnimationCommands.hpp"
#include "text/TextRenderer.hpp"
#include "engine/ShapeRenderer.hpp"
#include "shape/ShapeCommands.hpp"
#include "shape/ShapeLayerUpdater.hpp"
#include "shape/ShapePresetFactory.hpp"
#include "transform/TransformController.hpp"
#include "gradient/GradientCommand.hpp"
#include "gradient/GradientRenderer.hpp"
#include "async/AsyncJobSystem.hpp"
#include "processing/FilterProcessor.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"
#include "io/ImageIO.hpp"
#include "color/ColorManagementService.hpp"
#include "core/SolidColorData.hpp"
#include "core/AppPaths.hpp"
#include "ai/ImageGenProvider.hpp"
#include "ai/ImageGenProviderFactory.hpp"
#include "ai/InpaintTypes.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/upscale/RealEsrganProcessBackend.hpp"
#include "ai/upscale/UpscaleService.hpp"

#include <QImage>
#include <QPainter>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <GL/gl.h>
#include <QDebug>
#include <QSet>
#include <algorithm>
#include <cstring>
#include <limits>
#include <functional>
#include <set>
#include <exception>
#include <stdexcept>
#include <cmath>
#include <variant>
#include <memory>
#include <optional>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

static QImage flushLayerToCpuImage(Layer* layer);
static ResizeDocumentState captureResizeState(const Document* doc);
static ResizeDocumentState captureResizeStateShared(const Document* doc);
static Document documentFromResizeState(const ResizeDocumentState& state);
static void markDocumentStateDirty(Document* doc);
static bool applyResizeImageState(Document* doc, const ImageResizeOptions& options);
static bool applyResizeCanvasState(Document* doc, const CanvasResizeOptions& options);
static bool applyCropDocumentState(Document* doc, const QRect& cropRect);
static bool applyMergeLayersState(Document* doc, int srcFlat, int dstFlat);
static bool applyMergeVisibleState(Document* doc);
static bool applyFlattenImageState(Document* doc);
static bool nodeIsDescendantOf(const LayerTreeNode* node, const LayerTreeNode* ancestor);
static bool nodeEffectivelyVisible(const LayerTreeNode* node);
static void convertDocumentContentToProfile(Document* doc,
                                            const ColorProfile& source,
                                            const ColorProfile& destination,
                                            const ColorConversionOptions& options);

ImageController::ImageController(QObject* parent)
    : QObject(parent)
    , m_upscaleService(std::make_unique<UpscaleService>())
{
    connect(&m_playbackController, &anim::PlaybackController::frameRequested,
            this, &ImageController::setCurrentFrame);

    m_history.setChangeCallback([this]() {
        emit historyChanged();
    });

    if (auto* aj = AsyncJobSystem::instance()) {
        connect(aj, &AsyncJobSystem::jobCompleted,
                this, &ImageController::onAsyncJobCompleted);
        connect(aj, &AsyncJobSystem::progressiveBatch,
                this, &ImageController::onProgressiveBatch);
        connect(aj, &AsyncJobSystem::jobProgressChanged,
                this, [this](uint64_t jobId, int progress) {
            if (m_pendingAsyncJobs.find(jobId) != m_pendingAsyncJobs.end())
                emit progressOperationProgressChanged(jobId, progress);
        });
        connect(aj, &AsyncJobSystem::jobStatusChanged,
                this, [this](uint64_t jobId, const QString& status) {
            if (m_pendingAsyncJobs.find(jobId) != m_pendingAsyncJobs.end())
                emit progressOperationMessageChanged(jobId, status);
        });
    }

    connect(m_upscaleService.get(), &UpscaleService::jobProgress,
            this, [this](UpscaleJobId jobId, int percent, const QString& message) {
        if (m_pendingUpscaleJobs.find(jobId) == m_pendingUpscaleJobs.end())
            return;
        emit progressOperationMessageChanged(jobId, message);
        emit progressOperationProgressChanged(jobId, percent);
    });
    connect(m_upscaleService.get(), &UpscaleService::jobFinished,
            this, [this](UpscaleJobId jobId, const UpscaleJobResult& result) {
        auto pending = m_pendingUpscaleJobs.find(jobId);
        if (pending == m_pendingUpscaleJobs.end())
            return;
        const UpscaleOptions options = pending->second;
        m_pendingUpscaleJobs.erase(pending);

        if (result.cancelled) {
            m_pendingUpscaleLayers.erase(jobId);
            emit progressOperationCanceled(jobId);
            return;
        }
        if (!result.ok || result.image.isNull()) {
            m_pendingUpscaleLayers.erase(jobId);
            QString message = result.error.userMessage.isEmpty()
                ? tr("AI Upscale failed.") : result.error.userMessage;
            if (!result.error.technicalDetails.trimmed().isEmpty())
                message += tr("\n\nDetails:\n%1").arg(result.error.technicalDetails.trimmed());
            if (!result.error.logPath.trimmed().isEmpty())
                message += tr("\n\nDiagnostic log:\n%1").arg(result.error.logPath.trimmed());
            else if (!result.logPath.trimmed().isEmpty())
                message += tr("\n\nDiagnostic log:\n%1").arg(result.logPath.trimmed());
            emit progressOperationFailed(jobId, message);
            return;
        }

        emit progressOperationMessageChanged(jobId, tr("Applying result..."));
        const int layerIndex = m_pendingUpscaleLayers.count(jobId) ? m_pendingUpscaleLayers[jobId] : -1;
        m_pendingUpscaleLayers.erase(jobId);

        if (options.target == UpscaleTarget::CurrentDocument) {
            if (options.output == UpscaleOutputMode::NewDocument) {
                emit upscaleNewDocumentReady(result.image,
                    m_doc ? tr("%1 - Upscaled %2x").arg(m_doc->name).arg(options.scale)
                          : tr("Upscaled %1x").arg(options.scale),
                    result.colorProfile,
                    result.profileSource);
                emit progressOperationFinished(jobId);
                return;
            }
            emit progressOperationFailed(jobId,
                tr("Replace Document is not enabled yet. Use New Document for AI Upscale."));
            return;
        }

        auto* node = m_doc ? m_doc->nodeAt(layerIndex) : nullptr;
        auto* layer = node && node->type == LayerTreeNode::Type::Layer ? node->layer.get() : nullptr;
        if (!node || !layer) {
            emit progressOperationFailed(jobId, tr("The target layer is no longer available."));
            return;
        }

        const QImage beforeImage = layer->cpuImage.copy();
        const QTransform beforeTransform = node->transform();
        const QImage beforeMask = layer->maskImage.copy();
        const QPoint beforeMaskOrigin = layer->maskOrigin;
        const QImage afterMask = (options.preserveLayerMask && !beforeMask.isNull())
            ? beforeMask.scaled(beforeMask.width() * options.scale,
                                beforeMask.height() * options.scale,
                                Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8)
            : beforeMask;
        const QPoint afterMaskOrigin = options.preserveLayerMask
            ? QPoint(beforeMaskOrigin.x() * options.scale, beforeMaskOrigin.y() * options.scale)
            : beforeMaskOrigin;

        auto scaledFromTopLeft = [](const QTransform& t, double scale) {
            const QPointF a = t.map(QPointF(-1.0, 1.0));
            const QPointF b = t.map(QPointF(1.0, 1.0));
            const QPointF c = t.map(QPointF(-1.0, -1.0));
            const QPointF nb = a + (b - a) * scale;
            const QPointF nc = a + (c - a) * scale;
            const QPointF lx = (nb - a) * 0.5;
            const QPointF ly = (a - nc) * 0.5;
            const QPointF tr = a + lx - ly;
            QTransform out;
            out.setMatrix(lx.x(), lx.y(), 0.0,
                          ly.x(), ly.y(), 0.0,
                          tr.x(), tr.y(), 1.0);
            return out;
        };
        const QTransform afterTransform = scaledFromTopLeft(node->transform(), options.scale);

        if (options.output == UpscaleOutputMode::NewLayer) {
            auto newNode = std::make_unique<LayerTreeNode>();
            newNode->type = LayerTreeNode::Type::Layer;
            newNode->name = tr("%1 - Upscaled %2x").arg(node->name).arg(options.scale);
            newNode->layer = std::make_shared<Layer>();
            newNode->layer->name = newNode->name;
            newNode->layer->cpuImage = result.image.convertToFormat(QImage::Format_RGBA8888);
            newNode->layer->maskImage = afterMask;
            newNode->layer->maskRawImage = afterMask;
            newNode->layer->maskOrigin = afterMaskOrigin;
            newNode->layer->maskVisible = layer->maskVisible;
            newNode->layer->maskDensity = layer->maskDensity;
            newNode->layer->maskFeather = layer->maskFeather;
            newNode->layer->owner = newNode.get();
            newNode->setBaseOpacity(node->opacity());
            newNode->setBaseVisible(true);
            newNode->setBaseBlendMode(node->blendMode());
            newNode->setBaseTransform(afterTransform);
            newNode->layer->resetTransform = afterTransform;
            newNode->layer->hasResetTransform = true;
            if (m_doc && newNode->layer->cpuImage.width() * newNode->layer->cpuImage.height()
                >= m_doc->perfConfig.autoTileMinArea) {
                newNode->layer->enableTiling(m_doc->tileSize());
            } else {
                newNode->layer->disableTiling();
            }

            const int insertAt = layerIndex;
            const int newIndex = m_doc->insertNodeAt(insertAt, std::move(newNode));
            m_doc->activeFlatIndex = newIndex;
            m_doc->selectedFlatIndices = {newIndex};
            auto* inserted = m_doc->nodeAt(newIndex);
            m_history.push(std::make_unique<AddLayerCommand>(
                m_doc, newIndex, inserted ? inserted->clone() : nullptr, tr("AI Upscale Layer")));
            ++m_doc->compositionGeneration;
            emit layerChanged(newIndex);
            emit activeLayerChanged(newIndex);
            emit imageChanged();
            emit progressOperationProgressChanged(jobId, 100);
            emit progressOperationFinished(jobId);
            return;
        }

        layer->rasterStorage.clear();
        layer->cpuImage = result.image.convertToFormat(QImage::Format_RGBA8888);
        layer->maskImage = afterMask;
        layer->maskRawImage = afterMask;
        layer->maskOrigin = afterMaskOrigin;
        layer->textureOutdated = true;
        layer->pendingGpuUpload = true;
        layer->maskTextureOutdated = true;
        layer->maskThumbDirty = true;
        if (m_doc && layer->cpuImage.width() * layer->cpuImage.height()
            >= m_doc->perfConfig.autoTileMinArea) {
            layer->enableTiling(m_doc->tileSize());
        } else {
            layer->disableTiling();
        }
        node->setBaseTransform(afterTransform);
        node->thumbnailDirty = true;
        node->invalidateEffects();
        ++m_doc->compositionGeneration;

        auto comp = std::make_unique<CompositeCommand>(tr("AI Upscale Layer"));
        comp->add(std::make_unique<FilterCommand>(
            m_doc, layerIndex, beforeImage, beforeTransform,
            layer->cpuImage.copy(), afterTransform, tr("AI Upscale Layer")));
        if (!beforeMask.isNull() || !afterMask.isNull()) {
            comp->add(std::make_unique<MaskEditCommand>(
                m_doc, layerIndex, beforeMask, afterMask, tr("AI Upscale Layer Mask"),
                beforeMaskOrigin, afterMaskOrigin));
        }
        m_history.push(std::move(comp));
        emit layerChanged(layerIndex);
        emit activeLayerChanged(layerIndex);
        emit imageChanged();
        emit progressOperationProgressChanged(jobId, 100);
        emit progressOperationFinished(jobId);
    });
    connect(m_upscaleService.get(), &UpscaleService::jobFailed,
            this, [this](UpscaleJobId jobId, const UpscaleError& error) {
        if (m_pendingUpscaleJobs.find(jobId) == m_pendingUpscaleJobs.end())
            return;
        m_pendingUpscaleJobs.erase(jobId);
        m_pendingUpscaleLayers.erase(jobId);
        QString message = error.userMessage.isEmpty() ? tr("AI Upscale failed.") : error.userMessage;
        if (!error.technicalDetails.trimmed().isEmpty())
            message += tr("\n\nDetails:\n%1").arg(error.technicalDetails.trimmed());
        if (!error.logPath.trimmed().isEmpty())
            message += tr("\n\nDiagnostic log:\n%1").arg(error.logPath.trimmed());
        emit progressOperationFailed(jobId, message);
    });
}

ImageController::~ImageController()
{
    flushGpuChanges();
}

void ImageController::setDocument(Document* doc)
{
    if (auto* aj = AsyncJobSystem::instance())
        aj->cancelAll();
    m_pendingAsyncJobs.clear();
    m_doc = doc;
    m_history.clear();
    m_propertyController.setContext(m_doc, &m_history);
    m_playbackController.setDocument(m_doc);
    if (m_doc && !m_doc->size.isNull())
        m_doc->selection.resize(m_doc->size.width(), m_doc->size.height());
}

void ImageController::cancelLongOperation(uint64_t jobId)
{
    if (jobId != 0 && jobId == m_genJobId && m_genProvider) {
        m_genProvider->cancel();        // onGenerativeFailed(Canceled) follows
        return;
    }

    auto docIt = m_pendingDocumentOperations.find(jobId);
    if (docIt != m_pendingDocumentOperations.end()) {
        if (docIt->second)
            docIt->second->store(true);
        m_pendingDocumentOperations.erase(docIt);
        emit progressOperationCanceled(jobId);
        return;
    }

    auto upscaleIt = m_pendingUpscaleJobs.find(jobId);
    if (upscaleIt != m_pendingUpscaleJobs.end()) {
        if (m_upscaleService)
            m_upscaleService->cancel(jobId);
        return;
    }

    auto it = m_pendingAsyncJobs.find(jobId);
    if (it == m_pendingAsyncJobs.end())
        return;

    auto job = it->second;
    job->cancelled = true;

    auto* node = m_doc ? m_doc->nodeAt(job->targetFlatIndex) : nullptr;
    if (node && node->layer && node->layer.get() == job->weakLayer
        && !job->beforeImage.isNull()) {
        auto* layer = node->layer.get();
        layer->cpuImage = job->beforeImage.copy();
        markLayerDirty(layer);
        layer->textureOutdated = true;
        syncLayerToGpu(layer);
        emit imageChanged();
    }

    if (auto* aj = AsyncJobSystem::instance())
        aj->cancelForLayer(job->weakLayer);

    m_pendingAsyncJobs.erase(it);
    emit progressOperationCanceled(jobId);
}

struct DocumentStateOperationResult {
    bool ok = false;
    bool canceled = false;
    QString error;
    std::shared_ptr<ResizeDocumentState> after;
};

bool ImageController::runDocumentStateOperationAsync(
    const QString& progressMessage,
    const QString& undoName,
    std::function<bool(Document&)> operation)
{
    if (!m_doc || !operation)
        return false;

    if (auto* aj = AsyncJobSystem::instance())
        aj->cancelAll();
    m_pendingAsyncJobs.clear();

    const uint64_t jobId = m_nextDocumentOperationId++;
    // Cheap COW snapshot on the UI thread. The worker below turns it into an
    // independent localDoc (deep) and captures the "after" state — all the heavy
    // pixel copying happens off the UI thread, so a large document doesn't freeze
    // while recording undo state.
    auto before = std::make_shared<ResizeDocumentState>(captureResizeStateShared(m_doc));
    auto canceled = std::make_shared<std::atomic_bool>(false);
    m_pendingDocumentOperations[jobId] = canceled;

    emit progressOperationStarted(jobId, progressMessage, true);
    emit progressOperationProgressChanged(jobId, -1);

    auto* watcher = new QFutureWatcher<DocumentStateOperationResult>(this);
    connect(watcher, &QFutureWatcher<DocumentStateOperationResult>::finished,
            this, [this, watcher, jobId, before, canceled, undoName]() {
        DocumentStateOperationResult result;
        // watcher->result() rethrows if the future stored an exception; never let
        // that escape into the event loop (it would std::terminate the app).
        try {
            result = watcher->result();
        } catch (const std::exception& e) {
            result.ok = false;
            result.error = QString::fromUtf8(e.what());
        } catch (...) {
            result.ok = false;
        }
        watcher->deleteLater();

        const bool wasPending = m_pendingDocumentOperations.find(jobId)
            != m_pendingDocumentOperations.end();
        if (!wasPending)
            return;
        const bool alreadyCanceled = canceled->load();
        m_pendingDocumentOperations.erase(jobId);

        if (alreadyCanceled || result.canceled) {
            emit progressOperationCanceled(jobId);
            return;
        }

        if (!result.ok || !result.after) {
            emit progressOperationFailed(jobId,
                result.error.isEmpty() ? tr("The operation failed.") : result.error);
            return;
        }

        // Applying the result deep-clones the (possibly huge) document snapshot on
        // the UI thread, which can itself run out of memory. Catch that so a large
        // document fails gracefully instead of crashing the whole app.
        try {
            auto cmd = std::make_unique<ResizeDocumentCommand>(
                m_doc, std::move(*before), std::move(*result.after), undoName);
            cmd->execute();
            m_history.push(std::move(cmd));
            markDocumentStateDirty(m_doc);
        } catch (const std::bad_alloc&) {
            emit progressOperationFailed(jobId,
                tr("Not enough memory to complete the operation on a document this large."));
            return;
        } catch (const std::exception& e) {
            emit progressOperationFailed(jobId, QString::fromUtf8(e.what()));
            return;
        } catch (...) {
            emit progressOperationFailed(jobId, tr("The operation failed."));
            return;
        }

        emit layerChanged(m_doc ? m_doc->activeFlatIndex : -1);
        emit activeLayerChanged(m_doc ? m_doc->activeFlatIndex : -1);
        emit imageChanged();
        emit documentChanged();
        emit progressOperationProgressChanged(jobId, 100);
        emit progressOperationFinished(jobId);
    });

    watcher->setFuture(QtConcurrent::run([before, canceled, operation = std::move(operation)]() mutable {
        DocumentStateOperationResult result;
        if (canceled->load()) {
            result.canceled = true;
            return result;
        }

        // Heavy CPU compositing (layer styles = OpenCV blur, full-canvas buffers)
        // can exhaust memory on very large documents. A non-QException that escapes
        // a QtConcurrent functor calls std::terminate() and hard-crashes the app
        // (taking the console with it), so translate every throw — std::bad_alloc,
        // cv::Exception (both derive from std::exception), anything — into a
        // graceful failure the UI can report.
        try {
            Document localDoc = documentFromResizeState(*before);
            result.ok = operation(localDoc);
            if (canceled->load()) {
                result.canceled = true;
                return result;
            }
            if (result.ok)
                result.after = std::make_shared<ResizeDocumentState>(captureResizeState(&localDoc));
        } catch (const std::bad_alloc&) {
            result.ok = false;
            result.after.reset();
            result.error = QObject::tr(
                "Not enough memory to complete the operation on a document this large.");
        } catch (const std::exception& e) {
            result.ok = false;
            result.after.reset();
            result.error = QString::fromUtf8(e.what());
        } catch (...) {
            result.ok = false;
            result.after.reset();
            result.error = QObject::tr("The operation failed unexpectedly.");
        }
        return result;
    }));

    return true;
}

int ImageController::mergeDownTargetFlat(int srcFlat) const
{
    if (!m_doc)
        return -1;
    auto flat = m_doc->flatten();
    if (srcFlat < 0 || srcFlat >= static_cast<int>(flat.size()))
        return -1;
    auto* src = flat[srcFlat];
    if (!src)
        return -1;
    // Merge Down never crosses a container boundary: the target is the first
    // SIBLING below src (same parent). Anything with a different parent — src's
    // own clipped adjustments, the contents of a sibling group, layers outside
    // src's group — is not a candidate. The first sibling decides the outcome:
    // a pixel/shape/text Layer is the target; an Adjustment sibling is skipped
    // (a stack adjustment still applies to the merged result, it is not
    // consumed); a Group cannot receive a merge, so the action is unavailable.
    for (int i = srcFlat + 1; i < static_cast<int>(flat.size()); ++i) {
        auto* n = flat[i];
        if (!n || n->parent != src->parent)
            continue;
        if (n->type == LayerTreeNode::Type::Adjustment)
            continue;
        if (n->type == LayerTreeNode::Type::Layer && n->layer)
            return i;
        return -1;
    }
    return -1;
}

Layer* ImageController::activeLayer() const
{
    return m_doc ? m_doc->activeLayer() : nullptr;
}

// ----- Helpers to work with tree -----

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
    // Adjustment nodes carry a Layer too (mask storage + coordinate space),
    // so mask operations addressed by flat index work on them as well.
    if (node->type != LayerTreeNode::Type::Layer
        && node->type != LayerTreeNode::Type::Adjustment) {
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

static ShapeData normalizedShapeData(ShapeData data)
{
    data.style.strokeWidth = std::max(0.0, data.style.strokeWidth);
    return data;
}

static bool shapeHasGeometry(const ShapeData& data)
{
    constexpr qreal kMinShapeExtent = 0.001;
    QRectF bounds = ShapeRenderer::shapeBounds(data);
    if (data.path.commands.empty())
        return false;
    if (data.path.closed)
        return bounds.width() >= kMinShapeExtent && bounds.height() >= kMinShapeExtent;
    return bounds.width() >= kMinShapeExtent || bounds.height() >= kMinShapeExtent;
}

static QTransform shapeLayerTransform(const ShapeData& data,
                                      const QImage& rendered,
                                      const QSize& canvasSize)
{
    return ShapeLayerUpdater::rasterTransformForShape(data, rendered, canvasSize);
}

static QString nextShapeLayerName(const Document* doc, const ShapeData& data)
{
    const QString base = data.autoName();
    int maxSuffix = 0;

    if (doc) {
        for (auto* node : doc->flatten()) {
            if (!node || !node->layer || !node->layer->shapeData) continue;
            const QString name = node->name.isEmpty() ? node->layer->name : node->name;
            if (name == base) {
                maxSuffix = std::max(maxSuffix, 1);
            } else if (name.startsWith(base + QLatin1Char(' '))) {
                bool ok = false;
                const int suffix = name.mid(base.size() + 1).toInt(&ok);
                if (ok) maxSuffix = std::max(maxSuffix, suffix);
            }
        }
    }

    return QStringLiteral("%1 %2").arg(base).arg(maxSuffix + 1);
}

static ShapeData transformShapeData(const ShapeData& data,
                                    const QTransform& transform,
                                    const QPointF& canvasToPixelScale)
{
    ShapeData out = data;
    out.transform.localToCanvas = data.transform.localToCanvas * transform;

    // A parametric rounded rectangle must keep its options-bar radius after a
    // (possibly non-uniform) transform instead of letting the affine distort or
    // scale the baked arcs. Rebuild only the local control points against the
    // new transform; the stored radius remains the user-defined value.
    if (ShapePresetFactory::isParametricRoundedRect(out)) {
        out.path = ShapePresetFactory::roundedRectPathFor(
            data, out.transform.localToCanvas, canvasToPixelScale);
    }

    return normalizedShapeData(out);
}

// (docW/2, docH/2): canvas [-1,1] space spans the whole document, so this scale
// converts canvas units to document pixels (used to keep rounded corners
// circular in pixels regardless of the document's aspect ratio).
static QPointF canvasToPixelScaleFor(const QSize& documentSize)
{
    return QPointF(std::max(1, documentSize.width()) * 0.5,
                   std::max(1, documentSize.height()) * 0.5);
}

static void stampResetTransform(LayerTreeNode* node)
{
    if (!node || !node->layer) return;
    node->layer->resetTransform = node->transform();
    node->layer->hasResetTransform = true;
}

static void stampResetTransformRecursive(LayerTreeNode* node)
{
    if (!node) return;
    stampResetTransform(node);
    for (auto& child : node->children)
        stampResetTransformRecursive(child.get());
}

static cv::InterpolationFlags toCvInterpolation(ResizeInterpolation interpolation)
{
    switch (interpolation) {
    case ResizeInterpolation::Nearest:
        return cv::INTER_NEAREST;
    case ResizeInterpolation::Bilinear:
        return cv::INTER_LINEAR;
    case ResizeInterpolation::Bicubic:
    case ResizeInterpolation::BicubicAutomatic:
        return cv::INTER_CUBIC;
    case ResizeInterpolation::Lanczos:
        return cv::INTER_LANCZOS4;
    }
    return cv::INTER_CUBIC;
}

static QImage resampleRgbaImage(const QImage& source,
                                const QSize& targetSize,
                                ResizeInterpolation interpolation)
{
    if (source.isNull() || !targetSize.isValid() || targetSize.isEmpty())
        return source;
    if (source.size() == targetSize)
        return source.copy();

    cv::Mat src = ImageEngine::toCvMat(source);
    cv::Mat dst;
    cv::resize(src, dst,
               cv::Size(targetSize.width(), targetSize.height()),
               0.0, 0.0, toCvInterpolation(interpolation));
    if (dst.empty())
        return source.copy();
    return ImageEngine::toQImage(dst).convertToFormat(QImage::Format_RGBA8888);
}

static QImage resampleMaskImage(const QImage& source,
                                const QSize& targetSize,
                                ResizeInterpolation interpolation)
{
    if (source.isNull() || !targetSize.isValid() || targetSize.isEmpty())
        return source;
    if (source.size() == targetSize)
        return source.copy();

    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat src(gray.height(), gray.width(), CV_8UC1,
                const_cast<uchar*>(gray.constBits()),
                static_cast<size_t>(gray.bytesPerLine()));
    cv::Mat dst;
    cv::resize(src, dst,
               cv::Size(targetSize.width(), targetSize.height()),
               0.0, 0.0, toCvInterpolation(interpolation));
    if (dst.empty())
        return source.copy();

    QImage out(targetSize, QImage::Format_Grayscale8);
    for (int y = 0; y < targetSize.height(); ++y) {
        std::copy(dst.ptr<uchar>(y),
                  dst.ptr<uchar>(y) + targetSize.width(),
                  out.scanLine(y));
    }
    return out;
}

static QRect layerMaskBounds(const Layer* layer)
{
    if (!layer)
        return QRect();
    return layer->maskTargetBounds();
}

static QImage copyMaskIntoBounds(const QImage& source,
                                 const QPoint& sourceOrigin,
                                 const QRect& targetBounds)
{
    if (targetBounds.isEmpty())
        return QImage();

    QImage out(targetBounds.size(), QImage::Format_Grayscale8);
    out.fill(255);
    if (source.isNull())
        return out;

    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(sourceOrigin - targetBounds.topLeft(), gray);
    p.end();
    return out;
}

static double styleScaleFactor(double sx, double sy)
{
    if (sx <= 0.0 || sy <= 0.0)
        return 1.0;
    return std::sqrt(sx * sy);
}

static void scaleStyleParams(QVariantMap& params,
                             const QString& effectType,
                             double factor)
{
    if (factor <= 0.0 || std::abs(factor - 1.0) < 0.000001)
        return;

    auto scaleInt = [&](const QString& key) {
        const int current = params.value(key).toInt();
        params[key] = std::max(0, static_cast<int>(std::round(current * factor)));
    };
    auto scaleDouble = [&](const QString& key) {
        const double current = params.value(key).toDouble();
        params[key] = std::max(0.0, current * factor);
    };

    if (effectType == QLatin1String("drop_shadow")) {
        scaleInt(QStringLiteral("distance"));
        scaleInt(QStringLiteral("spread"));
        scaleDouble(QStringLiteral("blur"));
    } else if (effectType == QLatin1String("inner_shadow")) {
        scaleInt(QStringLiteral("distance"));
        scaleDouble(QStringLiteral("blur"));
    } else if (effectType == QLatin1String("stroke")) {
        scaleInt(QStringLiteral("size"));
    } else if (effectType == QLatin1String("outer_glow")) {
        scaleInt(QStringLiteral("spread"));
        scaleDouble(QStringLiteral("blur"));
    } else if (effectType == QLatin1String("inner_glow")) {
        scaleDouble(QStringLiteral("blur"));
    }
}

static QTransform oldDocToNewDocNoScale(const QSize& oldSize,
                                        const QSize& newSize,
                                        int offsetX,
                                        int offsetY)
{
    const double oldW = std::max(1, oldSize.width());
    const double oldH = std::max(1, oldSize.height());
    const double newW = std::max(1, newSize.width());
    const double newH = std::max(1, newSize.height());

    const double sx = oldW / newW;
    const double sy = oldH / newH;
    const double tx = sx - 1.0 + (2.0 * static_cast<double>(offsetX) / newW);
    const double ty = 1.0 - sy - (2.0 * static_cast<double>(offsetY) / newH);

    return QTransform(
        sx, 0.0, 0.0,
        0.0, sy, 0.0,
        tx, ty, 1.0);
}

static int centeredDeltaComponent(int delta)
{
    return static_cast<int>(std::floor(static_cast<double>(delta) * 0.5));
}

static QPoint anchorOffsetPx(CanvasAnchor anchor,
                             const QSize& oldSize,
                             const QSize& newSize)
{
    const int deltaW = newSize.width() - oldSize.width();
    const int deltaH = newSize.height() - oldSize.height();

    switch (anchor) {
    case CanvasAnchor::TopLeft:
        return QPoint(0, 0);
    case CanvasAnchor::TopCenter:
        return QPoint(centeredDeltaComponent(deltaW), 0);
    case CanvasAnchor::TopRight:
        return QPoint(deltaW, 0);
    case CanvasAnchor::MiddleLeft:
        return QPoint(0, centeredDeltaComponent(deltaH));
    case CanvasAnchor::Center:
        return QPoint(centeredDeltaComponent(deltaW), centeredDeltaComponent(deltaH));
    case CanvasAnchor::MiddleRight:
        return QPoint(deltaW, centeredDeltaComponent(deltaH));
    case CanvasAnchor::BottomLeft:
        return QPoint(0, deltaH);
    case CanvasAnchor::BottomCenter:
        return QPoint(centeredDeltaComponent(deltaW), deltaH);
    case CanvasAnchor::BottomRight:
        return QPoint(deltaW, deltaH);
    }
    return QPoint(0, 0);
}

static QImage translateMaskToCanvas(const QImage& source,
                                    const QSize& targetSize,
                                    const QPoint& offset)
{
    QImage out(targetSize, QImage::Format_Grayscale8);
    out.fill(0);
    if (source.isNull())
        return out;

    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(offset, source);
    p.end();
    return out;
}

static bool canvasExpanded(const QSize& oldSize, const QSize& newSize)
{
    return newSize.width() > oldSize.width() || newSize.height() > oldSize.height();
}

static QImage buildCanvasExtensionFill(const QSize& newSize,
                                       const QRect& oldRectInNewCanvas,
                                       const QColor& color)
{
    if (!newSize.isValid() || newSize.isEmpty())
        return {};

    QImage fill(newSize, QImage::Format_RGBA8888);
    fill.fill(color);

    QPainter p(&fill);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(oldRectInNewCanvas, Qt::transparent);
    p.end();

    return fill;
}

static std::vector<std::unique_ptr<LayerTreeNode>> cloneRootsSnapshot(
    const std::vector<std::unique_ptr<LayerTreeNode>>& roots)
{
    std::vector<std::unique_ptr<LayerTreeNode>> clones;
    clones.reserve(roots.size());
    for (const auto& root : roots) {
        if (root)
            clones.push_back(root->clone());
    }
    return clones;
}

static ResizeDocumentState captureResizeState(const Document* doc)
{
    ResizeDocumentState snapshot;
    if (!doc)
        return snapshot;
    snapshot.size = doc->size;
    snapshot.resolutionDpi = doc->resolutionDpi;
    snapshot.colorMode = doc->colorMode;
    snapshot.bitDepth = doc->bitDepth;
    snapshot.roots = cloneRootsSnapshot(doc->roots);
    snapshot.activeFlatIndex = doc->activeFlatIndex;
    snapshot.selectedFlatIndices = doc->selectedFlatIndices;
    snapshot.selectionMask = doc->selection.image().copy();
    snapshot.selectionActive = doc->selection.active();
    snapshot.guides = doc->guideManager.guides();
    return snapshot;
}

// COW variant of captureResizeState: shares layer pixel buffers (shallowClone)
// instead of deep-copying them, so it is cheap to build on the UI thread. Used
// for the async document-operation "before" snapshot — the heavy deep clones
// (localDoc reconstruction + the "after" state) then run on the worker thread,
// so a large document no longer freezes the UI just to record the undo state.
// Correct because nothing writes m_doc's buffers during the async op (it runs on
// a separate localDoc); any later in-place edit detaches via copy-on-write,
// leaving this snapshot's view intact.
static ResizeDocumentState captureResizeStateShared(const Document* doc)
{
    ResizeDocumentState snapshot;
    if (!doc)
        return snapshot;
    snapshot.size = doc->size;
    snapshot.resolutionDpi = doc->resolutionDpi;
    snapshot.colorMode = doc->colorMode;
    snapshot.bitDepth = doc->bitDepth;
    snapshot.roots.reserve(doc->roots.size());
    for (const auto& r : doc->roots)
        if (r) snapshot.roots.push_back(r->shallowClone());
    snapshot.activeFlatIndex = doc->activeFlatIndex;
    snapshot.selectedFlatIndices = doc->selectedFlatIndices;
    snapshot.selectionMask = doc->selection.image().copy();
    snapshot.selectionActive = doc->selection.active();
    snapshot.guides = doc->guideManager.guides();
    return snapshot;
}

static Document documentFromResizeState(const ResizeDocumentState& state)
{
    Document doc;
    doc.size = state.size;
    doc.resolutionDpi = state.resolutionDpi;
    doc.colorMode = state.colorMode;
    doc.bitDepth = state.bitDepth;
    doc.roots = cloneRootsSnapshot(state.roots);
    doc.activeFlatIndex = state.activeFlatIndex;
    doc.selectedFlatIndices = state.selectedFlatIndices;
    doc.selection.resize(state.size.width(), state.size.height());
    doc.selection.image() = state.selectionMask.copy();
    doc.selection.setActive(state.selectionActive);
    doc.guideManager.setGuides(state.guides);
    return doc;
}

static void markDocumentStateDirty(Document* doc)
{
    if (!doc)
        return;
    auto flat = doc->flatten();
    for (auto* node : flat) {
        if (!node)
            continue;
        node->thumbnailDirty = true;
        node->sourceDirty = true;
        node->invalidateEffects();
        if (node->layer) {
            node->layer->textureOutdated = true;
            node->layer->pendingGpuUpload = true;
        }
    }
    ++doc->compositionGeneration;
}

static QImage convertImageToProfile(const QImage& image,
                                    const ColorProfile& source,
                                    const ColorProfile& destination,
                                    const ColorConversionOptions& options)
{
    if (image.isNull() || !source.isValid() || !destination.isValid())
        return image;

    DocumentImage documentImage = convertQImageToDocumentImageValue(image, QString(), QString());
    documentImage.colorProfile = source;
    documentImage.iccProfile = source.iccBytes();

    const DocumentImage converted = ColorManagementService::instance().convertPixels(
        documentImage, source, destination, options);
    QImage out = convertDocumentImageToQImage(converted);
    return out.isNull() ? image : out.convertToFormat(QImage::Format_RGBA8888);
}

static QColor convertDocumentColor(const QColor& color,
                                   const ColorProfile& source,
                                   const ColorProfile& destination,
                                   const ColorConversionOptions& options)
{
    return ColorManagementService::instance().convertColor(color, source, destination, options);
}

static QVariant convertColorVariant(const QVariant& value,
                                    const ColorProfile& source,
                                    const ColorProfile& destination,
                                    const ColorConversionOptions& options)
{
    QColor color;
    if (value.canConvert<QColor>())
        color = value.value<QColor>();
    if (!color.isValid())
        color = QColor(value.toString());
    if (!color.isValid())
        return value;
    return convertDocumentColor(color, source, destination, options);
}

static void convertEffectColors(std::vector<LayerEffect>& effects,
                                const ColorProfile& source,
                                const ColorProfile& destination,
                                const ColorConversionOptions& options)
{
    static const QStringList colorKeys = {
        QStringLiteral("color"),
        QStringLiteral("startColor"),
        QStringLiteral("endColor")
    };

    for (LayerEffect& effect : effects) {
        for (const QString& key : colorKeys) {
            if (effect.params.contains(key))
                effect.params[key] = convertColorVariant(effect.params.value(key), source, destination, options);
            if (effect.defaultParams.contains(key))
                effect.defaultParams[key] = convertColorVariant(effect.defaultParams.value(key), source, destination, options);
        }
    }
}

static void convertAdjustmentExplicitColors(LayerTreeNode* node,
                                            const ColorProfile& source,
                                            const ColorProfile& destination,
                                            const ColorConversionOptions& options)
{
    if (!node || !node->adjustment)
        return;

    if (node->adjustment->type == QLatin1String("solidcolor")) {
        solidcolor::SolidColorData data =
            solidcolor::SolidColorData::fromParams(node->adjustment->params);
        data.color = convertDocumentColor(data.color, source, destination, options);
        node->adjustment->params = data.toParams();
    }
}

static void convertLayerContentToProfile(LayerTreeNode* node,
                                         const QSize& documentSize,
                                         const ColorProfile& source,
                                         const ColorProfile& destination,
                                         const ColorConversionOptions& options)
{
    if (!node)
        return;

    convertEffectColors(node->effects, source, destination, options);
    convertAdjustmentExplicitColors(node, source, destination, options);

    Layer* layer = node->layer.get();
    if (!layer)
        return;

    if (layer->isTextLayer() && layer->textData) {
        for (TextSpan& span : layer->textData->spans)
            span.color = convertDocumentColor(span.color, source, destination, options);
        TextRenderer renderer;
        renderer.render(*layer->textData, layer->cpuImage);
    } else if (layer->isShapeLayer() && layer->shapeData) {
        layer->shapeData->style.fillColor =
            convertDocumentColor(layer->shapeData->style.fillColor, source, destination, options);
        layer->shapeData->style.strokeColor =
            convertDocumentColor(layer->shapeData->style.strokeColor, source, destination, options);
        layer->cpuImage = ShapeRenderer::render(*layer->shapeData, documentSize)
            .convertToFormat(QImage::Format_RGBA8888);
        layer->shapeCache.dirty = true;
    } else {
        layer->cpuImage = convertImageToProfile(layer->cpuImage, source, destination, options);
        if (layer->rasterStorage.isEnabled()) {
            layer->rasterStorage.forEachTile([&](core::RasterTile& tile) {
                tile.cpuImage = convertImageToProfile(tile.cpuImage, source, destination, options);
                tile.dirtyGpu = true;
            });
            layer->rasterStorage.markAllGpuDirty();
        }
    }

    layer->textureOutdated = true;
    layer->pendingGpuUpload = true;
    if (layer->owner) {
        layer->owner->thumbnailDirty = true;
        layer->owner->sourceDirty = true;
        layer->owner->invalidateEffects();
    }
}

static void convertDocumentContentToProfile(Document* doc,
                                            const ColorProfile& source,
                                            const ColorProfile& destination,
                                            const ColorConversionOptions& options)
{
    if (!doc)
        return;

    auto flat = doc->flatten();
    for (LayerTreeNode* node : flat)
        convertLayerContentToProfile(node, doc->size, source, destination, options);
}

// ----- Layer Operations -----

void ImageController::newLayer()
{
    if (!m_doc) return;
    QSize size = m_doc->size;
    if (!size.isValid() || size.isEmpty())
        size = QSize(1024, 768);

    auto treeNode = std::make_unique<LayerTreeNode>();
    treeNode->type = LayerTreeNode::Type::Layer;
    treeNode->name = QString("Layer %1").arg(m_doc->flatCount() + 1);
    treeNode->layer = std::make_shared<Layer>();
    treeNode->layer->name = treeNode->name;
    treeNode->layer->cpuImage = QImage(size, QImage::Format_RGBA8888);
    treeNode->layer->cpuImage.fill(Qt::transparent);
    treeNode->layer->owner = treeNode.get();
    treeNode->layer->resetTransform = treeNode->transform();
    treeNode->layer->hasResetTransform = true;

    int flatIndex = 0;
    if (m_doc->flatCount() == 0) {
        m_doc->size = size;
        m_doc->selection.resize(size.width(), size.height());
        m_doc->roots.push_back(std::move(treeNode));
        m_doc->activeFlatIndex = 0;
    } else {
        auto* activeNode = m_doc->activeNode();
        if (activeNode && activeNode->type == LayerTreeNode::Type::Group) {
            treeNode->parent = activeNode;
            activeNode->children.insert(activeNode->children.begin(), std::move(treeNode));
            int parentFlat = m_doc->activeFlatIndex;
            m_doc->activeFlatIndex = parentFlat + 1;
            flatIndex = parentFlat + 1;
        } else {
            flatIndex = (activeNode && m_doc->activeFlatIndex >= 0)
                ? m_doc->activeFlatIndex
                : 0;
            m_doc->activeFlatIndex = m_doc->insertNodeAt(flatIndex, std::move(treeNode));
        }
    }

    m_doc->selectedFlatIndices.clear();
    m_doc->selectedFlatIndices.insert(m_doc->activeFlatIndex);

    auto* newNode = m_doc->nodeAt(m_doc->activeFlatIndex);
    if (newNode && newNode->layer) {
        if (size.width() * size.height() >= 256 * 256)
            newNode->layer->enableTiling(m_doc->tileSize());
        m_history.push(std::make_unique<AddLayerCommand>(
            m_doc, flatIndex, newNode->clone(), tr("Add Layer")));
    }

    emit layerChanged(m_doc->activeFlatIndex);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::newGroup()
{
    if (!m_doc) return;

    std::vector<LayerTreeNode*> selectedNodes;
    if (!m_doc->selectedFlatIndices.empty()) {
        auto flat = m_doc->flatten();
        selectedNodes.reserve(m_doc->selectedFlatIndices.size());
        for (int idx : m_doc->selectedFlatIndices) {
            if (idx < 0 || idx >= static_cast<int>(flat.size())) continue;
            auto* node = flat[idx];
            if (node && node->type == LayerTreeNode::Type::Layer)
                selectedNodes.push_back(node);
        }
    }

    auto groupNode = std::make_unique<LayerTreeNode>();
    groupNode->type = LayerTreeNode::Type::Group;
    groupNode->name = QString("Group %1").arg(m_doc->flatCount() + 1);

    int flatIndex = 0;
    auto* activeNode = m_doc->activeNode();
    if (!selectedNodes.empty()) {
        auto flat = m_doc->flatten();
        int firstSelected = static_cast<int>(flat.size());
        for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
            for (auto* sel : selectedNodes) {
                if (flat[i] == sel) {
                    firstSelected = std::min(firstSelected, i);
                    break;
                }
            }
        }
        flatIndex = std::max(0, firstSelected);
        m_doc->activeFlatIndex = m_doc->insertNodeAt(flatIndex, std::move(groupNode));
    } else if (activeNode && m_doc->flatCount() > 0) {
        flatIndex = (activeNode && m_doc->activeFlatIndex >= 0)
            ? m_doc->activeFlatIndex
            : 0;
        m_doc->activeFlatIndex = m_doc->insertNodeAt(flatIndex, std::move(groupNode));
    } else {
        m_doc->roots.insert(m_doc->roots.begin(), std::move(groupNode));
        m_doc->activeFlatIndex = 0;
    }

    auto* newNode = m_doc->nodeAt(m_doc->activeFlatIndex);
    if (newNode) {
        m_history.push(std::make_unique<AddLayerCommand>(
            m_doc, flatIndex, newNode->clone(), tr("Add Group")));
    }

    if (newNode && !selectedNodes.empty()) {
        auto findFlatIndex = [this](LayerTreeNode* target) -> int {
            if (!m_doc || !target) return -1;
            auto flatNow = m_doc->flatten();
            for (int i = 0; i < static_cast<int>(flatNow.size()); ++i) {
                if (flatNow[i] == target)
                    return i;
            }
            return -1;
        };

        for (auto* selNode : selectedNodes) {
            const int selIdx = findFlatIndex(selNode);
            const int groupIdx = findFlatIndex(newNode);
            if (selIdx < 0 || groupIdx < 0 || selIdx == groupIdx)
                continue;
            moveNodeIntoGroup(selIdx, groupIdx);
        }

        const int groupIdx = findFlatIndex(newNode);
        if (groupIdx >= 0) {
            m_doc->activeFlatIndex = groupIdx;
            m_doc->selectedFlatIndices.clear();
            m_doc->selectedFlatIndices.insert(groupIdx);
        }
    }

    emit layerChanged(m_doc->activeFlatIndex);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit selectionChanged();
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::ungroupSelectedNodes()
{
    if (!m_doc) return;

    // Resolve the group nodes to dissolve. Work on raw pointers so the flat
    // indices shifting under the mutation never invalidate the work list.
    // Selection is processed ancestor-first (ascending flat index), so a
    // dissolved descendant group is always promoted (survives) before it is
    // itself revisited.
    std::vector<LayerTreeNode*> groups;
    {
        auto flat = m_doc->flatten();
        auto collect = [&](int idx) {
            if (idx < 0 || idx >= static_cast<int>(flat.size())) return;
            auto* n = flat[idx];
            if (n && n->type == LayerTreeNode::Type::Group)
                groups.push_back(n);
        };
        if (!m_doc->selectedFlatIndices.empty()) {
            for (int idx : m_doc->selectedFlatIndices)
                collect(idx);
        }
        if (groups.empty())
            collect(m_doc->activeFlatIndex);
    }
    if (groups.empty()) return;

    auto cloneRoots = [](const std::vector<std::unique_ptr<LayerTreeNode>>& roots) {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& r : roots) {
            if (r)
                clones.push_back(r->shallowClone()); // COW: structural op, no pixel copy
        }
        return clones;
    };
    auto beforeRoots = cloneRoots(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    std::vector<LayerTreeNode*> promoted;   // children moved out — reselected after

    for (auto* group : groups) {
        auto& sibs = group->parent ? group->parent->children : m_doc->roots;
        int pos = -1;
        for (int i = 0; i < static_cast<int>(sibs.size()); ++i) {
            if (sibs[i].get() == group) { pos = i; break; }
        }
        if (pos < 0) continue;

        LayerTreeNode* newParent = group->parent;   // null for a root group

        // Detach the children first (group->children becomes empty) so erasing
        // the now-empty group is clean, then splice them into the group's slot.
        std::vector<std::unique_ptr<LayerTreeNode>> moved;
        moved.reserve(group->children.size());
        for (auto& child : group->children) {
            if (!child) continue;
            child->parent = newParent;
            promoted.push_back(child.get());
            moved.push_back(std::move(child));
        }
        group->children.clear();

        sibs.erase(sibs.begin() + pos);
        for (int i = 0; i < static_cast<int>(moved.size()); ++i)
            sibs.insert(sibs.begin() + pos + i, std::move(moved[i]));
    }

    // Reselect the promoted children at their new flat indices.
    std::set<int> newSelected;
    int newActive = -1;
    {
        auto flatNow = m_doc->flatten();
        for (int i = 0; i < static_cast<int>(flatNow.size()); ++i) {
            for (auto* p : promoted) {
                if (flatNow[i] == p) {
                    newSelected.insert(i);
                    if (newActive < 0) newActive = i;
                    break;
                }
            }
        }
    }
    if (newActive < 0)
        newActive = std::min(beforeActive, std::max(0, m_doc->flatCount() - 1));
    m_doc->activeFlatIndex = newActive;
    m_doc->selectedFlatIndices = newSelected;

    auto afterRoots = cloneRoots(m_doc->roots);
    m_history.push(std::make_unique<LayerTreeStateCommand>(
        m_doc,
        std::move(beforeRoots), beforeActive, beforeSelected,
        std::move(afterRoots), newActive, newSelected,
        tr("Ungroup")));

    emit layerChanged(newActive);
    emit activeLayerChanged(newActive);
    emit selectionChanged();
    emit imageChanged();
    ++m_doc->compositionGeneration;
}

// ── Adjustment layers ─────────────────────────────────────────────

static int flatIndexOfNode(const Document* doc, const LayerTreeNode* target)
{
    if (!doc || !target) return -1;
    auto flat = doc->flatten();
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        if (flat[i] == target)
            return i;
    }
    return -1;
}

// Composes an adjustment's stored mask into a full document-sized buffer.
// Areas the stored buffer doesn't cover default to white (= fully applied),
// matching the out-of-bounds sampling convention of the compositors.
static QImage adjustmentMaskAsDocImage(const Layer* l, const QSize& docSize)
{
    QImage doc(docSize, QImage::Format_Grayscale8);
    doc.fill(255);
    if (l && !l->maskImage.isNull()) {
        QPainter p(&doc);
        p.drawImage(l->maskOrigin, l->maskImage);
    }
    return doc;
}

// Document-space mask → parent-layer pixel space (Single Layer Mode). Same
// affine chain as createMaskFromSelection: layer pixel → canvas pixel via the
// node's accumulated transform, sampled inverse. Border = white.
static QImage warpMaskDocToLayer(const QImage& docMaskIn, const Document* doc,
                                 const LayerTreeNode* layerNode,
                                 const QRect& targetBounds)
{
    const int dw = doc->size.width();
    const int dh = doc->size.height();
    const QSize baseSize = layerNode->layer->rasterBaseSize();
    const int lw = std::max(1, baseSize.width());
    const int lh = std::max(1, baseSize.height());
    const int mw = std::max(1, targetBounds.width());
    const int mh = std::max(1, targetBounds.height());

    const QTransform tf = layerNode->accumulatedTransform();
    double a00 = dw * tf.m11() / lw;
    double a01 = -dw * tf.m21() / lh;
    double a02 = dw * 0.5 * (1.0 - tf.m11() + tf.m21() + tf.m31());
    double a10 = -dh * tf.m12() / lw;
    double a11 = dh * tf.m22() / lh;
    double a12 = dh * 0.5 * (1.0 + tf.m12() - tf.m22() - tf.m32());
    a02 += a00 * targetBounds.left() + a01 * targetBounds.top();
    a12 += a10 * targetBounds.left() + a11 * targetBounds.top();

    cv::Mat xf = (cv::Mat_<double>(2, 3) << a00, a01, a02, a10, a11, a12);
    QImage docMask = docMaskIn.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat docMat(dh, dw, CV_8UC1, docMask.bits(),
                   static_cast<size_t>(docMask.bytesPerLine()));

    QImage out(mw, mh, QImage::Format_Grayscale8);
    out.fill(255);
    cv::Mat outMat(mh, mw, CV_8UC1, out.bits(),
                   static_cast<size_t>(out.bytesPerLine()));
    cv::warpAffine(docMat, outMat, xf, cv::Size(mw, mh),
                   cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                   cv::BORDER_CONSTANT, cv::Scalar(255));
    return out;
}

// Parent-layer pixel space → document space (back to Normal Mode). Forward
// warp, mirroring loadMaskToSelection but with a white border.
static QImage warpMaskLayerToDoc(const QImage& layerMaskIn, const QPoint& maskOrigin,
                                 const Document* doc,
                                 const LayerTreeNode* layerNode)
{
    const int dw = doc->size.width();
    const int dh = doc->size.height();
    const QSize baseSize = layerNode->layer->rasterBaseSize();
    const int bw = std::max(1, baseSize.width());
    const int bh = std::max(1, baseSize.height());

    const QTransform tf = layerNode->accumulatedTransform();
    double a00 = dw * tf.m11() / bw;
    double a01 = -dw * tf.m21() / bh;
    double a02 = dw * 0.5 * (1.0 - tf.m11() + tf.m21() + tf.m31());
    double a10 = -dh * tf.m12() / bw;
    double a11 = dh * tf.m22() / bh;
    double a12 = dh * 0.5 * (1.0 + tf.m12() - tf.m22() - tf.m32());
    a02 += a00 * maskOrigin.x() + a01 * maskOrigin.y();
    a12 += a10 * maskOrigin.x() + a11 * maskOrigin.y();

    cv::Mat xf = (cv::Mat_<double>(2, 3) << a00, a01, a02, a10, a11, a12);
    QImage layerMask = layerMaskIn.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat srcMat(layerMask.height(), layerMask.width(), CV_8UC1,
                   layerMask.bits(),
                   static_cast<size_t>(layerMask.bytesPerLine()));

    QImage out(dw, dh, QImage::Format_Grayscale8);
    out.fill(255);
    cv::Mat outMat(dh, dw, CV_8UC1, out.bits(),
                   static_cast<size_t>(out.bytesPerLine()));
    cv::warpAffine(srcMat, outMat, xf, cv::Size(dw, dh),
                   cv::INTER_LINEAR,
                   cv::BORDER_CONSTANT, cv::Scalar(255));
    return out;
}

// Re-binds an adjustment node's mask + coordinate space after its parent
// changed (Normal Mode ⇄ Single Layer Mode, or between two parent layers).
// `oldParentLayer` is the Layer-type node it was nested under before the
// move (nullptr when it came from the stack). No-op when the mode is
// unchanged, so plain reorders within the same scope keep the mask bits.
static void rebindAdjustmentSpace(Document* doc, LayerTreeNode* adj,
                                  const LayerTreeNode* oldParentLayer)
{
    if (!doc || !adj || adj->type != LayerTreeNode::Type::Adjustment)
        return;
    LayerTreeNode* newParentLayer =
        (adj->parent && adj->parent->type == LayerTreeNode::Type::Layer)
            ? adj->parent : nullptr;
    if (newParentLayer == oldParentLayer)
        return;
    if (!adj->layer) {
        adj->layer = std::make_shared<Layer>();
        adj->layer->name = adj->name;
        adj->layer->owner = adj;
    }
    Layer* l = adj->layer.get();

    // 1) Current mask → document space.
    QImage docMask;
    if (!l->maskImage.isNull()) {
        if (oldParentLayer && oldParentLayer->layer)
            docMask = warpMaskLayerToDoc(l->maskImage, l->maskOrigin,
                                         doc, oldParentLayer);
        else
            docMask = adjustmentMaskAsDocImage(l, doc->size);
    }

    // 2) Document space → new coordinate space.
    if (newParentLayer && newParentLayer->layer) {
        QRect bounds = newParentLayer->layer->maskTargetBounds();
        if (bounds.isEmpty())
            bounds = QRect(QPoint(0, 0), doc->size);
        if (!docMask.isNull())
            l->maskImage = warpMaskDocToLayer(docMask, doc, newParentLayer, bounds);
        l->maskOrigin = bounds.topLeft();
        l->cpuImage = QImage(bounds.size(), QImage::Format_RGBA8888);
        l->cpuImage.fill(Qt::transparent);
    } else {
        if (!docMask.isNull())
            l->maskImage = docMask;
        l->maskOrigin = QPoint(0, 0);
        l->cpuImage = QImage(doc->size, QImage::Format_RGBA8888);
        l->cpuImage.fill(Qt::transparent);
    }
    l->maskRawImage = QImage();
    adj->setBaseTransform(QTransform());
    l->maskThumbDirty = true;
    l->maskTextureOutdated = true;
    l->textureOutdated = true;
    adj->invalidateEffects();   // propagates into the new parent layer's bake
}

void ImageController::addAdjustmentLayer(const QString& adjustmentType,
                                        const QVariantMap& initialParams)
{
    if (!m_doc || m_doc->size.isEmpty())
        return;
    if (!adjustments::isKnown(adjustmentType)) {
        qWarning() << "addAdjustmentLayer: unknown adjustment" << adjustmentType;
        return;
    }

    // Incremental naming: "Grayscale", "Grayscale 2", "Grayscale 3", ...
    const QString base = adjustments::displayName(adjustmentType);
    QSet<QString> names;
    for (auto* n : m_doc->flatten()) {
        if (n) names.insert(n->name);
    }
    QString name = base;
    for (int i = 2; names.contains(name); ++i)
        name = QStringLiteral("%1 %2").arg(base).arg(i);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Adjustment;
    node->name = name;
    node->adjustment = std::make_shared<AdjustmentData>();
    node->adjustment->type = adjustmentType;
    node->adjustment->params = initialParams;
    node->layer = std::make_shared<Layer>();
    node->layer->name = name;
    node->layer->owner = node.get();
    // The transparent doc-sized image defines the document coordinate space
    // used by the mask tools; it is never drawn or uploaded.
    node->layer->cpuImage = QImage(m_doc->size, QImage::Format_RGBA8888);
    node->layer->cpuImage.fill(Qt::transparent);
    // Default: born with a white (reveal-all) mask.
    node->layer->maskImage = QImage(m_doc->size, QImage::Format_Grayscale8);
    node->layer->maskImage.fill(255);
    node->layer->maskVisible = true;
    node->layer->maskThumbDirty = true;

    // Above the active node; top of the stack when nothing is selected. When
    // the active node is a nested adjustment, anchor on its parent layer so
    // creation never lands in Single Layer Mode implicitly.
    int insertAt = 0;
    auto* active = m_doc->activeNode();
    if (active && m_doc->activeFlatIndex >= 0) {
        LayerTreeNode* anchor = active;
        while (anchor->parent && anchor->parent->type == LayerTreeNode::Type::Layer)
            anchor = anchor->parent;
        const int anchorIdx = flatIndexOfNode(m_doc, anchor);
        insertAt = std::max(0, anchorIdx);
    }

    const int newIndex = m_doc->insertNodeAt(insertAt, std::move(node));
    m_doc->activeFlatIndex = newIndex;
    m_doc->selectedFlatIndices = {newIndex};

    if (auto* inserted = m_doc->nodeAt(newIndex)) {
        m_history.push(std::make_unique<AddLayerCommand>(
            m_doc, newIndex, inserted->clone(), tr("Add Adjustment Layer")));
    }

    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit selectionChanged();
    emit imageChanged();
    ++m_doc->compositionGeneration;
}

void ImageController::moveAdjustmentToLayer(int adjFlatIndex, int targetLayerFlatIndex)
{
    if (!m_doc || adjFlatIndex == targetLayerFlatIndex)
        return;
    auto* adj = m_doc->nodeAt(adjFlatIndex);
    auto* target = m_doc->nodeAt(targetLayerFlatIndex);
    if (!adj || !adj->isAdjustmentLayer())
        return;
    // Compatible targets: Pixel / Text / Shape layers — never another
    // adjustment or a group.
    if (!target || target->type != LayerTreeNode::Type::Layer || !target->layer)
        return;
    if (adj->parent == target)
        return;

    auto cloneRoots = [](const std::vector<std::unique_ptr<LayerTreeNode>>& roots) {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& r : roots) {
            if (r) clones.push_back(r->shallowClone()); // COW: structural op, no pixel copy
        }
        return clones;
    };
    auto beforeRoots = cloneRoots(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    const LayerTreeNode* oldParentLayer =
        (adj->parent && adj->parent->type == LayerTreeNode::Type::Layer)
            ? adj->parent : nullptr;

    auto owned = m_doc->takeNodeAt(adjFlatIndex);
    if (!owned) return;
    LayerTreeNode* adjPtr = owned.get();
    owned->parent = target;
    // Topmost child = applied last, matching panel order.
    target->children.insert(target->children.begin(), std::move(owned));
    rebindAdjustmentSpace(m_doc, adjPtr, oldParentLayer);
    target->collapsed = false;
    target->thumbnailDirty = true;
    target->invalidateEffects();

    const int newIndex = flatIndexOfNode(m_doc, adjPtr);
    m_doc->activeFlatIndex = newIndex;
    m_doc->selectedFlatIndices.clear();
    if (newIndex >= 0)
        m_doc->selectedFlatIndices.insert(newIndex);

    auto afterRoots = cloneRoots(m_doc->roots);
    m_history.push(std::make_unique<LayerTreeStateCommand>(
        m_doc,
        std::move(beforeRoots), beforeActive, beforeSelected,
        std::move(afterRoots), m_doc->activeFlatIndex, m_doc->selectedFlatIndices,
        tr("Move Adjustment into Layer")));

    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit selectionChanged();
    emit imageChanged();
    ++m_doc->compositionGeneration;
}

void ImageController::moveAdjustmentToStack(int adjFlatIndex, int insertFlatIndex)
{
    if (!m_doc)
        return;
    auto* adj = m_doc->nodeAt(adjFlatIndex);
    if (!adj || !adj->isAdjustmentLayer())
        return;
    const LayerTreeNode* oldParentLayer =
        (adj->parent && adj->parent->type == LayerTreeNode::Type::Layer)
            ? adj->parent : nullptr;
    if (!oldParentLayer) {
        // Already in the stack — a plain reorder covers it.
        reorderNode(adjFlatIndex, insertFlatIndex);
        return;
    }

    auto cloneRoots = [](const std::vector<std::unique_ptr<LayerTreeNode>>& roots) {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& r : roots) {
            if (r) clones.push_back(r->shallowClone()); // COW: structural op, no pixel copy
        }
        return clones;
    };
    auto beforeRoots = cloneRoots(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    auto owned = m_doc->takeNodeAt(adjFlatIndex);
    if (!owned) return;
    LayerTreeNode* adjPtr = owned.get();

    int insertAt = insertFlatIndex;
    if (insertFlatIndex > adjFlatIndex)
        insertAt -= 1;
    insertAt = std::clamp(insertAt, 0, m_doc->flatCount());
    // Don't land inside another layer's child list — that would silently
    // re-enter Single Layer Mode on a different layer.
    if (auto* t = m_doc->nodeAt(insertAt)) {
        LayerTreeNode* anchor = t;
        while (anchor->parent && anchor->parent->type == LayerTreeNode::Type::Layer)
            anchor = anchor->parent;
        if (anchor != t) {
            const int redirected = flatIndexOfNode(m_doc, anchor);
            if (redirected >= 0)
                insertAt = redirected;
        }
    }

    const int newIndex = m_doc->insertNodeAt(insertAt, std::move(owned));
    rebindAdjustmentSpace(m_doc, adjPtr, oldParentLayer);

    m_doc->activeFlatIndex = newIndex;
    m_doc->selectedFlatIndices.clear();
    if (newIndex >= 0)
        m_doc->selectedFlatIndices.insert(newIndex);

    auto afterRoots = cloneRoots(m_doc->roots);
    m_history.push(std::make_unique<LayerTreeStateCommand>(
        m_doc,
        std::move(beforeRoots), beforeActive, beforeSelected,
        std::move(afterRoots), m_doc->activeFlatIndex, m_doc->selectedFlatIndices,
        tr("Move Adjustment out of Layer")));

    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit selectionChanged();
    emit imageChanged();
    ++m_doc->compositionGeneration;
}

void ImageController::updateAdjustmentParamsLive(int flatIndex, const QVariantMap& params)
{
    if (!m_doc)
        return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->isAdjustmentLayer())
        return;

    node->adjustment->params = params;
    // A Single-Layer-Mode adjustment is baked into its parent layer's effected
    // image — invalidate so the live frame re-renders the new curve.
    if (node->type == LayerTreeNode::Type::Adjustment)
        node->invalidateEffects();
    ++m_doc->compositionGeneration;
    emit imageChanged();
}

void ImageController::commitAdjustmentParams(int flatIndex, const QVariantMap& before,
                                             const QVariantMap& after, const QString& label)
{
    if (!m_doc)
        return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->isAdjustmentLayer())
        return;
    if (before == after)
        return; // no-op gesture: nothing to record

    AdjustmentData beforeData{ node->adjustment->type, before };
    AdjustmentData afterData{ node->adjustment->type, after };

    node->adjustment->params = after;
    if (node->type == LayerTreeNode::Type::Adjustment)
        node->invalidateEffects();
    ++m_doc->compositionGeneration;

    m_history.push(std::make_unique<AdjustmentParamsCommand>(
        m_doc, flatIndex, std::move(beforeData), std::move(afterData), label));

    emit layerChanged(flatIndex);
    emit imageChanged();
    emit historyChanged();
}

void ImageController::removeNode(int flatIndex)
{
    if (!m_doc || flatIndex < 0 || flatIndex >= m_doc->flatCount())
        return;

    auto* node = m_doc->nodeAt(flatIndex);
    if (!node) return;

    // Cancel any pending async jobs for this layer
    if (node->layer) {
        if (auto* aj = AsyncJobSystem::instance())
            aj->cancelForLayer(node->layer.get());
    }

    auto clone = node->clone();

    (void)m_doc->takeNodeAt(flatIndex);

    if (m_doc->flatCount() == 0) {
        m_doc->activeFlatIndex = -1;
    } else {
        if (m_doc->activeFlatIndex >= m_doc->flatCount())
            m_doc->activeFlatIndex = m_doc->flatCount() - 1;
    }

    m_history.push(std::make_unique<RemoveLayerCommand>(
        m_doc, flatIndex, std::move(clone), tr("Remove Layer")));

    emit layerChanged(flatIndex);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setCurrentFrame(int frame)
{
    if (!m_doc)
        return;
    const int before = m_doc->currentFrame();
    // Clamp + evaluate happen in the document; returns whether the composite
    // changed. Pixels/masks/thumbnails/textures are intentionally not touched.
    const bool composeChanged = m_doc->setCurrentFrame(frame);
    const int after = m_doc->currentFrame();
    if (after != before)
        emit currentFrameChanged(after);
    if (composeChanged)
        emit imageChanged();  // existing flow: syncLayersToGpu + canvas update
}

bool ImageController::prepareRasterCelForEdit(LayerTreeNode* node)
{
    if (!m_doc)
        return false;
    if (!node)
        node = m_doc->activeNode();
    if (!node || !node->layer || node->type != LayerTreeNode::Type::Layer)
        return false;

    auto* track = m_doc->animation.rasterTrack(node->id);
    if (!track || track->isEmpty())
        return true; // ordinary static-layer edit

    const int frame = m_doc->currentFrame();
    anim::CelId celId;
    if (track->hasKeyframe(frame))
        celId = m_doc->animation.detachRasterCelForEdit(node->id, frame);
    else if (const auto held = track->celAt(frame))
        celId = *held;

    if (celId.isNull()) {
        anim::RasterCelContent content;
        QSize size = node->layer->rasterBaseSize();
        if (!size.isValid() || size.isEmpty())
            size = m_doc->size.expandedTo(QSize(1, 1));
        content.cpuImage = QImage(size, QImage::Format_RGBA8888);
        content.cpuImage.fill(Qt::transparent);
        content.bounds = QRect(QPoint(0, 0), size);
        celId = m_doc->animation.celStorage().createCel(std::move(content));
        track->setCel(frame, celId);
    }

    auto content = m_doc->animation.celStorage().contentHandle(celId);
    if (!content)
        return false;
    node->layer->setEvaluatedRasterContent(celId, content);
    node->compositeDirty = true;
    ++m_doc->compositionGeneration;
    return true;
}

bool ImageController::createRasterCel(bool duplicateCurrent)
{
    if (!m_doc)
        return false;
    auto* node = m_doc->activeNode();
    if (!node || !node->layer || node->type != LayerTreeNode::Type::Layer
        || node->layer->isTextLayer() || node->layer->isShapeLayer())
        return false;

    anim::AnimationModel before = m_doc->animation;
    auto& track = m_doc->animation.ensureRasterTrack(node->id);
    const int frame = m_doc->currentFrame();
    std::optional<anim::CelId> celId;
    if (duplicateCurrent)
        celId = track.celAt(frame);

    if (!celId) {
        anim::RasterCelContent content;
        if (duplicateCurrent) {
            content.cpuImage = node->layer->compositeImage();
        } else {
            QSize size = node->layer->rasterBaseSize();
            if (!size.isValid() || size.isEmpty())
                size = m_doc->size.expandedTo(QSize(1, 1));
            content.cpuImage = QImage(size, QImage::Format_RGBA8888);
            content.cpuImage.fill(Qt::transparent);
        }
        content.bounds = QRect(QPoint(0, 0), content.cpuImage.size());
        celId = m_doc->animation.celStorage().createCel(std::move(content));
    }
    track.setCel(frame, celId);
    m_doc->animation.pruneUnusedCels();
    m_doc->setCurrentFrame(frame);
    anim::AnimationModel after = m_doc->animation;
    m_history.push(std::make_unique<anim::AnimationModelStateCommand>(
        m_doc, std::move(before), std::move(after),
        duplicateCurrent ? tr("Duplicate Raster Cel") : tr("Create Raster Cel")));
    emit imageChanged();
    return true;
}

bool ImageController::createEmptyRasterFrame()
{
    if (!m_doc)
        return false;
    auto* node = m_doc->activeNode();
    if (!node || !node->layer || node->type != LayerTreeNode::Type::Layer)
        return false;
    anim::AnimationModel before = m_doc->animation;
    m_doc->animation.ensureRasterTrack(node->id).setCel(
        m_doc->currentFrame(), std::nullopt);
    m_doc->animation.pruneUnusedCels();
    m_doc->setCurrentFrame(m_doc->currentFrame());
    anim::AnimationModel after = m_doc->animation;
    m_history.push(std::make_unique<anim::AnimationModelStateCommand>(
        m_doc, std::move(before), std::move(after), tr("Create Empty Frame")));
    emit imageChanged();
    return true;
}

bool ImageController::removeRasterCelKeyframe()
{
    if (!m_doc)
        return false;
    auto* node = m_doc->activeNode();
    auto* track = node ? m_doc->animation.rasterTrack(node->id) : nullptr;
    if (!track || !track->hasKeyframe(m_doc->currentFrame()))
        return false;
    anim::AnimationModel before = m_doc->animation;
    track->removeKeyframe(m_doc->currentFrame());
    if (track->isEmpty())
        m_doc->animation.removeRasterTrack(node->id);
    else
        m_doc->animation.pruneUnusedCels();
    if (!m_doc->animation.hasRasterTrack(node->id))
        node->layer->clearEvaluatedRasterContent();
    m_doc->setCurrentFrame(m_doc->currentFrame());
    anim::AnimationModel after = m_doc->animation;
    m_history.push(std::make_unique<anim::AnimationModelStateCommand>(
        m_doc, std::move(before), std::move(after), tr("Remove Raster Cel")));
    emit imageChanged();
    return true;
}

bool ImageController::moveRasterCelKeyframe(int targetFrame)
{
    if (!m_doc) return false;
    auto* node = m_doc->activeNode();
    auto* track = node ? m_doc->animation.rasterTrack(node->id) : nullptr;
    const int from = m_doc->currentFrame();
    targetFrame = std::clamp(targetFrame, m_doc->animation.startFrame(),
                             m_doc->animation.endFrame());
    if (!track || from == targetFrame || !track->hasKeyframe(from))
        return false;
    anim::AnimationModel before = m_doc->animation;
    track->moveKeyframe(from, targetFrame);
    m_doc->setCurrentFrame(targetFrame);
    anim::AnimationModel after = m_doc->animation;
    m_history.push(std::make_unique<anim::AnimationModelStateCommand>(
        m_doc, std::move(before), std::move(after), tr("Move Raster Cel")));
    emit currentFrameChanged(targetFrame);
    emit imageChanged();
    return true;
}

bool ImageController::pasteRasterCel(
    const std::optional<anim::RasterCelContent>& content)
{
    if (!m_doc) return false;
    auto* node = m_doc->activeNode();
    if (!node || !node->layer || node->type != LayerTreeNode::Type::Layer)
        return false;
    anim::AnimationModel before = m_doc->animation;
    auto& track = m_doc->animation.ensureRasterTrack(node->id);
    if (content) {
        anim::RasterCelContent detached;
        detached.cpuImage = content->cpuImage.copy();
        detached.bounds = content->bounds;
        detached.metadata = content->metadata;
        if (content->rasterStorage.isEnabled()) {
            QRect bounds;
            const QImage tiles = content->rasterStorage.toImage(&bounds);
            detached.rasterStorage.replaceWithImage(
                tiles, bounds.topLeft(), content->rasterStorage.tileSize());
            detached.rasterStorage.setBaseSize(content->rasterStorage.baseSize());
        }
        const anim::CelId id = m_doc->animation.celStorage().createCel(
            std::move(detached));
        track.setCel(m_doc->currentFrame(), id);
    } else {
        track.setCel(m_doc->currentFrame(), std::nullopt);
    }
    m_doc->animation.pruneUnusedCels();
    m_doc->setCurrentFrame(m_doc->currentFrame());
    anim::AnimationModel after = m_doc->animation;
    m_history.push(std::make_unique<anim::AnimationModelStateCommand>(
        m_doc, std::move(before), std::move(after), tr("Paste Raster Cel")));
    emit imageChanged();
    return true;
}

// Copy animation tracks from a source subtree onto a structurally-identical
// destination subtree (the duplicate), remapping to the destination's new ids.
static void copyTracksParallel(Document* doc, const LayerTreeNode* src,
                               const LayerTreeNode* dst)
{
    if (!doc || !src || !dst) return;
    doc->animation.copyTracks(src->id, dst->id);
    const size_t n = std::min(src->children.size(), dst->children.size());
    for (size_t i = 0; i < n; ++i)
        copyTracksParallel(doc, src->children[i].get(), dst->children[i].get());
}

// Snapshot a subtree's animation tracks (keyed by their current ids) for the
// clipboard. Used only for a whole-layer/group copy, never a raster copy.
static void captureSubtreeTracks(Document* doc, const LayerTreeNode* node,
                                 std::vector<ClipboardTrack>& out,
                                 std::vector<ClipboardRasterTrack>* rasterOut = nullptr,
                                 anim::CelStorage* celOut = nullptr)
{
    if (!doc || !node) return;
    if (const auto* layerTracks = doc->animation.tracksFor(node->id)) {
        for (const auto& [prop, track] : *layerTracks)
            out.push_back(ClipboardTrack{ node->id, prop, track });
    }
    if (rasterOut) {
        if (const auto* track = doc->animation.rasterTrack(node->id)) {
            rasterOut->push_back(ClipboardRasterTrack{node->id, *track});
            if (celOut) {
                for (const auto& [_, celId] : track->keyframes()) {
                    if (!celId || celOut->contains(*celId)) continue;
                    if (const auto* content = doc->animation.celStorage().content(*celId))
                        celOut->insertCel(*celId, *content);
                }
            }
        }
    }
    for (const auto& c : node->children)
        captureSubtreeTracks(doc, c.get(), out, rasterOut, celOut);
}

void ImageController::duplicateNode(int flatIndex)
{
    if (!m_doc || flatIndex < 0 || flatIndex >= m_doc->flatCount())
        return;

    auto* srcNode = m_doc->nodeAt(flatIndex);
    if (!srcNode) return;

    auto dup = std::make_unique<LayerTreeNode>();
    dup->type = srcNode->type;
    dup->name = srcNode->name + " (copy)";
    dup->setBaseOpacity(srcNode->opacity());
    dup->setBaseVisible(srcNode->isVisible());
    dup->setBaseBlendMode(srcNode->blendMode());
    dup->groupBlendMode = srcNode->groupBlendMode;
    dup->lockFlags = srcNode->lockFlags;
    dup->collapsed = srcNode->collapsed;
    dup->clipped = srcNode->clipped;
    dup->setBaseTransform(srcNode->transform());
    dup->effects = srcNode->effects;   // layer styles travel with the copy
    dup->invalidateEffects();

    if (srcNode->layer) {
        dup->layer = std::make_shared<Layer>();
        dup->layer->name = dup->name;
        dup->layer->cpuImage = flushLayerToCpuImage(srcNode->layer.get());
        dup->layer->textureOutdated = true;
        dup->layer->owner = dup.get();
        dup->layer->resetTransform = dup->transform();
        dup->layer->hasResetTransform = true;
        if (srcNode->layer->textData)
            dup->layer->textData = std::make_shared<TextLayerData>(*srcNode->layer->textData);
        if (srcNode->layer->shapeData)
            dup->layer->shapeData = std::make_shared<ShapeData>(*srcNode->layer->shapeData);
        if (!srcNode->layer->maskImage.isNull()) {
            dup->layer->maskImage = srcNode->layer->maskImage.copy();
            if (!srcNode->layer->maskRawImage.isNull())
                dup->layer->maskRawImage = srcNode->layer->maskRawImage.copy();
            dup->layer->maskOrigin = srcNode->layer->maskOrigin;
            dup->layer->maskThumbDirty = true;
        }
        dup->layer->maskVisible = srcNode->layer->maskVisible;
        dup->layer->maskDensity = srcNode->layer->maskDensity;
        dup->layer->maskFeather = srcNode->layer->maskFeather;
    }

    if (srcNode->adjustment)
        dup->adjustment = std::make_shared<AdjustmentData>(*srcNode->adjustment);

    // Nested children (Single-Layer-Mode adjustments under a layer, group
    // members) duplicate with their parent.
    for (const auto& child : srcNode->children) {
        if (!child) continue;
        auto childClone = child->clone();
        childClone->parent = dup.get();
        dup->children.push_back(std::move(childClone));
    }

    // Real duplication: the copy is a NEW node — give the whole duplicated
    // subtree fresh, distinct ids (children came from clone(), which preserves
    // ids, so without this they would collide with the originals).
    dup->assignNewIds();
    // Copy the source's animation tracks onto the duplicate's new ids so the
    // copy animates independently of the original.
    if (m_doc->animation.hasAnyTracks())
        copyTracksParallel(m_doc, srcNode, dup.get());

    auto clone = dup->clone();
    m_doc->insertNodeAt(flatIndex, std::move(dup));
    m_doc->activeFlatIndex = flatIndex;

    m_history.push(std::make_unique<DuplicateLayerCommand>(
        m_doc, flatIndex, std::move(clone), tr("Duplicate Layer")));

    emit layerChanged(flatIndex);
    emit activeLayerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setActiveNode(int flatIndex)
{
    if (!m_doc) return;
    if (flatIndex < 0 || flatIndex >= m_doc->flatCount()) {
        m_doc->deselectAll();
        emit activeLayerChanged(-1);
        return;
    }
    m_doc->selectNode(flatIndex, false);
    emit activeLayerChanged(flatIndex);
}

void ImageController::setMultiSelectNode(int flatIndex, bool addToSelection)
{
    if (!m_doc) return;
    if (flatIndex < 0 || flatIndex >= m_doc->flatCount()) {
        if (!addToSelection) {
            m_doc->deselectAll();
            emit selectionChanged();
            emit activeLayerChanged(-1);
        }
        return;
    }
    m_doc->selectNode(flatIndex, addToSelection);
    emit selectionChanged();
    emit activeLayerChanged(m_doc->activeFlatIndex);
}

void ImageController::selectNodeRange(int fromFlat, int toFlat)
{
    if (!m_doc) return;
    m_doc->selectRange(fromFlat, toFlat);
    emit selectionChanged();
    emit activeLayerChanged(m_doc->activeFlatIndex);
}

void ImageController::selectAllNodes()
{
    if (!m_doc) return;
    int count = m_doc->flatCount();
    if (count == 0) return;
    for (int i = 0; i < count; ++i)
        m_doc->selectedFlatIndices.insert(i);
    m_doc->activeFlatIndex = count - 1;
    emit selectionChanged();
    emit activeLayerChanged(m_doc->activeFlatIndex);
}

std::vector<int> ImageController::selectedIndices() const
{
    if (!m_doc) return {};
    return m_doc->selectedList();
}

void ImageController::setSelectedIndices(const std::set<int>& indices)
{
    if (!m_doc) return;
    m_doc->selectedFlatIndices = indices;
    if (indices.empty()) {
        m_doc->activeFlatIndex = -1;
    } else if (indices.count(m_doc->activeFlatIndex) == 0) {
        m_doc->activeFlatIndex = *indices.rbegin();
    }
    emit selectionChanged();
    emit activeLayerChanged(m_doc->activeFlatIndex);
}

void ImageController::setNodeTransforms(
    const std::vector<int>& flatIndices,
    const std::vector<QTransform>& newTransforms,
    const std::vector<QTransform>& oldTransforms,
    const QString& name)
{
    if (!m_doc) return;
    // All three vectors must match in size
    size_t n = std::min({flatIndices.size(), newTransforms.size(), oldTransforms.size()});
    if (n == 0) { return; }

    std::vector<int> staticIndices;
    std::vector<QTransform> staticBefore;
    std::vector<QTransform> staticAfter;
    m_history.beginMacro(name.isEmpty() ? tr("Move") : name);
    for (size_t i = 0; i < n; ++i) {
        auto* node = m_doc->nodeAt(flatIndices[i]);
        if (!node)
            continue;
        if (!m_propertyController.editTransform(
                node, newTransforms[i], m_doc->currentFrame())) {
            node->setBaseTransform(newTransforms[i]);
            staticIndices.push_back(flatIndices[i]);
            staticBefore.push_back(oldTransforms[i]);
            staticAfter.push_back(newTransforms[i]);
        }
    }
    if (!staticIndices.empty())
        m_history.push(std::make_unique<NodeTransformCommand>(
            m_doc, std::move(staticIndices), std::move(staticBefore),
            std::move(staticAfter), name.isEmpty() ? tr("Move") : name));
    m_history.endMacro();
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::previewNodeTransform(LayerTreeNode* node,
                                           const QTransform& transform)
{
    if (!node || !m_doc)
        return;
    bool animated = autoKey();
    for (anim::Property property : {
             anim::Property::PositionX, anim::Property::PositionY,
             anim::Property::ScaleX, anim::Property::ScaleY,
             anim::Property::Rotation, anim::Property::SkewX,
             anim::Property::SkewY, anim::Property::PivotX,
             anim::Property::PivotY}) {
        animated = animated || m_doc->animation.track(node->id, property) != nullptr;
    }
    if (animated)
        node->setEvaluatedTransform(transform);
    else
        node->setBaseTransform(transform);
}

void ImageController::flipNodesAsUnit(const std::vector<int>& flatIndices, bool horizontal)
{
    if (!m_doc || flatIndices.empty()) return;

    // 1. Union bounding box of the target subtrees, in canvas NDC — measured on
    //    the same VISUAL frame the on-screen outline uses
    //    (TransformController::visualFrameForNode): the content pixel bounds for
    //    dab (rasterStorage) layers and the oriented content frame for shapes,
    //    the plain layer quad otherwise. Using the raw layer quad here put the
    //    pivot at the centre of the layer BASE, not of the visible pixels, so
    //    flipping a dab layer reflected the content about the wrong axis and the
    //    outline jumped to a different place. With the shared frame the outline
    //    stays put and only the content mirrors.
    qreal minX = std::numeric_limits<qreal>::max();
    qreal minY = std::numeric_limits<qreal>::max();
    qreal maxX = std::numeric_limits<qreal>::lowest();
    qreal maxY = std::numeric_limits<qreal>::lowest();
    bool hasBounds = false;
    const QPointF localCorners[4] = {
        {-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}
    };
    std::function<void(LayerTreeNode*)> accumulate = [&](LayerTreeNode* n) {
        if (!n) return;
        if (n->type == LayerTreeNode::Type::Layer && n->layer) {
            const QTransform t = TransformController::visualFrameForNode(n);
            for (const QPointF& c : localCorners) {
                const QPointF p = t.map(c);
                minX = std::min(minX, p.x()); minY = std::min(minY, p.y());
                maxX = std::max(maxX, p.x()); maxY = std::max(maxY, p.y());
                hasBounds = true;
            }
        }
        for (const auto& child : n->children)
            accumulate(child.get());
    };
    for (int idx : flatIndices)
        accumulate(m_doc->nodeAt(idx));
    if (!hasBounds) return;

    const QPointF pivot((minX + maxX) * 0.5, (minY + maxY) * 0.5);

    // 2. Reflection about the canvas axis through the shared pivot (NDC). The
    //    same world reflection W is applied to every target, so they mirror as
    //    one rigid block rather than each about its own centre.
    const QTransform W = horizontal
        ? QTransform(-1, 0, 0, 1, 2.0 * pivot.x(), 0)
        : QTransform(1, 0, 0, -1, 0, 2.0 * pivot.y());

    // 3. For each target: newT = nodeT · (P · W · P⁻¹), where P is the parent's
    //    accumulated transform — applies the world reflection while keeping the
    //    node expressed in its parent's frame. Flipping a group node alone
    //    mirrors its children for free (they ride its transform).
    std::vector<int> idxs;
    std::vector<QTransform> before, after;
    for (int idx : flatIndices) {
        auto* n = m_doc->nodeAt(idx);
        if (!n) continue;
        if (n->isPositionLocked()) continue;   // honour Lock Position per node
        const QTransform P = n->parent ? n->parent->accumulatedTransform()
                                       : QTransform();
        const QTransform delta = P * W * P.inverted();
        idxs.push_back(idx);
        before.push_back(n->transform());
        after.push_back(n->transform() * delta);
    }
    if (idxs.empty()) {
        emit operationBlocked(tr("This layer's position is locked."));
        return;
    }

    setNodeTransforms(idxs, after, before,
                      horizontal ? tr("Flip Horizontal") : tr("Flip Vertical"));
}

int ImageController::addGuide(GuideOrientation orientation, qreal position)
{
    if (!m_doc) return -1;
    const int index = m_doc->guideManager.addGuide(orientation, position);
    const Guide guide = m_doc->guideManager.guideAt(index);
    m_history.push(std::make_unique<AddGuideCommand>(
        m_doc, index, guide, tr("Add Guide")));
    emit guidesChanged();
    return index;
}

void ImageController::moveGuide(int index, qreal position, qreal oldPosition)
{
    if (!m_doc || index < 0 || index >= m_doc->guideManager.guideCount())
        return;
    if (std::abs(position - oldPosition) < 0.001)
        return;

    m_doc->guideManager.moveGuide(index, position);
    m_history.push(std::make_unique<MoveGuideCommand>(
        m_doc, index, oldPosition, position, tr("Move Guide")));
    emit guidesChanged();
}

void ImageController::removeGuide(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->guideManager.guideCount())
        return;

    const Guide removed = m_doc->guideManager.removeGuide(index);
    m_history.push(std::make_unique<RemoveGuideCommand>(
        m_doc, index, removed, tr("Remove Guide")));
    emit guidesChanged();
}

void ImageController::clearGuides()
{
    if (!m_doc || m_doc->guideManager.empty())
        return;

    std::vector<Guide> before = m_doc->guideManager.guides();
    m_doc->guideManager.clear();
    m_history.push(std::make_unique<ClearGuidesCommand>(
        m_doc, std::move(before), tr("Clear Guides")));
    emit guidesChanged();
}

void ImageController::removeSelectedNodes()
{
    if (!m_doc) return;
    auto indices = m_doc->selectedList();
    if (indices.empty()) {
        if (m_doc->activeFlatIndex >= 0 && m_doc->activeFlatIndex < m_doc->flatCount())
            indices = {m_doc->activeFlatIndex};
        else
            return;
    }

    // Fully-locked (Lock All) layers cannot be deleted. Drop them from the set
    // and warn if that leaves nothing to remove.
    bool skippedLocked = false;
    indices.erase(std::remove_if(indices.begin(), indices.end(),
        [this, &skippedLocked](int idx) {
            auto* n = m_doc->nodeAt(idx);
            if (n && n->isFullyLocked()) { skippedLocked = true; return true; }
            return false;
        }), indices.end());
    if (indices.empty()) {
        if (skippedLocked)
            emit operationBlocked(tr("The layer is locked and cannot be removed."));
        return;
    }

    // Sort descending so we remove from back without index invalidation
    std::sort(indices.begin(), indices.end(), std::greater<int>());

    auto name = indices.size() == 1 ? tr("Remove Layer") : tr("Remove Layers");
    auto composite = std::make_unique<CompositeCommand>(name);

    for (int idx : indices) {
        auto* node = m_doc->nodeAt(idx);
        if (!node) continue;

        if (node->layer) {
            if (auto* aj = AsyncJobSystem::instance())
                aj->cancelForLayer(node->layer.get());
        }

        auto clone = node->clone();
        (void)m_doc->takeNodeAt(idx);

        composite->add(std::make_unique<RemoveLayerCommand>(
            m_doc, idx, std::move(clone), QString()));
    }

    if (m_doc->flatCount() == 0) {
        m_doc->activeFlatIndex = -1;
        m_doc->selectedFlatIndices.clear();
    } else {
        int newActive = qMin(indices.back(), m_doc->flatCount() - 1);
        m_doc->activeFlatIndex = newActive;
        m_doc->selectedFlatIndices.clear();
        m_doc->selectedFlatIndices.insert(newActive);
    }

    m_history.push(std::move(composite));

    for (int idx : indices)
        emit layerChanged(idx);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit selectionChanged();
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setNodeOpacity(int flatIndex, float opacity)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    // Animated property / auto-key: record a keyframe instead of editing base.
    if (m_propertyController.editOpacity(node, opacity, m_doc->currentFrame())) {
        emit layerChanged(flatIndex);
        emit imageChanged();
        return;
    }
    float before = node->opacity();
    float after = std::clamp(opacity, 0.0f, 1.0f);
    if (before == after) return;
    node->setBaseOpacity(after);
    // A nested adjustment (Single Layer Mode) bakes its opacity into the
    // parent layer's effected image — invalidate so the next frame re-bakes.
    if (node->type == LayerTreeNode::Type::Adjustment)
        node->invalidateEffects();
    m_history.push(std::make_unique<NodePropertyCommand>(
        m_doc, flatIndex, before, after,
        node->isVisible(), node->isVisible(),
        node->blendMode(), node->blendMode(),
        tr("Set Opacity")));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::beginNodeOpacity(int flatIndex)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    m_opacityLayerIdx = flatIndex;
    m_opacityBefore   = node->opacity();
}

void ImageController::previewNodeOpacity(int flatIndex, float opacity)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    node->setBaseOpacity(std::clamp(opacity, 0.0f, 1.0f));
    if (node->type == LayerTreeNode::Type::Adjustment)
        node->invalidateEffects();
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::commitNodeOpacity(int flatIndex, float opacity)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;

    // Animated property / auto-key: the whole slider drag commits as one keyframe
    // edit at the current frame (SetKeyframeCommand is a single undo entry).
    if (m_propertyController.editOpacity(node, opacity, m_doc->currentFrame())) {
        m_opacityBefore = -1.0f;
        m_opacityLayerIdx = -1;
        emit layerChanged(flatIndex);
        emit imageChanged();
        return;
    }

    float before = (m_opacityLayerIdx == flatIndex && m_opacityBefore >= 0.0f)
                   ? m_opacityBefore : node->opacity();
    float after  = std::clamp(opacity, 0.0f, 1.0f);

    node->setBaseOpacity(after);
    m_opacityBefore   = -1.0f;
    m_opacityLayerIdx = -1;
    if (node->type == LayerTreeNode::Type::Adjustment)
        node->invalidateEffects();

    if (before == after) return;

    m_history.push(std::make_unique<NodePropertyCommand>(
        m_doc, flatIndex, before, after,
        node->isVisible(), node->isVisible(),
        node->blendMode(), node->blendMode(),
        tr("Set Opacity")));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setNodeVisibility(int flatIndex, bool visible)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    if (m_propertyController.editVisibility(node, visible, m_doc->currentFrame())) {
        emit layerChanged(flatIndex);
        emit imageChanged();
        return;
    }
    bool before = node->isVisible();
    if (before == visible) return;
    node->setBaseVisible(visible);
    // Toggling a nested adjustment (Single Layer Mode) changes the parent
    // layer's baked render.
    if (node->type == LayerTreeNode::Type::Adjustment)
        node->invalidateEffects();
    m_history.push(std::make_unique<NodePropertyCommand>(
        m_doc, flatIndex, node->opacity(), node->opacity(),
        before, visible,
        node->blendMode(), node->blendMode(),
        tr("Set Visibility")));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::toggleSoloVisibility(int flatIndex)
{
    if (!m_doc) return;
    auto flat = m_doc->flatten();
    if (flat.empty()) return;

    auto markVisible = [](LayerTreeNode* n, bool vis) {
        if (!n || n->isVisible() == vis) return;
        n->setBaseVisible(vis);
        if (n->type == LayerTreeNode::Type::Adjustment)
            n->invalidateEffects();
    };

    if (m_soloActive) {
        // Restore the snapshot captured when solo was engaged (by flat index;
        // nodeAt bounds-checks, so a structural change mid-solo can't crash).
        for (const auto& [idx, vis] : m_soloPrevVisibility)
            markVisible(m_doc->nodeAt(idx), vis);
        m_soloActive = false;
        m_soloPrevVisibility.clear();
    } else {
        auto* target = m_doc->nodeAt(flatIndex);
        if (!target) return;

        // Snapshot every node's visibility so the next Alt+click restores it.
        m_soloPrevVisibility.clear();
        m_soloPrevVisibility.reserve(flat.size());
        for (int i = 0; i < static_cast<int>(flat.size()); ++i)
            m_soloPrevVisibility.push_back({i, flat[i]->isVisible()});

        // A node belongs to the target's branch if it is the target, a
        // descendant (target is one of its ancestors), or an ancestor of the
        // target. Everything else is hidden; the target branch keeps its own
        // per-node visibility.
        auto inTargetBranch = [target](LayerTreeNode* n) -> bool {
            for (auto* p = n; p; p = p->parent)
                if (p == target) return true;          // self or descendant
            for (auto* p = target; p; p = p->parent)
                if (p == n) return true;               // ancestor
            return false;
        };
        for (auto* n : flat) {
            if (!inTargetBranch(n))
                markVisible(n, false);
        }
        // Force the ancestor chain (incl. target) visible so the branch renders
        // even if a parent group was hidden.
        for (auto* p = target; p; p = p->parent)
            markVisible(p, true);

        m_soloActive = true;
    }

    emit layerChanged(flatIndex);
    emit imageChanged();
    ++m_doc->compositionGeneration;
}

void ImageController::setNodeBlendMode(int flatIndex, BlendMode mode)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    if (m_propertyController.editBlendMode(node, mode, m_doc->currentFrame())) {
        emit layerChanged(flatIndex);
        emit imageChanged();
        return;
    }
    BlendMode before = node->blendMode();
    if (before == mode) return;
    node->setBaseBlendMode(mode);
    m_history.push(std::make_unique<NodePropertyCommand>(
        m_doc, flatIndex, node->opacity(), node->opacity(),
        node->isVisible(), node->isVisible(),
        before, mode,
        tr("Set Blend Mode")));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

static QString lockBitName(int flagBit, bool on)
{
    const QString verb = on ? QObject::tr("Lock") : QObject::tr("Unlock");
    switch (flagBit) {
    case LockTransparent: return verb + QObject::tr(" Transparent Pixels");
    case LockImage:       return verb + QObject::tr(" Image Pixels");
    case LockPosition:    return verb + QObject::tr(" Position");
    case LockAll:         return on ? QObject::tr("Lock All") : QObject::tr("Unlock All");
    default:              return QObject::tr("Change Layer Lock");
    }
}

void ImageController::setNodesLockBit(const std::vector<int>& flatIndices,
                                      int flagBit, bool on)
{
    if (!m_doc || flatIndices.empty()) return;
    std::vector<LockFlagsCommand::Entry> entries;
    for (int idx : flatIndices) {
        auto* node = m_doc->nodeAt(idx);
        if (!node) continue;
        const int before = node->lockFlags;
        const int after = on ? (before | flagBit) : (before & ~flagBit);
        if (before == after) continue;
        node->lockFlags = after;
        entries.push_back({idx, before, after});
    }
    if (entries.empty()) return;
    m_history.push(std::make_unique<LockFlagsCommand>(
        m_doc, entries, lockBitName(flagBit, on)));
    // Locks never change pixels, so no compositionGeneration bump; just refresh
    // the panel/menus that mirror lock state.
    for (const auto& e : entries)
        emit layerChanged(e.flatIndex);
}

void ImageController::setNodeLockFlags(int flatIndex, int newFlags)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    const int before = node->lockFlags;
    if (before == newFlags) return;
    node->lockFlags = newFlags;
    std::vector<LockFlagsCommand::Entry> entries{ {flatIndex, before, newFlags} };
    const bool nowLocked = newFlags != LockNone;
    m_history.push(std::make_unique<LockFlagsCommand>(
        m_doc, entries,
        nowLocked ? tr("Lock Layer") : tr("Unlock Layer")));
    emit layerChanged(flatIndex);
}

void ImageController::previewLayerEffects(int flatIndex, const std::vector<LayerEffect>& effects)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer) return;
    node->effects = effects;
    node->invalidateEffects();
    node->thumbnailDirty = true;
    node->layer->textureOutdated = true;
    if (m_doc) ++m_doc->compositionGeneration;
    emit layerChanged(flatIndex);
    emit imageChanged();
}

void ImageController::commitLayerEffects(
    int flatIndex,
    const std::vector<LayerEffect>& beforeEffects,
    const std::vector<LayerEffect>& afterEffects)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer) return;
    // Layer styles are non-destructive, so only the master Lock All blocks them.
    if (node->isFullyLocked()) {
        emit operationBlocked(tr("This layer is fully locked."));
        return;
    }
    node->effects = afterEffects;
    node->invalidateEffects();
    node->thumbnailDirty = true;
    node->layer->textureOutdated = true;
    m_history.push(std::make_unique<LayerEffectsCommand>(
        m_doc, flatIndex, beforeEffects, afterEffects, tr("Layer Styles")));
    if (m_doc) ++m_doc->compositionGeneration;
    emit layerChanged(flatIndex);
    emit imageChanged();
}

void ImageController::copyLayerEffects(int flatIndex)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node) return;
    m_copiedEffects = node->effects;
}

void ImageController::pasteLayerEffects()
{
    if (!m_doc || m_copiedEffects.empty()) return;
    // Apply to the whole selection (fall back to the active node). Each target
    // goes through commitLayerEffects, which validates Lock-All per node and
    // pushes its own undo step; locked layers are skipped with feedback.
    std::vector<int> targets(m_doc->selectedFlatIndices.begin(),
                             m_doc->selectedFlatIndices.end());
    if (targets.empty() && m_doc->activeFlatIndex >= 0)
        targets.push_back(m_doc->activeFlatIndex);
    for (int idx : targets) {
        auto* node = m_doc->nodeAt(idx);
        if (!node || !node->layer) continue;
        commitLayerEffects(idx, node->effects, m_copiedEffects);
    }
}

void ImageController::clearLayerEffects(int flatIndex)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || node->effects.empty()) return;
    commitLayerEffects(flatIndex, node->effects, {});
}

void ImageController::toggleAllLayerEffects(int flatIndex)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || node->effects.empty()) return;
    // If anything is enabled, disable the whole stack; otherwise enable it all.
    bool anyEnabled = false;
    for (const auto& e : node->effects)
        if (e.enabled) { anyEnabled = true; break; }
    std::vector<LayerEffect> after = node->effects;
    for (auto& e : after)
        e.enabled = !anyEnabled;
    commitLayerEffects(flatIndex, node->effects, after);
}

void ImageController::reorderNode(int fromFlat, int toFlat)
{
    if (!m_doc || fromFlat < 0 || fromFlat >= m_doc->flatCount() ||
        toFlat < 0 || toFlat > m_doc->flatCount())
        return;

    int insertAt = (toFlat > fromFlat) ? toFlat - 1 : toFlat;
    if (insertAt == fromFlat)
        return;

    auto cloneRoots = [](const std::vector<std::unique_ptr<LayerTreeNode>>& roots) {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& r : roots) {
            if (r)
                clones.push_back(r->shallowClone()); // COW: structural op, no pixel copy
        }
        return clones;
    };
    auto beforeRoots = cloneRoots(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    auto* movingNodeBefore = m_doc->nodeAt(fromFlat);
    if (!movingNodeBefore) return;
    // Adjustment nodes are not spatial: skip the world-transform preservation
    // and instead rebind the mask space if the move changed their mode.
    const LayerTreeNode* adjOldParentLayer =
        (movingNodeBefore->parent
         && movingNodeBefore->parent->type == LayerTreeNode::Type::Layer)
            ? movingNodeBefore->parent : nullptr;
    const QTransform movingWorldBefore = movingNodeBefore->accumulatedTransform();
    QTransform oldParentAccum;
    for (auto* p = movingNodeBefore->parent; p; p = p->parent)
        oldParentAccum = oldParentAccum * p->transform();
    const bool hasResetBefore = movingNodeBefore->layer
        && movingNodeBefore->layer->hasResetTransform;
    const QTransform resetWorldBefore = hasResetBefore
        ? (movingNodeBefore->layer->resetTransform * oldParentAccum)
        : QTransform();

    auto owned = [&]() -> std::unique_ptr<LayerTreeNode> {
        auto* node = movingNodeBefore;
        if (!node) return nullptr;
        if (node->parent) {
            for (auto it = node->parent->children.begin(); it != node->parent->children.end(); ++it) {
                if (it->get() == node) {
                    auto ptr = std::move(*it);
                    node->parent->children.erase(it);
                    return ptr;
                }
            }
        } else {
            for (auto it = m_doc->roots.begin(); it != m_doc->roots.end(); ++it) {
                if (it->get() == node) {
                    auto ptr = std::move(*it);
                    m_doc->roots.erase(it);
                    return ptr;
                }
            }
        }
        return nullptr;
    }();

    if (!owned) return;

    const int newActive = m_doc->insertNodeAt(insertAt, std::move(owned));
    if (newActive >= 0) {
        auto* movedNode = m_doc->nodeAt(newActive);
        if (movedNode && movedNode->type == LayerTreeNode::Type::Adjustment) {
            // Mode may have changed (stack ⇄ nested under a layer): convert
            // the mask between document and layer space; transform stays
            // identity.
            rebindAdjustmentSpace(m_doc, movedNode, adjOldParentLayer);
        } else if (movedNode) {
            QTransform newParentAccum;
            for (auto* p = movedNode->parent; p; p = p->parent)
                newParentAccum = newParentAccum * p->transform();
            movedNode->setBaseTransform(movingWorldBefore * newParentAccum.inverted());
            if (movedNode->layer) {
                if (hasResetBefore) {
                    movedNode->layer->resetTransform = resetWorldBefore * newParentAccum.inverted();
                    movedNode->layer->hasResetTransform = true;
                } else {
                    movedNode->layer->resetTransform = movedNode->transform();
                    movedNode->layer->hasResetTransform = true;
                }
            }
        }
    }
    m_doc->activeFlatIndex = newActive;
    m_doc->selectedFlatIndices.clear();
    if (newActive >= 0)
        m_doc->selectedFlatIndices.insert(newActive);

    auto afterRoots = cloneRoots(m_doc->roots);
    const int afterActive = m_doc->activeFlatIndex;
    const std::set<int> afterSelected = m_doc->selectedFlatIndices;

    m_history.push(std::make_unique<LayerTreeStateCommand>(
        m_doc,
        std::move(beforeRoots), beforeActive, beforeSelected,
        std::move(afterRoots), afterActive, afterSelected,
        tr("Reorder")));

    emit activeLayerChanged(newActive);
    emit selectionChanged();
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setLayerTransform(int flatIndex, const QTransform& transform,
                                         const QTransform* oldTransform)
{
    auto* node = nodeAtOrWarn(m_doc, flatIndex);
    if (!node) return;
    // Animated transform / auto-key: keyframe the decomposed components at the
    // current frame (one undo group) instead of editing the base transform.
    if (m_propertyController.editTransform(node, transform, m_doc->currentFrame())) {
        emit layerChanged(flatIndex);
        emit imageChanged();
        return;
    }
    QTransform oldXf = oldTransform ? *oldTransform : node->transform();
    node->setBaseTransform(transform);
    if (node->layer) {
        // Transform-only change → transform-only command (same as the
        // multi-selection path, setNodeTransforms). The old FilterCommand here
        // also restored a cpuImage snapshot and cleared rasterStorage on
        // undo/redo — for dab (rasterStorage) layers cpuImage is the stale base,
        // so the first undo of a Move silently wiped every painted dab.
        m_history.push(std::make_unique<NodeTransformCommand>(
            m_doc, std::vector<int>{flatIndex},
            std::vector<QTransform>{oldXf},
            std::vector<QTransform>{transform},
            tr("Move")));
    }
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

QPointF ImageController::screenToImage(QPointF screenPos, Layer* layer,
                                        float zoom, QPointF panOffset,
                                        QPointF canvasHalfExtents, QSize viewportSize)
{
    if (!layer) return {};
    float ndcX = 2.0f * static_cast<float>(screenPos.x()) / viewportSize.width() - 1.0f;
    float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / viewportSize.height();

    float invZoom = 1.0f / zoom;
    float docNdcX = (ndcX - static_cast<float>(panOffset.x())) * invZoom;
    float docNdcY = (ndcY - static_cast<float>(panOffset.y())) * invZoom;

    float invHx = 1.0f / canvasHalfExtents.x();
    float invHy = 1.0f / canvasHalfExtents.y();
    float canvasNdcX = docNdcX * invHx;
    float canvasNdcY = docNdcY * invHy;

    const QTransform t = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
    QTransform inv = t.inverted();
    qreal lx, ly;
    inv.map(canvasNdcX, canvasNdcY, &lx, &ly);

    // Tiled layers map their local NDC onto rasterBaseSize(), not the cpuImage
    // (same convention as CanvasView::screenToImage).
    const QSize imgSize = layer->rasterStorage.isEnabled()
        ? layer->rasterBaseSize()
        : layer->cpuImage.size();
    float imgX = (static_cast<float>(lx) + 1.0f) * 0.5f * imgSize.width();
    float imgY = (1.0f - static_cast<float>(ly)) * 0.5f * imgSize.height();

    return {imgX, imgY};
}

void ImageController::paintBrushDab(Layer* layer, QPointF imagePos,
                                     float radius, float opacity,
                                     QColor color, bool eraser)
{
    Q_UNUSED(opacity)
    if (!layer || layer->cpuImage.isNull()) return;

    int cx = static_cast<int>(imagePos.x());
    int cy = static_cast<int>(imagePos.y());
    int r = static_cast<int>(std::ceil(radius));

    QPainter p(&layer->cpuImage);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(eraser ? QPainter::CompositionMode_Clear
                                : QPainter::CompositionMode_SourceOver);

    QColor c = eraser ? Qt::transparent : color;
    QRadialGradient grad(cx, cy, radius, cx, cy, 0);
    grad.setColorAt(1.0f, QColor(c.red(), c.green(), c.blue(), 0));
    grad.setColorAt(0.3f, QColor(c.red(), c.green(), c.blue(), static_cast<int>(c.alpha() * 0.9f)));
    grad.setColorAt(0.0f, c);

    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(cx, cy), r, r);
    p.end();
}

void ImageController::paintStroke(Layer* layer, QPointF from, QPointF to,
                                   float radius, float opacity,
                                   QColor color, bool eraser)
{
    QPointF diff = to - from;
    float dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());
    float spacing = radius * 0.4f;
    int steps = std::max(1, static_cast<int>(dist / spacing));

    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / steps;
        QPointF pos = from + diff * t;
        paintBrushDab(layer, pos, radius, opacity, color, eraser);
    }
}

void ImageController::expandLayer(Layer* layer, QPointF imagePos,
                                   float brushRadius, int margin)
{
    if (!layer || layer->cpuImage.isNull()) return;
    float r = brushRadius + margin;

    int oldW = layer->cpuImage.width();
    int oldH = layer->cpuImage.height();

    int minX = std::min(0, static_cast<int>(imagePos.x() - r));
    int minY = std::min(0, static_cast<int>(imagePos.y() - r));
    int maxX = std::max(oldW, static_cast<int>(imagePos.x() + r));
    int maxY = std::max(oldH, static_cast<int>(imagePos.y() + r));
    int newW = maxX - minX;
    int newH = maxY - minY;
    int offX = -minX;
    int offY = -minY;

    if (newW <= oldW && newH <= oldH)
        return;

    QImage newImg(newW, newH, QImage::Format_RGBA8888);
    newImg.fill(Qt::transparent);
    {
        QPainter p(&newImg);
        p.drawImage(offX, offY, layer->cpuImage);
    }
    layer->cpuImage = newImg;

    if (layer->tiledSystem) {
        layer->enableTiling(layer->tileManager.tileSize());
        layer->tileManager.markAllDirty();
        layer->pendingGpuUpload = true;
    }

    float sx = float(newW) / float(oldW);
    float sy = float(newH) / float(oldH);
    float dx = 1.0f - 2.0f * offX / newW - float(oldW) / newW;
    float dy = float(oldH) / newH - 1.0f + 2.0f * offY / newH;
    QTransform adj;
    adj.scale(sx, sy);
    adj.translate(dx, dy);
    if (layer->owner)
        layer->owner->setBaseTransform(adj * layer->owner->transform());
}

void ImageController::syncLayerToGpu(Layer* layer)
{
    if (!layer || layer->cpuImage.isNull()) return;
    if (layer->textureId == 0) return;

    // Tiled (TileManager) layers also feed their per-tile textures, so flag those
    // for re-upload. But do NOT return here only setting that flag: the blend-mode
    // (shaderBlend) compositor path samples the layer's full-resolution textureId,
    // not the tiles, so the full texture must stay in sync with cpuImage as well.
    // Previously this branch cleared textureOutdated and returned without touching
    // the full texture, leaving it stale — a tiled layer with a non-Normal blend
    // mode then rendered transparent (its white fill never reached the texture the
    // blend path reads). Fall through to upload the full texture below; the flag is
    // only cleared once that upload actually happens.
    if (layer->tiledSystem)
        layer->pendingGpuUpload = true;

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto* extra = ctx->extraFunctions();
    if (!extra) return;

    extra->glBindTexture(GL_TEXTURE_2D, layer->textureId);
    extra->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                        layer->cpuImage.width(), layer->cpuImage.height(),
                        0, GL_RGBA, GL_UNSIGNED_BYTE,
                        layer->cpuImage.constBits());
    layer->textureOutdated = false;
    if (layer->owner) {
        layer->owner->thumbnailDirty = true;
        layer->owner->invalidateEffects();
    }
}

void ImageController::syncLayerFromGpu(Layer* layer)
{
    if (!layer || layer->textureId == 0) return;
    if (layer->cpuImage.isNull()) return;

    // Tiled layers (TileManager or RasterLayerStorage) keep their authoritative
    // pixels in cpuImage / per-tile textures — the single textureId is just an
    // empty placeholder for them. Reading it back here would wipe cpuImage
    // (e.g. delete_selected would clear the whole layer). cpuImage is already
    // current for these, so there is nothing to read back.
    if (layer->tiledSystem || layer->rasterStorage.isEnabled()) return;

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    ctx->extraFunctions()->glBindTexture(GL_TEXTURE_2D, layer->textureId);
    ::glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    layer->cpuImage.bits());
    layer->textureOutdated = false;
    // In-place readback: the QImage cacheKey may not change (sole-owner buffer),
    // so drop the cached alpha content bounds explicitly.
    layer->invalidateContentBounds();
    if (layer->owner) layer->owner->thumbnailDirty = true;
}

void ImageController::syncLayerMaskToGpu(Layer* layer)
{
    if (!layer || layer->maskImage.isNull()) return;

    auto* ctx = QOpenGLContext::currentContext();
    auto* extra = ctx ? ctx->extraFunctions() : nullptr;
    if (!extra) {
        // Mask mutations arrive from menu/panel handlers where no GL context is
        // current. Defer the upload: the render path re-uploads from maskImage
        // when maskTextureOutdated is set (uploadMaskTexture / syncLayersToGpu).
        layer->maskTextureOutdated = true;
        if (layer->owner)
            layer->owner->invalidateEffects();
        return;
    }
    if (layer->maskTextureId == 0)
        extra->glGenTextures(1, &layer->maskTextureId);

    extra->glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
    extra->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    extra->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    extra->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    extra->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint prevUnpackAlignment = 4;
    ::glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    extra->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                        layer->maskImage.width(), layer->maskImage.height(),
                        0, GL_RED, GL_UNSIGNED_BYTE,
                        layer->maskImage.constBits());
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    layer->maskTextureOutdated = false;
    if (layer->owner)
        layer->owner->invalidateEffects();
}

void ImageController::syncLayerMaskFromGpu(Layer* layer)
{
    if (!layer || layer->maskTextureId == 0 || layer->maskImage.isNull()) return;

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    ctx->extraFunctions()->glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);

    // [MASK-AUDIT] glGetTexImage reads the ENTIRE GPU texture (mip 0) into the
    // maskImage buffer; it ignores maskImage's own dimensions. If the GPU texture
    // is larger/smaller than maskImage, this overflows or row-misaligns the
    // buffer → progressive stretch/flip across edit round-trips. Confirm sizes.
    {
        GLint gpuW = 0, gpuH = 0;
        ::glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &gpuW);
        ::glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &gpuH);
        const qint64 cpuBytes = static_cast<qint64>(layer->maskImage.sizeInBytes());
        const int    gpuRow   = ((gpuW + 3) / 4) * 4;            // GL_PACK_ALIGNMENT=4
        const qint64 gpuBytes = static_cast<qint64>(gpuRow) * gpuH;
    }

    GLint prevPackAlignment = 4;
    ::glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
    ::glPixelStorei(GL_PACK_ALIGNMENT, 4);
    ::glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE,
                    layer->maskImage.bits());
    ::glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    layer->maskThumbDirty = true;
}

void ImageController::addLayerMask(int flatIndex, bool revealAll)
{
    if (!m_doc) return;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || layer->cpuImage.isNull()) return;
    if (!checkMaskEditable(layer)) return;

    const QRect targetBounds = layerMaskBounds(layer);
    if (targetBounds.isEmpty()) return;

    layer->maskImage = QImage(targetBounds.size(), QImage::Format_Grayscale8);
    layer->maskImage.fill(revealAll ? 255 : 0);
    layer->maskOrigin = targetBounds.topLeft();
    layer->maskVisible = true;
    layer->maskThumbDirty = true;
    if (layer->isShapeLayer())
        layer->textureOutdated = true;
    syncLayerMaskToGpu(layer);
    // Creating a mask selects the freshly created mask as the edit target.
    if (m_doc->activeFlatIndex == flatIndex)
        setEditingMask(true);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::removeLayerMask(int flatIndex)
{
    if (!m_doc) return;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer) return;
    if (!checkMaskEditable(layer)) return;

    if (layer->maskTextureId) {
        flushGpuChanges();
        layer->maskTextureId = 0;
    }
    if (layer->maskFbo) {
        flushGpuChanges();
        layer->maskFbo = 0;
    }
    layer->maskImage = QImage();
    layer->maskOrigin = QPoint(0, 0);
    layer->maskVisible = false;
    layer->maskThumbDirty = true;
    if (layer->isShapeLayer())
        layer->textureOutdated = true;
    if (layer->owner)
        layer->owner->invalidateEffects();

    if (m_editingMask && m_doc->activeFlatIndex == flatIndex) {
        setEditingMask(false);
    }

    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

bool ImageController::hasLayerMask(int flatIndex) const
{
    if (!m_doc) return false;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    return layer && !layer->maskImage.isNull();
}

void ImageController::toggleLayerMask(int flatIndex)
{
    if (hasLayerMask(flatIndex))
        removeLayerMask(flatIndex);
    else
        addLayerMask(flatIndex);
}

void ImageController::loadMaskToSelection(int flatIndex)
{
    if (!m_doc || m_doc->size.isEmpty()) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || node->layer->maskImage.isNull()) return;
    auto* layer = node->layer.get();

    int dw = m_doc->size.width();
    int dh = m_doc->size.height();
    int lw = layer->maskImage.width();
    int lh = layer->maskImage.height();
    if (lw <= 0 || lh <= 0) return;
    // Mirror of createMaskFromSelection: mask pixels live at maskOrigin within
    // the layer's base-size pixel space, so the affine scales by the base size
    // and offsets by the origin (a mask larger than the base stays aligned).
    const QSize baseSize = layer->rasterBaseSize();
    const int bw = std::max(1, baseSize.width());
    const int bh = std::max(1, baseSize.height());

    QTransform tf = node->accumulatedTransform();

    double a00 = dw * tf.m11() / bw;
    double a01 = -dw * tf.m21() / bh;
    double a02 = dw * 0.5 * (1.0 - tf.m11() + tf.m21() + tf.m31());
    double a10 = -dh * tf.m12() / bw;
    double a11 = dh * tf.m22() / bh;
    double a12 = dh * 0.5 * (1.0 + tf.m12() - tf.m22() - tf.m32());
    a02 += a00 * layer->maskOrigin.x() + a01 * layer->maskOrigin.y();
    a12 += a10 * layer->maskOrigin.x() + a11 * layer->maskOrigin.y();

    cv::Mat fullXf = (cv::Mat_<double>(2,3) << a00, a01, a02, a10, a11, a12);
    cv::Mat layerMask(lh, lw, CV_8UC1, layer->maskImage.bits(),
                      static_cast<size_t>(layer->maskImage.bytesPerLine()));

    m_doc->selection.create(dw, dh);
    cv::Mat docMask(dh, dw, CV_8UC1, m_doc->selection.bits(),
                    static_cast<size_t>(m_doc->selection.image().bytesPerLine()));
    cv::warpAffine(layerMask, docMask, fullXf, cv::Size(dw, dh),
                   cv::INTER_NEAREST,
                   cv::BORDER_CONSTANT, cv::Scalar(0));

    m_doc->selection.setActive(!m_doc->selection.isEmpty());

    m_history.push(std::make_unique<SelectionCommand>(
        m_doc, QImage(), m_doc->selection.image().copy(),
        false, m_doc->selection.active(),
        tr("Load Mask to Selection")));

    emit imageChanged();
}

void ImageController::combineMaskToSelection(int flatIndex, SelectMode mode)
{
    // Replace is the plain "load" case — reuse the proven path.
    if (mode == SelectMode::Replace) {
        loadMaskToSelection(flatIndex);
        return;
    }
    if (!m_doc || m_doc->size.isEmpty()) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || node->layer->maskImage.isNull()) return;
    auto* layer = node->layer.get();

    const int dw = m_doc->size.width();
    const int dh = m_doc->size.height();
    const int lw = layer->maskImage.width();
    const int lh = layer->maskImage.height();
    if (lw <= 0 || lh <= 0) return;

    // Same mask→document affine as loadMaskToSelection, but warped into a
    // temporary doc-sized mask that is then combined with the live selection
    // instead of replacing it.
    const QSize baseSize = layer->rasterBaseSize();
    const int bw = std::max(1, baseSize.width());
    const int bh = std::max(1, baseSize.height());

    QTransform tf = node->accumulatedTransform();
    double a00 = dw * tf.m11() / bw;
    double a01 = -dw * tf.m21() / bh;
    double a02 = dw * 0.5 * (1.0 - tf.m11() + tf.m21() + tf.m31());
    double a10 = -dh * tf.m12() / bw;
    double a11 = dh * tf.m22() / bh;
    double a12 = dh * 0.5 * (1.0 + tf.m12() - tf.m22() - tf.m32());
    a02 += a00 * layer->maskOrigin.x() + a01 * layer->maskOrigin.y();
    a12 += a10 * layer->maskOrigin.x() + a11 * layer->maskOrigin.y();

    cv::Mat fullXf = (cv::Mat_<double>(2,3) << a00, a01, a02, a10, a11, a12);
    cv::Mat layerMask(lh, lw, CV_8UC1, layer->maskImage.bits(),
                      static_cast<size_t>(layer->maskImage.bytesPerLine()));

    QImage warped(dw, dh, QImage::Format_Grayscale8);
    warped.fill(0);
    cv::Mat docMask(dh, dw, CV_8UC1, warped.bits(),
                    static_cast<size_t>(warped.bytesPerLine()));
    cv::warpAffine(layerMask, docMask, fullXf, cv::Size(dw, dh),
                   cv::INTER_NEAREST,
                   cv::BORDER_CONSTANT, cv::Scalar(0));

    QImage before = m_doc->selection.image().copy();
    const bool beforeActive = m_doc->selection.active();

    m_doc->selection.combineGrayscaleMask(warped, mode);
    m_doc->selection.setActive(!m_doc->selection.isEmpty());

    m_history.push(std::make_unique<SelectionCommand>(
        m_doc, before, m_doc->selection.image().copy(),
        beforeActive, m_doc->selection.active(),
        tr("Combine Mask with Selection")));

    emit imageChanged();
}

void ImageController::createMaskFromSelection(int flatIndex)
{
    if (!m_doc || !m_doc->selection.active() || m_doc->selection.isEmpty()) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer) return;
    auto* layer = node->layer.get();
    if (!checkMaskEditable(layer)) return;

    int dw = m_doc->size.width();
    int dh = m_doc->size.height();
    const QSize baseSize = layer->rasterBaseSize();
    int lw = baseSize.width();
    int lh = baseSize.height();
    if (lw <= 0 || lh <= 0) return;
    const QRect targetBounds = layerMaskBounds(layer);
    if (targetBounds.isEmpty()) return;
    const int mw = targetBounds.width();
    const int mh = targetBounds.height();

    QTransform tf = node->accumulatedTransform();
    double invDet = tf.m11() * tf.m22() - tf.m12() * tf.m21();

    double a00 = dw * tf.m11() / lw;
    double a01 = -dw * tf.m21() / lh;
    double a02 = dw * 0.5 * (1.0 - tf.m11() + tf.m21() + tf.m31());
    double a10 = -dh * tf.m12() / lw;
    double a11 = dh * tf.m22() / lh;
    double a12 = dh * 0.5 * (1.0 + tf.m12() - tf.m22() - tf.m32());
    a02 += a00 * targetBounds.left() + a01 * targetBounds.top();
    a12 += a10 * targetBounds.left() + a11 * targetBounds.top();

    cv::Mat invXf = (cv::Mat_<double>(2,3) << a00, a01, a02, a10, a11, a12);
    QImage selCopy = m_doc->selection.image().copy();
    cv::Mat docMask(dh, dw, CV_8UC1, selCopy.bits(),
                    static_cast<size_t>(selCopy.bytesPerLine()));

    QImage beforeMask = layer->maskImage.isNull() ? QImage() : layer->maskImage.copy();
    const QPoint beforeOrigin = layer->maskOrigin;
    QImage newMask(mw, mh, QImage::Format_Grayscale8);
    newMask.fill(255);
    cv::Mat layerMat(mh, mw, CV_8UC1, newMask.bits(),
                     static_cast<size_t>(newMask.bytesPerLine()));
    cv::warpAffine(docMask, layerMat, invXf, cv::Size(mw, mh),
                   cv::INTER_NEAREST | cv::WARP_INVERSE_MAP,
                   cv::BORDER_CONSTANT, cv::Scalar(255));

    layer->maskImage = newMask;
    layer->maskRawImage = newMask;
    layer->maskOrigin = targetBounds.topLeft();
    layer->maskVisible = true;
    layer->maskThumbDirty = true;
    // Shape layers render through the sprite cache only while unmasked; gaining
    // a mask switches the GPU texture source to cpuImage (see shapeSpriteRenderable).
    if (layer->isShapeLayer())
        layer->textureOutdated = true;

    syncLayerMaskToGpu(layer);
    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, flatIndex, std::move(beforeMask), newMask.copy(),
        tr("Create Mask from Selection"),
        beforeOrigin, layer->maskOrigin));
    // Creating a mask selects the freshly created mask as the edit target.
    if (m_doc->activeFlatIndex == flatIndex)
        setEditingMask(true);
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::invertLayerMask(int flatIndex)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || layer->maskImage.isNull()) return;
    if (!checkMaskEditable(layer)) return;

    QImage before = layer->maskImage.copy();
    layer->maskImage.invertPixels();
    layer->maskThumbDirty = true;

    syncLayerMaskToGpu(layer);
    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, flatIndex, std::move(before), layer->maskImage.copy(),
        tr("Invert Layer Mask"),
        layer->maskOrigin, layer->maskOrigin));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setMaskDensity(int flatIndex, float density)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer) return;
    if (!checkMaskEditable(layer)) return;
    layer->maskDensity = std::clamp(density, 0.0f, 1.0f);
    if (layer->owner)
        layer->owner->invalidateEffects();
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::setMaskFeather(int flatIndex, float radius)
{
    beginMaskFeather(flatIndex);
    commitMaskFeather(flatIndex, radius);
}

void ImageController::beginMaskFeather(int flatIndex)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || layer->maskImage.isNull()) return;
    if (!checkMaskEditable(layer)) return;
    m_featherLayerIdx    = flatIndex;
    m_featherOriginalMask = layer->maskImage.copy();
}

void ImageController::previewMaskFeather(int flatIndex, float radius)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || m_featherOriginalMask.isNull() || m_featherLayerIdx != flatIndex) return;

    radius = std::max(0.0f, radius);
    layer->maskImage = m_featherOriginalMask.copy();

    if (radius > 0.0f) {
        int ksize = static_cast<int>(2 * std::ceil(radius) + 1);
        if (ksize % 2 == 0) ++ksize;
        cv::Mat mask(layer->maskImage.height(), layer->maskImage.width(),
                     CV_8UC1, layer->maskImage.bits(),
                     static_cast<size_t>(layer->maskImage.bytesPerLine()));
        cv::GaussianBlur(mask, mask, cv::Size(ksize, ksize), radius);
    }

    layer->maskFeather = radius;
    layer->maskThumbDirty = true;
    syncLayerMaskToGpu(layer);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::commitMaskFeather(int flatIndex, float radius)
{
    if (m_featherOriginalMask.isNull() || m_featherLayerIdx != flatIndex)
        beginMaskFeather(flatIndex);

    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || m_featherOriginalMask.isNull()) return;

    QImage before = m_featherOriginalMask.copy();
    previewMaskFeather(flatIndex, radius);

    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, flatIndex, std::move(before), layer->maskImage.copy(),
        tr("Feather Layer Mask"),
        layer->maskOrigin, layer->maskOrigin));

    m_featherOriginalMask = QImage();
    m_featherLayerIdx = -1;
    emit layerChanged(flatIndex);
}

void ImageController::copyLayerMask(int flatIndex)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || layer->maskImage.isNull()) return;
    m_copiedMask = layer->maskImage.copy();
    m_copiedMaskOrigin = layer->maskOrigin;
}

void ImageController::pasteLayerMask(int flatIndex)
{
    if (m_copiedMask.isNull()) return;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer) return;
    if (!checkMaskEditable(layer)) return;

    QImage before = layer->maskImage.isNull() ? QImage() : layer->maskImage.copy();
    const QPoint beforeOrigin = layer->maskOrigin;
    const QRect targetBounds = layerMaskBounds(layer);
    if (targetBounds.isEmpty()) return;
    layer->maskImage = copyMaskIntoBounds(m_copiedMask, m_copiedMaskOrigin, targetBounds);
    layer->maskOrigin = targetBounds.topLeft();
    layer->maskVisible = true;
    layer->maskThumbDirty = true;
    if (layer->isShapeLayer())
        layer->textureOutdated = true;

    syncLayerMaskToGpu(layer);
    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, flatIndex, std::move(before), layer->maskImage.copy(),
        tr("Paste Layer Mask"),
        beforeOrigin, layer->maskOrigin));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::clearLayerMask(int flatIndex)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || layer->maskImage.isNull()) return;
    if (!checkMaskEditable(layer)) return;

    QImage before = layer->maskImage.copy();
    layer->maskImage.fill(255);
    layer->maskThumbDirty = true;

    syncLayerMaskToGpu(layer);
    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, flatIndex, std::move(before), layer->maskImage.copy(),
        tr("Clear Layer Mask"),
        layer->maskOrigin, layer->maskOrigin));
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::pushLayerSnapshot(const QString& name, int flatIndex, QImage before)
{
    if (!m_doc) return;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer) return;
    auto* node = m_doc->nodeAt(flatIndex);
    QImage after = layer->cpuImage.copy();
    QTransform t = node ? node->transform() : QTransform();
    m_history.push(std::make_unique<FilterCommand>(
        m_doc, flatIndex, std::move(before), t,
        std::move(after), t, name));
}

void ImageController::pushRasterTileSnapshot(const QString& name, int flatIndex,
                                             std::vector<core::RasterTileChange> changes)
{
    if (!m_doc || changes.empty())
        return;
    m_history.push(std::make_unique<RasterTileCommand>(
        m_doc, flatIndex, std::move(changes), name));
}

void ImageController::pushMaskEditSnapshot(const QString& name, int flatIndex, QImage before,
                                           QPoint beforeOrigin, QPoint afterOrigin)
{
    if (!m_doc) return;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer || layer->maskImage.isNull()) return;
    QImage after = layer->maskImage.copy();
    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, flatIndex, std::move(before), std::move(after), name,
        beforeOrigin, afterOrigin));
}

void ImageController::pushCommand(std::unique_ptr<Command> cmd)
{
    m_history.push(std::move(cmd));
}

UpscaleBackendStatus ImageController::upscaleBackendStatus()
{
    return m_upscaleService ? m_upscaleService->probe() : UpscaleBackendStatus{};
}

uint64_t ImageController::upscale(const UpscaleOptions& options)
{
    if (!m_doc || !m_upscaleService)
        return 0;

    if (options.output == UpscaleOutputMode::ReplaceDocument) {
        emit operationBlocked(tr("Replace Document is not enabled yet. Use New Document for AI Upscale."));
        return 0;
    }
    if (options.target == UpscaleTarget::CurrentLayer
        && options.output != UpscaleOutputMode::NewLayer
        && options.output != UpscaleOutputMode::ReplaceLayer) {
        emit operationBlocked(tr("Choose a layer output mode for AI Upscale."));
        return 0;
    }
    if (options.target == UpscaleTarget::CurrentDocument
        && options.output != UpscaleOutputMode::NewDocument
        && options.output != UpscaleOutputMode::ReplaceDocument) {
        emit operationBlocked(tr("Choose a document output mode for AI Upscale."));
        return 0;
    }

    UpscaleBackendStatus status = m_upscaleService->probe();
    if (!status.available()) {
        emit operationBlocked(status.userMessage.isEmpty()
            ? tr("Real-ESRGAN is not available.") : status.userMessage);
        return 0;
    }

    const QString packagedModelDir = RealEsrganProcessBackend::packagedModelsDirectory();
    QStringList missingPackaged;
    for (const QString& file : RealEsrganProcessBackend::expectedModelFiles(options.modelId)) {
        if (!QFile::exists(QDir(packagedModelDir).filePath(file)))
            missingPackaged << file;
    }
    const bool hasPackagedModel = QFileInfo(packagedModelDir).isDir() && missingPackaged.isEmpty();

    auto* registry = AiModelRegistry::instance();
    const auto model = hasPackagedModel ? std::nullopt : registry->modelById(options.modelId);
    QString modelDir = hasPackagedModel
        ? packagedModelDir
        : (model ? model->rootDir : RealEsrganProcessBackend::defaultModelDirectory(options.modelId));
    if (!hasPackagedModel && model) {
        if (!registry->validateModel(*model)) {
            emit operationBlocked(tr("The selected Real-ESRGAN model is not installed correctly."));
            return 0;
        }
    } else {
        QStringList missing = hasPackagedModel ? QStringList() : missingPackaged;
        if (!missing.isEmpty()) {
            emit operationBlocked(tr("The selected Real-ESRGAN model is missing."));
            return 0;
        }
    }

    UpscaleInput input;
    input.modelDir = modelDir;
    input.colorProfile = m_doc->colorProfile();
    input.profileSource = m_doc->profileSource();

    int sourceLayerIndex = -1;
    if (options.target == UpscaleTarget::CurrentLayer) {
        sourceLayerIndex = m_doc->activeFlatIndex;
        auto* node = m_doc->nodeAt(sourceLayerIndex);
        auto* layer = node && node->type == LayerTreeNode::Type::Layer ? node->layer.get() : nullptr;
        if (!node || !layer || layer->isTextLayer() || layer->isShapeLayer()) {
            emit operationBlocked(tr("Rasterize or select a pixel layer to use AI Upscale."));
            return 0;
        }
        if (options.output == UpscaleOutputMode::ReplaceLayer && node->isPixelEditingLocked()) {
            emit operationBlocked(tr("The pixels of this layer are locked."));
            return 0;
        }
        if (layer->rasterStorage.isEnabled()) {
            layer->cpuImage = flushLayerToCpuImage(layer);
            layer->rasterStorage.clear();
            layer->textureOutdated = true;
        }
        input.image = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
        input.sourceName = node->name;
    } else {
        input.image = compositeImage(m_doc).convertToFormat(QImage::Format_RGBA8888);
        input.sourceName = m_doc->name;
    }

    if (input.image.isNull()) {
        emit operationBlocked(tr("There is no image to upscale."));
        return 0;
    }

    const qint64 expectedW = static_cast<qint64>(input.image.width()) * options.scale;
    const qint64 expectedH = static_cast<qint64>(input.image.height()) * options.scale;
    const qint64 expectedBytes = expectedW * expectedH * 4;
    QStorageInfo storage(AppPaths::cacheDir());
    if (storage.isValid() && storage.bytesAvailable() > 0
        && storage.bytesAvailable() < expectedBytes * 3) {
        emit operationBlocked(tr("There is not enough free disk space for the AI Upscale temporary files."));
        return 0;
    }

    const UpscaleJobId jobId = m_upscaleService->upscale(input, options);
    if (jobId == 0)
        return 0;
    m_pendingUpscaleJobs[jobId] = options;
    if (sourceLayerIndex >= 0)
        m_pendingUpscaleLayers[jobId] = sourceLayerIndex;
    emit progressOperationStarted(jobId, tr("AI Upscale"), true);
    emit progressOperationProgressChanged(jobId, -1);
    emit progressOperationMessageChanged(jobId, tr("Preparing image..."));
    return jobId;
}

bool ImageController::convertDocumentToProfile(const ColorProfile& destinationProfile,
                                               const ColorConversionOptions& options)
{
    if (!m_doc || !m_doc->colorProfile().isValid() || !destinationProfile.isValid())
        return false;
    if (m_doc->colorProfile().equivalentTo(destinationProfile))
        return false;

    const ColorProfile beforeProfile = m_doc->colorProfile();
    const ColorProfileSource beforeSource = m_doc->profileSource();
    auto beforeRoots = cloneRootsSnapshot(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    ColorProfile afterProfile = destinationProfile;
    afterProfile.setSource(ColorProfileSource::ConvertedByUser);

    convertDocumentContentToProfile(m_doc, beforeProfile, afterProfile, options);
    m_doc->setColorProfile(afterProfile);
    m_doc->setProfileSource(ColorProfileSource::ConvertedByUser);
    m_doc->markColorStateDirty();
    markDocumentStateDirty(m_doc);

    auto afterRoots = cloneRootsSnapshot(m_doc->roots);
    auto command = std::make_unique<ConvertDocumentProfileCommand>(
        m_doc,
        std::move(beforeRoots),
        beforeActive,
        beforeSelected,
        beforeProfile,
        beforeSource,
        std::move(afterRoots),
        m_doc->activeFlatIndex,
        m_doc->selectedFlatIndices,
        afterProfile,
        ColorProfileSource::ConvertedByUser);
    m_history.push(std::move(command));

    emit layerChanged(m_doc->activeFlatIndex);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
    emit documentChanged();
    return true;
}

bool ImageController::checkDestructiveOp(Layer* layer)
{
    if (!layer) return false;
    if (layer->isTextLayer() || layer->isShapeLayer()) {
        emit operationBlocked(tr("This operation requires a rasterized layer."));
        return false;
    }
    // Adjustment layers have no editable pixels — only their mask is paintable.
    if (layer->owner && layer->owner->type == LayerTreeNode::Type::Adjustment) {
        emit operationBlocked(tr("Adjustment layers do not have editable pixels."));
        return false;
    }
    // Central pixel-edit lock gate: Lock Image Pixels / Lock All block every
    // destructive raster op (filters, fill, brush, clear, etc.) that routes
    // through here, so the check lives in one place instead of each tool.
    if (layer->owner && layer->owner->isPixelEditingLocked()) {
        emit operationBlocked(tr("The pixels of this layer are locked."));
        return false;
    }
    // A distort layer's pixels already live in cpuImage; a destructive edit just
    // forfeits re-editability, so silently drop the distort metadata instead of
    // blocking (no pixel change needed).
    if (layer->isDistortLayer())
        layer->distortData.reset();
    return true;
}

// Mask-edit lock gate. Only the master Lock All protects the mask; Lock Image
// Pixels deliberately leaves the mask editable (standard lock semantics).
bool ImageController::checkMaskEditable(Layer* layer)
{
    if (!layer) return false;
    if (layer->owner && layer->owner->isFullyLocked()) {
        emit operationBlocked(tr("This layer is fully locked."));
        return false;
    }
    return true;
}

bool ImageController::anyFullyLockedLayer(bool visibleOnly) const
{
    if (!m_doc) return false;
    for (auto* n : m_doc->flatten()) {
        if (!n) continue;
        if (visibleOnly && !n->isVisible()) continue;
        if (n->isFullyLocked()) return true;
    }
    return false;
}

void ImageController::fillActiveLayer(const QColor& color)
{
    auto* layer = activeLayer();
    if (!layer || !m_doc) return;
    if (!checkDestructiveOp(layer)) return;

    prepareRasterCelForEdit();
    layer = activeLayer();
    layer->renderRasterStorage().clear();
    layer->writableRenderCpuImage().fill(color);
    markLayerDirty(layer);
    layer->textureOutdated = true;
    syncLayerToGpu(layer);
    emit imageChanged();
}

bool ImageController::applyGradient(const GradientApplication& application)
{
    if (!m_doc) {
        emit operationBlocked(tr("No document is open."));
        return false;
    }

    auto* node = m_doc->activeNode();
    auto* layer = m_doc->activeLayer();
    if (!node || !layer) {
        emit operationBlocked(tr("No active layer."));
        return false;
    }
    if (node->type != LayerTreeNode::Type::Layer) {
        emit operationBlocked(tr("Gradient can only be applied to a layer."));
        return false;
    }
    // Mask is the edit target: render the gradient into the mask (grayscale),
    // not the RGB pixels. Comes before the pixel guards (pixel-lock, text/shape)
    // because text/shape layers can have an editable mask.
    if (isEditingMask() && !layer->maskImage.isNull())
        return applyGradientToMask(application);
    if (node->isPixelEditingLocked()) {
        emit operationBlocked(tr("Layer pixels are locked."));
        return false;
    }
    if (layer->isTextLayer()) {
        emit operationBlocked(tr("Gradient fill for text layers is not supported yet."));
        return false;
    }
    if (layer->isShapeLayer()) {
        emit operationBlocked(tr("Gradient fill for shape layers is not supported yet."));
        return false;
    }
    if (layer->cpuImage.isNull() && !layer->rasterStorage.isEnabled()) {
        emit operationBlocked(tr("Active layer has no raster pixels."));
        return false;
    }

    const bool flushedRaster = layer->rasterStorage.isEnabled();
    if (flushedRaster) {
        layer->cpuImage = flushLayerToCpuImage(layer);
        layer->rasterStorage.clear();
    }

    QImage before = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
    GradientRenderRequest request;
    request.definition = application.definition;
    request.targetSize = before.size();
    request.startPoint = application.startPoint;
    request.endPoint = application.endPoint;
    request.opacity = application.opacity;
    request.blendMode = application.blendMode;
    request.baseImage = before;
    request.lockAlpha = application.lockAlpha || node->isTransparencyLocked();

    if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
        cv::Mat layerMask = makeLayerMask(layer);
        if (!layerMask.empty()) {
            request.selectionMask = QImage(layerMask.data,
                                           layerMask.cols,
                                           layerMask.rows,
                                           static_cast<int>(layerMask.step),
                                           QImage::Format_Grayscale8).copy();
        }
    }

    QImage after = GradientRenderer::compositeGradient(request)
        .convertToFormat(QImage::Format_RGBA8888);
    if (after.isNull() || after == before)
        return false;

    GradientApplication stored = application;
    stored.definition.normalize();
    stored.lockAlpha = request.lockAlpha;
    stored.affectedRegion = stored.affectedRegion.isEmpty()
        ? QRect(QPoint(0, 0), before.size())
        : stored.affectedRegion;

    auto command = std::make_unique<ApplyGradientCommand>(
        m_doc,
        m_doc->activeFlatIndex,
        before,
        after,
        stored,
        tr("Apply Gradient"));
    command->execute();
    // Keep the dab-layer representation (see delete_selected); undo/redo stays
    // consistent because ApplyGradientCommand::apply is representation-
    // preserving (rebuilds the tiles when rasterStorage is enabled).
    if (flushedRaster)
        layer->replaceRasterStorageWithImage(layer->cpuImage);
    syncLayerToGpu(layer);
    emit imageChanged();
    emit layerChanged(m_doc->activeFlatIndex);
    m_history.push(std::move(command));
    return true;
}

bool ImageController::applyGradientToMask(const GradientApplication& application)
{
    auto* layer = m_doc ? m_doc->activeLayer() : nullptr;
    if (!layer || layer->maskImage.isNull())
        return false;
    if (!checkMaskEditable(layer))
        return false;

    QImage before = layer->maskImage.copy();
    const QPoint origin = layer->maskOrigin;
    const QSize maskSize = layer->maskImage.size();

    // Grayscale mask as RGBA so the renderer's blend modes composite against the
    // existing mask (Qt maps Grayscale8 -> R=G=B, alpha 255).
    GradientRenderRequest request;
    request.definition = application.definition;
    request.targetSize = maskSize;
    // Gradient endpoints arrive in layer-pixel space; the mask lives at maskOrigin
    // within that space, so shift into mask-local coordinates.
    request.startPoint = application.startPoint - QPointF(origin);
    request.endPoint = application.endPoint - QPointF(origin);
    request.opacity = application.opacity;
    request.blendMode = application.blendMode;
    request.baseImage = layer->maskImage.convertToFormat(QImage::Format_RGBA8888);
    request.lockAlpha = false;

    if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
        cv::Mat maskSel = selectionInMaskSpace(layer);
        if (!maskSel.empty()) {
            request.selectionMask = QImage(maskSel.data,
                                           maskSel.cols,
                                           maskSel.rows,
                                           static_cast<int>(maskSel.step),
                                           QImage::Format_Grayscale8).copy();
        }
    }

    QImage after = GradientRenderer::compositeGradient(request);
    if (after.isNull())
        return false;
    // Luminance of the composited gradient becomes the new mask value.
    QImage newMask = after.convertToFormat(QImage::Format_Grayscale8);
    if (newMask.isNull() || newMask == before)
        return false;

    layer->maskImage = newMask;
    layer->maskThumbDirty = true;
    if (layer->isShapeLayer())
        layer->textureOutdated = true;
    if (layer->owner)
        layer->owner->invalidateEffects();
    syncLayerMaskToGpu(layer);

    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, m_doc->activeFlatIndex, std::move(before), newMask.copy(),
        tr("Gradient Mask"), origin, origin));

    emit layerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
    return true;
}

bool ImageController::applyFillBucketToMask(const QPoint& layerPos,
                                           const QColor& color, float tolerance)
{
    auto* layer = m_doc ? m_doc->activeLayer() : nullptr;
    if (!layer || layer->maskImage.isNull())
        return false;
    if (!checkMaskEditable(layer))
        return false;

    QImage before = layer->maskImage.copy();
    const QPoint origin = layer->maskOrigin;
    const int gray = qGray(color.rgb());   // color luminance → mask value

    cv::Mat maskMat(layer->maskImage.height(), layer->maskImage.width(), CV_8UC1,
                    layer->maskImage.bits(),
                    static_cast<size_t>(layer->maskImage.bytesPerLine()));

    const bool hasSelection = m_doc->selection.active() && !m_doc->selection.isEmpty();
    if (hasSelection) {
        cv::Mat sel = selectionInMaskSpace(layer);
        if (sel.empty())
            return false;
        // Feather-aware blend of the flat grayscale into the mask by coverage
        // (blendByMask is RGBA-only, so do the single-channel lerp inline).
        const int h = std::min(maskMat.rows, sel.rows);
        const int w = std::min(maskMat.cols, sel.cols);
        for (int y = 0; y < h; ++y) {
            uchar* d = maskMat.ptr<uchar>(y);
            const uchar* m = sel.ptr<uchar>(y);
            for (int x = 0; x < w; ++x) {
                const int mv = m[x];
                if (mv == 0) continue;
                if (mv == 255) { d[x] = static_cast<uchar>(gray); continue; }
                const int dv = d[x];
                d[x] = static_cast<uchar>(dv + ((gray - dv) * mv + 127) / 255);
            }
        }
    } else {
        const QPoint mp(layerPos.x() - origin.x(), layerPos.y() - origin.y());
        if (mp.x() < 0 || mp.y() < 0 || mp.x() >= maskMat.cols || mp.y() >= maskMat.rows)
            return false;
        cv::Mat result = ImageEngine::fillRegion(maskMat, mp.x(), mp.y(),
                                                 cv::Scalar(gray), tolerance);
        if (result.empty())
            return false;
        result.copyTo(maskMat);   // fillRegion clones; copy back into live buffer
    }

    if (layer->maskImage == before)
        return false;

    layer->maskThumbDirty = true;
    if (layer->isShapeLayer())
        layer->textureOutdated = true;
    if (layer->owner)
        layer->owner->invalidateEffects();
    syncLayerMaskToGpu(layer);

    m_history.push(std::make_unique<MaskEditCommand>(
        m_doc, m_doc->activeFlatIndex, std::move(before), layer->maskImage.copy(),
        tr("Fill Mask"), origin, origin));

    emit layerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
    return true;
}

static bool applyResizeImageState(Document* doc, const ImageResizeOptions& options)
{
    if (!doc)
        return false;

    const QSize oldSize = doc->size;
    const bool allowResample = options.resampleImage;
    const QSize requestedSize = options.targetSize.isValid() && !options.targetSize.isEmpty()
        ? options.targetSize
        : oldSize;
    const QSize targetSize = allowResample ? requestedSize : oldSize;

    if (targetSize.width() <= 0 || targetSize.height() <= 0)
        return false;

    const bool sizeChanged = oldSize != targetSize;
    const bool resolutionChanged = options.updateResolution
        && options.resolutionDpi > 0.0
        && std::abs(doc->resolutionDpi - options.resolutionDpi) > 0.0001;

    if (!sizeChanged && !resolutionChanged)
        return false;

    const double sx = sizeChanged
        ? static_cast<double>(targetSize.width()) / std::max(1, oldSize.width())
        : 1.0;
    const double sy = sizeChanged
        ? static_cast<double>(targetSize.height()) / std::max(1, oldSize.height())
        : 1.0;
    const double styleFactor = styleScaleFactor(sx, sy);

    auto flat = doc->flatten();
    for (auto* node : flat) {
        if (!node)
            continue;

        if (sizeChanged && options.scaleStyles && !node->effects.empty()) {
            for (auto& effect : node->effects)
                scaleStyleParams(effect.params, effect.type, styleFactor);
        }
        node->invalidateEffects();
        node->thumbnailDirty = true;

        if (node->type != LayerTreeNode::Type::Layer || !node->layer)
            continue;

        auto* layer = node->layer.get();

        if (sizeChanged && allowResample) {
            if (layer->isTextLayer() && layer->textData) {
                auto& td = *layer->textData;
                td.box.width = static_cast<float>(td.box.width * sx);
                td.box.height = static_cast<float>(td.box.height * sy);
                td.lineSpacing = static_cast<float>(std::max(0.1, td.lineSpacing * styleFactor));
                for (auto& span : td.spans) {
                    span.fontSize = static_cast<float>(
                        std::max(0.1f, span.fontSize * static_cast<float>(styleFactor)));
                    span.letterSpacing = static_cast<float>(span.letterSpacing * styleFactor);
                    span.kerning = static_cast<float>(span.kerning * styleFactor);
                    span.baselineShift = static_cast<float>(span.baselineShift * styleFactor);
                }
                // Paragraph spacing scales with the overall size; lineHeight is a
                // multiplier so it stays constant. Indents scale horizontally.
                for (auto& para : td.paragraphs) {
                    para.spaceBefore = static_cast<float>(para.spaceBefore * styleFactor);
                    para.spaceAfter = static_cast<float>(para.spaceAfter * styleFactor);
                    para.firstLineIndent = static_cast<float>(para.firstLineIndent * sx);
                    para.leftIndent = static_cast<float>(para.leftIndent * sx);
                    para.rightIndent = static_cast<float>(para.rightIndent * sx);
                }
                td.dirty = true;

                TextRenderer renderer;
                renderer.render(td, layer->cpuImage);
                layer->cpuImage = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
            } else if (layer->isShapeLayer() && layer->shapeData) {
                QImage rendered = ShapeRenderer::render(*layer->shapeData, targetSize);
                if (!rendered.isNull())
                    layer->cpuImage = rendered.convertToFormat(QImage::Format_RGBA8888);
                layer->shapeCache.dirty = true;
            } else {
                layer->cpuImage = resampleRgbaImage(layer->cpuImage, targetSize, options.interpolation);
                layer->rasterStorage.clear();
            }

            if (!layer->maskImage.isNull()) {
                // Scale the mask by the document factors instead of forcing it
                // to the document size: shape layers re-render at their own
                // bounds size and expanded masks have their own dimensions, so
                // a doc-sized mask would stretch out of alignment. The origin
                // scales with the same factors to stay pixel-aligned.
                const QSize maskTarget(
                    std::max(1, static_cast<int>(std::lround(layer->maskImage.width() * sx))),
                    std::max(1, static_cast<int>(std::lround(layer->maskImage.height() * sy))));
                layer->maskImage = resampleMaskImage(layer->maskImage, maskTarget, options.interpolation);
                layer->maskOrigin = QPoint(
                    static_cast<int>(std::lround(layer->maskOrigin.x() * sx)),
                    static_cast<int>(std::lround(layer->maskOrigin.y() * sy)));
                layer->maskThumbDirty = true;
                layer->maskTextureOutdated = true;
            }
        }

        if (layer->rasterStorage.isEnabled()) {
            layer->rasterStorage.setBaseSize(layer->cpuImage.size());
            layer->rasterStorage.markAllGpuDirty();
            layer->pendingGpuUpload = true;
        }
        if (layer->cpuImage.width() * layer->cpuImage.height() >= doc->perfConfig.autoTileMinArea)
            layer->enableTiling(doc->tileSize());
        else
            layer->disableTiling();

        layer->textureOutdated = true;
        if (layer->owner)
            layer->owner->thumbnailDirty = true;
    }

    if (sizeChanged && allowResample) {
        doc->size = targetSize;
        doc->selection.image() = resampleMaskImage(
            doc->selection.image(), targetSize, options.interpolation);
        doc->selection.resize(targetSize.width(), targetSize.height());

        auto guides = doc->guideManager.guides();
        for (auto& guide : guides) {
            if (guide.orientation == GuideOrientation::Vertical) {
                guide.position = std::clamp<qreal>(
                    guide.position * sx, 0.0, std::max(0, targetSize.width()));
            } else {
                guide.position = std::clamp<qreal>(
                    guide.position * sy, 0.0, std::max(0, targetSize.height()));
            }
        }
        doc->guideManager.setGuides(std::move(guides));
    }

    if (resolutionChanged)
        doc->resolutionDpi = options.resolutionDpi;

    ++doc->compositionGeneration;
    return true;
}

bool ImageController::resizeImage(const ImageResizeOptions& options)
{
    if (!m_doc)
        return false;

    if (auto* aj = AsyncJobSystem::instance())
        aj->cancelAll();
    m_pendingAsyncJobs.clear();

    ResizeDocumentState beforeState = captureResizeState(m_doc);
    if (!applyResizeImageState(m_doc, options))
        return false;

    ResizeDocumentState afterState = captureResizeState(m_doc);
    m_history.push(std::make_unique<ResizeDocumentCommand>(
        m_doc,
        std::move(beforeState),
        std::move(afterState),
        tr("Resize Image")));

    emit imageChanged();
    emit documentChanged();
    return true;
}

bool ImageController::resizeImageAsync(const ImageResizeOptions& options)
{
    return runDocumentStateOperationAsync(
        tr("Resizing Image"),
        tr("Resize Image"),
        [options](Document& doc) { return applyResizeImageState(&doc, options); });
}

static bool applyResizeCanvasState(Document* doc, const CanvasResizeOptions& options)
{
    if (!doc || !options.targetSize.isValid() || options.targetSize.isEmpty())
        return false;
    if (options.targetSize == doc->size)
        return false;

    const QSize oldSize = doc->size;
    const QSize newSize = options.targetSize;
    const QPoint offset = anchorOffsetPx(options.anchor, oldSize, newSize);
    const QTransform adjust = oldDocToNewDocNoScale(oldSize, newSize,
                                                     offset.x(), offset.y());

    for (auto& root : doc->roots) {
        if (root)
            root->setBaseTransform(adjust * root->transform());
    }

    doc->size = newSize;
    doc->selection.image() = translateMaskToCanvas(doc->selection.image(), newSize, offset);
    doc->selection.resize(newSize.width(), newSize.height());

    auto guides = doc->guideManager.guides();
    for (auto& guide : guides) {
        if (guide.orientation == GuideOrientation::Vertical) {
            guide.position = std::clamp<qreal>(
                guide.position + offset.x(), 0.0, std::max(0, newSize.width()));
        } else {
            guide.position = std::clamp<qreal>(
                guide.position + offset.y(), 0.0, std::max(0, newSize.height()));
        }
    }
    doc->guideManager.setGuides(std::move(guides));

    if (options.fillExtension
        && options.extensionColor.alpha() > 0
        && canvasExpanded(oldSize, newSize)) {
        const QRect oldRect(offset, oldSize);
        QImage fillImage = buildCanvasExtensionFill(newSize, oldRect, options.extensionColor);
        if (!fillImage.isNull()) {
            auto node = std::make_unique<LayerTreeNode>();
            node->type = LayerTreeNode::Type::Layer;
            node->name = QObject::tr("Canvas Extension Fill");
            node->layer = std::make_shared<Layer>();
            node->layer->name = node->name;
            node->layer->cpuImage = std::move(fillImage);
            node->layer->owner = node.get();
            node->layer->textureOutdated = true;
            node->layer->pendingGpuUpload = true;
            node->layer->resetTransform = node->transform();
            node->layer->hasResetTransform = true;

            if (newSize.width() * newSize.height() >= doc->perfConfig.autoTileMinArea)
                node->layer->enableTiling(doc->tileSize());
            else
                node->layer->disableTiling();

            doc->roots.push_back(std::move(node));
        }
    }

    ++doc->compositionGeneration;
    return true;
}

bool ImageController::resizeCanvas(const CanvasResizeOptions& options)
{
    if (!m_doc)
        return false;

    if (auto* aj = AsyncJobSystem::instance())
        aj->cancelAll();
    m_pendingAsyncJobs.clear();

    ResizeDocumentState beforeState = captureResizeState(m_doc);
    if (!applyResizeCanvasState(m_doc, options))
        return false;

    ResizeDocumentState afterState = captureResizeState(m_doc);
    m_history.push(std::make_unique<ResizeDocumentCommand>(
        m_doc,
        std::move(beforeState),
        std::move(afterState),
        tr("Resize Canvas")));

    emit imageChanged();
    emit documentChanged();
    return true;
}

bool ImageController::resizeCanvasAsync(const CanvasResizeOptions& options)
{
    return runDocumentStateOperationAsync(
        tr("Resizing Canvas"),
        tr("Resize Canvas"),
        [options](Document& doc) { return applyResizeCanvasState(&doc, options); });
}

void ImageController::resizeDocument(const QSize& newSize)
{
    ImageResizeOptions options;
    options.targetSize = newSize;
    options.resampleImage = true;
    options.scaleStyles = true;
    options.interpolation = ResizeInterpolation::BicubicAutomatic;
    options.updateResolution = false;
    resizeImage(options);
}

static bool applyCropDocumentState(Document* doc, const QRect& cropRect)
{
    if (!doc)
        return false;
    const QRect docBounds(QPoint(0, 0), doc->size);
    const QRect crop = cropRect.normalized().intersected(docBounds);
    const int cropX = crop.x();
    const int cropY = crop.y();
    const int cropW = crop.width();
    const int cropH = crop.height();
    if (cropW <= 0 || cropH <= 0)
        return false;
    if (cropX == 0 && cropY == 0 && cropW == doc->size.width()
        && cropH == doc->size.height())
        return false;

    const double docW = static_cast<double>(doc->size.width());
    const double docH = static_cast<double>(doc->size.height());

    std::vector<int> rootFlatIndices;
    auto flat = doc->flatten();
    for (int fi = 0; fi < static_cast<int>(flat.size()); ++fi) {
        auto* node = flat[fi];
        if (node && !node->parent)
            rootFlatIndices.push_back(fi);
    }

    const double sx = docW / static_cast<double>(cropW);
    const double sy = docH / static_cast<double>(cropH);
    const double tx = (docW - 2.0 * cropX - cropW) / static_cast<double>(cropW);
    const double ty = (2.0 * cropY + cropH - docH) / static_cast<double>(cropH);
    const QTransform oldDocToNewDoc(
        sx, 0.0, 0.0,
        0.0, sy, 0.0,
        tx, ty, 1.0);

    for (int flatIndex : rootFlatIndices) {
        auto* node = doc->nodeAt(flatIndex);
        if (node)
            node->setBaseTransform(oldDocToNewDoc * node->transform());
    }

    doc->size = QSize(cropW, cropH);
    doc->selection.resize(cropW, cropH);
    doc->selection.clear();
    doc->selection.setActive(false);
    ++doc->compositionGeneration;
    return true;
}

void ImageController::cropDocument(const QRect& cropRect)
{
    if (!m_doc)
        return;

    ResizeDocumentState beforeState = captureResizeState(m_doc);
    if (!applyCropDocumentState(m_doc, cropRect))
        return;

    ResizeDocumentState afterState = captureResizeState(m_doc);
    m_history.push(std::make_unique<ResizeDocumentCommand>(
        m_doc, std::move(beforeState), std::move(afterState), tr("Crop Document")));

    emit imageChanged();
    emit documentChanged();
}

bool ImageController::cropDocumentAsync(const QRect& cropRect)
{
    return runDocumentStateOperationAsync(
        tr("Cropping Document"),
        tr("Crop Document"),
        [cropRect](Document& doc) { return applyCropDocumentState(&doc, cropRect); });
}

// Composites the rasterStorage tile data on top of cpuImage into a full base-size QImage.
// Use this instead of layer->renderImage() whenever you need a correct full-size snapshot
// of a layer's visual state (tile bounds ≠ base size when only part of the canvas was painted).
static QImage flushLayerToCpuImage(Layer* layer)
{
    if (!layer) return {};
    const auto& storage = layer->renderRasterStorage();
    const QImage& baseImage = layer->renderCpuImage();
    if (!storage.isEnabled())
        return baseImage.copy();

    QRect tileBounds;
    QImage tileData = storage.toImage(&tileBounds);
    const QSize baseSize = storage.baseSize();

    QImage full(baseSize, QImage::Format_RGBA8888);
    full.fill(Qt::transparent);

    QPainter p(&full);
    if (!baseImage.isNull())
        p.drawImage(0, 0, baseImage.convertToFormat(QImage::Format_RGBA8888));
    if (!tileData.isNull() && !tileBounds.isEmpty())
        p.drawImage(tileBounds.topLeft(), tileData);
    p.end();

    return full;
}

static QImage compositeDocumentImage(Document* doc, RenderTargetType targetType)
{
    RenderContext ctx;
    ctx.document = doc;
    ctx.targetType = targetType;
    if (doc)
        ctx.outputSize = doc->size;
    return DocumentCompositor::composite(doc, ctx);
}

static void applyMergedImageToNode(Document* doc, LayerTreeNode* node, QImage image)
{
    if (!doc || !node || !node->layer || image.isNull())
        return;
    auto* layer = node->layer.get();
    layer->rasterStorage.clear();
    layer->cpuImage = std::move(image);
    layer->shapeData.reset();
    layer->textData.reset();
    layer->shapeCache = Layer::ShapeRenderCache{};
    LayerMaskSnapshot::clear(layer);
    layer->textureOutdated = true;
    layer->pendingGpuUpload = true;
    if (layer->cpuImage.width() * layer->cpuImage.height() >= doc->perfConfig.autoTileMinArea)
        layer->enableTiling(doc->tileSize());
    else
        layer->disableTiling();
    // The merged pixels are in DOCUMENT space. If the node still sits inside
    // transformed ancestor groups (Merge Down of two siblings in a group), those
    // transforms remain live and would re-transform the already-transformed
    // pixels — cancel them so the accumulated transform is identity. At root
    // level this reduces to the identity transform.
    node->setBaseTransform(node->parent
        ? node->parent->accumulatedTransform().inverted()
        : QTransform());
    // The merged pixels already bake every consumed layer's blend mode and
    // opacity (composited by DocumentCompositor). The result node must therefore
    // composite as Normal at full opacity, otherwise its own retained
    // blend/opacity would be applied a second time.
    node->setBaseBlendMode(BlendMode::Normal);
    node->setBaseOpacity(1.0f);
    node->effects.clear();
    node->thumbnailDirty = true;
    node->sourceDirty = true;
    node->invalidateEffects();
}

// (flatIndexOfNode is defined earlier in this file.) Re-resolve a node's flat
// index before each removal, since the flat index space shifts as nodes go away.

static bool nodeIsDescendantOf(const LayerTreeNode* node, const LayerTreeNode* ancestor)
{
    for (const LayerTreeNode* p = node ? node->parent : nullptr; p; p = p->parent)
        if (p == ancestor)
            return true;
    return false;
}

// Effective visibility: the node AND every ancestor group must be visible —
// exactly the condition under which the compositor renders it. Merge decisions
// must use this, not the node's own flag: a visible-flagged layer inside a
// hidden group contributes nothing to the composite, so consuming it would
// silently destroy its pixels.
static bool nodeEffectivelyVisible(const LayerTreeNode* node)
{
    for (const LayerTreeNode* p = node; p; p = p->parent)
        if (!p->isVisible())
            return false;
    return node != nullptr;
}

// Drops every clipped (Single-Layer-Mode) adjustment child of a Layer node. Used
// after a merge bakes the layer's adjustments into its pixels, so they are not
// re-applied on top of the already-baked result.
static void removeClippedAdjustmentChildren(LayerTreeNode* node)
{
    if (!node)
        return;
    auto& ch = node->children;
    ch.erase(std::remove_if(ch.begin(), ch.end(),
                 [](const std::unique_ptr<LayerTreeNode>& c) {
                     return c && c->type == LayerTreeNode::Type::Adjustment;
                 }),
             ch.end());
    node->invalidateEffects();
}

// Removes a list of nodes by identity, re-resolving the flat index before each
// removal and skipping nodes already gone (e.g. taken as part of an ancestor
// subtree). Then prunes any group that was emptied by the removals.
static void removeNodesByIdentity(Document* doc, const std::vector<LayerTreeNode*>& nodes)
{
    if (!doc)
        return;
    std::set<LayerTreeNode*> touchedParents;
    for (auto* n : nodes) {
        if (!n)
            continue;
        if (n->parent && n->parent->type == LayerTreeNode::Type::Group)
            touchedParents.insert(n->parent);
        const int idx = flatIndexOfNode(doc, n);
        if (idx >= 0)
            doc->takeNodeAt(idx);
    }
    // Prune groups that the merge emptied (their content was consumed). A group
    // that still holds invisible/untouched children is preserved.
    for (auto* g : touchedParents) {
        if (!g || g->type != LayerTreeNode::Type::Group)
            continue;
        if (!g->children.empty())
            continue;
        const int idx = flatIndexOfNode(doc, g);
        if (idx >= 0)
            doc->takeNodeAt(idx);
    }
}

// A merge composites into a single QImage. Qt caps a QImage at ~2 GB (roughly
// 23000×23000 for 32-bit), returning a NULL image (not throwing) past that. A
// bare `return false` here surfaces only the generic "operation failed", so
// diagnose the null result and throw a descriptive message the async runner's
// catch turns into a user-visible reason.
static void requireMergeComposite(const QImage& merged, const Document* doc)
{
    if (!merged.isNull())
        return;
    const qint64 w = doc ? doc->size.width() : 0;
    const qint64 h = doc ? doc->size.height() : 0;
    qWarning() << "[Merge] composite returned a null image; document size ="
               << w << "x" << h
               << "— likely exceeds QImage's ~2GB per-image limit.";
    throw std::runtime_error(
        QObject::tr("Merge failed: the composited result could not be created. "
                    "The document (%1×%2 px) is too large — it exceeds the maximum "
                    "image size that can be merged in one piece.")
            .arg(w).arg(h).toStdString());
}

// True when the layer would contribute at least one pixel to a composite:
// painted dab tiles (rasterStorage), any cpuImage pixel with alpha, or a live
// text/shape source (conservatively treated as content). Merge Down uses this
// to decide whether a side's blend mode can survive the merge (see below).
static bool layerHasVisibleContent(const Layer* layer)
{
    if (!layer)
        return false;
    if (layer->isTextLayer() || layer->isShapeLayer())
        return true;
    if (layer->rasterStorage.isEnabled()
        && !layer->rasterStorage.contentBounds().isEmpty())
        return true;
    return !layer->cpuContentBounds().isEmpty();
}

static bool applyMergeLayersState(Document* doc, int srcFlat, int dstFlat)
{
    if (!doc || srcFlat == dstFlat)
        return false;
    auto* srcNode = doc->nodeAt(srcFlat);
    auto* dstNode = doc->nodeAt(dstFlat);
    if (!srcNode || !dstNode) {
        qWarning() << "[MergeDown] aborted: invalid src/dst node at flat"
                   << srcFlat << dstFlat;
        return false;
    }
    // Merge Down consolidates two real Layer nodes; an adjustment endpoint is
    // never a valid merge target.
    if (srcNode->type != LayerTreeNode::Type::Layer
        || dstNode->type != LayerTreeNode::Type::Layer
        || !srcNode->layer || !dstNode->layer) {
        qWarning() << "[MergeDown] aborted: src/dst is not a pixel/shape/text layer";
        return false;
    }
    // Same-container only: with src and dst as siblings, every group traversed
    // by the subset composite is a common ancestor, which is what makes the
    // pass-through composite below (and the ancestor-transform cancellation in
    // applyMergedImageToNode) correct.
    if (srcNode->parent != dstNode->parent) {
        qWarning() << "[MergeDown] aborted: src/dst are not siblings of the same container";
        return false;
    }
    // A hidden endpoint contributes nothing to the composite, so merging would
    // silently destroy its pixels (src) or overwrite them with content that
    // ignores it (dst).
    if (!nodeEffectivelyVisible(srcNode) || !nodeEffectivelyVisible(dstNode)) {
        qWarning() << "[MergeDown] aborted: src/dst is hidden";
        return false;
    }

    // Visual source of truth: composite ONLY the two layers, in their real stack
    // order, with their own clipped adjustments baked in (computeEffectedImage)
    // and their masks/opacity/blend honoured. Normal-Mode adjustment siblings are
    // NOT consumed by Merge Down (they affect the whole stack, not just these two
    // layers), so applyAdjustments stays false. Ancestor groups are composited
    // pass-through: their opacity/blend/effects stay live on the tree (the result
    // remains their child), so baking them into the pixels would apply them twice.
    RenderContext ctx;
    ctx.document = doc;
    ctx.targetType = RenderTargetType::Flatten;
    ctx.outputSize = doc->size;
    // Captured before the merge mutates the nodes — used to decide whether a
    // blend mode can survive the merge (below).
    const bool srcHasContent = layerHasVisibleContent(srcNode->layer.get());
    const bool dstHasContent = layerHasVisibleContent(dstNode->layer.get());
    const BlendMode srcBlend = srcNode->blendMode();
    const BlendMode dstBlend = dstNode->blendMode();

    QImage merged = DocumentCompositor::compositeSubset(
        doc, {srcNode, dstNode}, /*applyAdjustments=*/false, ctx,
        /*ancestorGroupsPassThrough=*/true);
    requireMergeComposite(merged, doc);

    // dst keeps the merged pixels; its own clipped adjustments are already baked
    // into `merged`, so drop them to avoid a double application.
    removeClippedAdjustmentChildren(dstNode);
    applyMergedImageToNode(doc, dstNode, std::move(merged));

    // A non-Normal blend mode reads the BACKDROP — and the pair was composited
    // against a transparent one, where W3C compositing falls back to the plain
    // source colour (i.e. the blend bakes as a no-op wherever the other layer
    // has no pixels). When exactly ONE side contributed pixels, the pair image
    // is therefore just that side's pixels with its blend NOT baked, and the
    // appearance against the layers below is preserved exactly by carrying that
    // side's blend mode onto the result instead of Normal. The everyday case:
    // dabs painted with Multiply/Screen/etc. merged down onto an empty layer —
    // resetting to Normal visibly "dropped" the blend mode. When BOTH sides
    // have pixels the blend against dst's content is baked and the result stays
    // Normal (matching the pair's rendered look; the interaction with layers
    // below dst's transparent areas is not representable in a single layer).
    if (srcHasContent != dstHasContent)
        dstNode->setBaseBlendMode(srcHasContent ? srcBlend : dstBlend);

    // src is consumed; its clipped adjustment children leave with the subtree.
    const int srcIdxNow = flatIndexOfNode(doc, srcNode);
    if (srcIdxNow >= 0)
        doc->takeNodeAt(srcIdxNow);

    const int newActive = flatIndexOfNode(doc, dstNode);
    doc->activeFlatIndex = std::clamp(newActive, 0, std::max(0, doc->flatCount() - 1));
    doc->selectedFlatIndices.clear();
    if (doc->activeFlatIndex >= 0)
        doc->selectedFlatIndices.insert(doc->activeFlatIndex);
    ++doc->compositionGeneration;
    return true;
}

static bool applyMergeVisibleState(Document* doc)
{
    if (!doc || doc->flatCount() < 2 || doc->activeFlatIndex < 0) {
        qWarning() << "[MergeVisible] aborted: doc/flatCount/activeIndex invalid";
        return false;
    }

    auto flat = doc->flatten();
    if (doc->activeFlatIndex >= static_cast<int>(flat.size()))
        return false;

    auto* activeNode = flat[doc->activeFlatIndex];
    // The result must land on a real, EFFECTIVELY visible Layer (its ancestors
    // visible too — a visible-flagged layer inside a hidden group renders
    // nothing and cannot hold the visible composite). If the active node does
    // not qualify (e.g. an adjustment is selected, or it sits in a hidden
    // group), fall back to the top-most effectively visible layer.
    if (!activeNode || activeNode->type != LayerTreeNode::Type::Layer
        || !activeNode->layer || !nodeEffectivelyVisible(activeNode)) {
        activeNode = nullptr;
        for (auto* n : flat) {
            if (n && n->type == LayerTreeNode::Type::Layer && n->layer
                && nodeEffectivelyVisible(n)) {
                activeNode = n;
                break;
            }
        }
    }
    if (!activeNode) {
        qWarning() << "[MergeVisible] aborted: no visible layer to hold the result";
        return false;
    }

    int visibleLayerCount = 0;
    int visibleAdjCount = 0;
    for (auto* n : flat) {
        if (!n || !nodeEffectivelyVisible(n))
            continue;
        if (n->type == LayerTreeNode::Type::Layer && n->layer)
            ++visibleLayerCount;
        else if (n->type == LayerTreeNode::Type::Adjustment && n->isAdjustmentLayer())
            ++visibleAdjCount;
    }
    // Something is worth merging when there are ≥2 visible layers, OR a single
    // visible layer plus at least one visible adjustment to bake into it. A lone
    // layer with no adjustments would merge to itself — a no-op.
    const bool somethingToMerge = visibleLayerCount >= 2
        || (visibleLayerCount >= 1 && visibleAdjCount >= 1);
    if (!somethingToMerge) {
        qWarning() << "[MergeVisible] nothing to merge: visibleLayers="
                   << visibleLayerCount << "visibleAdjustments=" << visibleAdjCount;
        return false;
    }

    // Visual source of truth: the full visible composite (invisible layers are
    // skipped, every visible adjustment is applied to the layers it affects).
    QImage merged = compositeDocumentImage(doc, RenderTargetType::Flatten);
    requireMergeComposite(merged, doc);

    // Consume every EFFECTIVELY visible node EXCEPT the result layer: visible
    // pixel layers AND visible adjustment layers (Normal-Mode at root, or
    // clipped). Nodes that are hidden — by their own flag OR a hidden ancestor
    // group — contributed nothing to `merged` and are left untouched; consuming
    // them would destroy pixels that are not in the composite.
    std::vector<LayerTreeNode*> toRemove;
    for (auto* n : flat) {
        if (!n || n == activeNode)
            continue;
        if (!nodeEffectivelyVisible(n))
            continue;
        const bool visibleLayer = n->type == LayerTreeNode::Type::Layer && n->layer;
        const bool visibleAdj = n->type == LayerTreeNode::Type::Adjustment
            && n->isAdjustmentLayer();
        if (visibleLayer || visibleAdj)
            toRemove.push_back(n);
    }
    removeNodesByIdentity(doc, toRemove);
    // The result layer's own clipped adjustments are baked into `merged`.
    removeClippedAdjustmentChildren(activeNode);

    // `merged` is the final document composite — every ancestor group's
    // opacity/blend/effects/transform is already baked into the pixels. The
    // result must therefore live at ROOT level: leaving it nested inside a
    // surviving group (one that still holds hidden children) would re-apply the
    // group's properties on top of the baked result. Hoist it above its
    // outermost ancestor, then prune any ancestor group the merge left empty.
    if (activeNode->parent) {
        LayerTreeNode* outermost = activeNode;
        while (outermost->parent)
            outermost = outermost->parent;
        LayerTreeNode* oldParent = activeNode->parent;
        const int takeIdx = flatIndexOfNode(doc, activeNode);
        auto taken = takeIdx >= 0 ? doc->takeNodeAt(takeIdx) : nullptr;
        if (!taken) {
            qWarning() << "[MergeVisible] aborted: could not detach result layer";
            return false;
        }
        const int outerIdx = flatIndexOfNode(doc, outermost);
        doc->insertNodeAt(outerIdx, std::move(taken));
        for (LayerTreeNode* g = oldParent;
             g && g->type == LayerTreeNode::Type::Group && g->children.empty();) {
            LayerTreeNode* next = g->parent;
            const int gi = flatIndexOfNode(doc, g);
            if (gi >= 0)
                doc->takeNodeAt(gi);
            g = next;
        }
    }

    const int newActiveFlatIdx = flatIndexOfNode(doc, activeNode);
    if (newActiveFlatIdx < 0) {
        qWarning() << "[MergeVisible] aborted: result layer vanished after removals";
        return false;
    }

    applyMergedImageToNode(doc, activeNode, std::move(merged));
    doc->activeFlatIndex = newActiveFlatIdx;
    doc->selectedFlatIndices.clear();
    doc->selectedFlatIndices.insert(newActiveFlatIdx);
    ++doc->compositionGeneration;
    return true;
}

static bool applyFlattenImageState(Document* doc)
{
    if (!doc || doc->flatCount() == 0)
        return false;

    QImage flattened = compositeDocumentImage(doc, RenderTargetType::Flatten);
    if (flattened.isNull()) {
        flattened = QImage(doc->size, QImage::Format_RGBA8888);
        flattened.fill(Qt::transparent);
    }

    doc->roots.clear();
    auto newNode = std::make_unique<LayerTreeNode>();
    newNode->type = LayerTreeNode::Type::Layer;
    newNode->name = QObject::tr("Background");
    newNode->layer = std::make_shared<Layer>();
    newNode->layer->name = newNode->name;
    newNode->layer->cpuImage = std::move(flattened);
    newNode->layer->owner = newNode.get();
    newNode->layer->textureOutdated = true;
    newNode->layer->pendingGpuUpload = true;
    newNode->layer->resetTransform = newNode->transform();
    newNode->layer->hasResetTransform = true;
    if (newNode->layer->cpuImage.width() * newNode->layer->cpuImage.height()
        >= doc->perfConfig.autoTileMinArea)
        newNode->layer->enableTiling(doc->tileSize());
    doc->roots.push_back(std::move(newNode));
    doc->activeFlatIndex = 0;
    doc->selectedFlatIndices.clear();
    doc->selectedFlatIndices.insert(0);
    ++doc->compositionGeneration;
    return true;
}

void ImageController::mergeLayers(int srcFlat, int dstFlat)
{
    if (!m_doc) return;
    auto* srcLayer = layerAtOrWarn(m_doc, srcFlat);
    auto* dstLayer = layerAtOrWarn(m_doc, dstFlat);
    if (!srcLayer || !dstLayer || srcFlat == dstFlat) return;

    // A merge destroys the source and rewrites the destination's pixels, so a
    // lock on either side (Lock Image Pixels / Lock All) blocks it.
    auto* srcNodeLk = m_doc->nodeAt(srcFlat);
    auto* dstNodeLk = m_doc->nodeAt(dstFlat);
    if ((srcNodeLk && srcNodeLk->type == LayerTreeNode::Type::Adjustment)
        || (dstNodeLk && dstNodeLk->type == LayerTreeNode::Type::Adjustment)) {
        emit operationBlocked(tr("Adjustment layers cannot be merged."));
        return;
    }
    if ((srcNodeLk && srcNodeLk->isPixelEditingLocked())
        || (dstNodeLk && dstNodeLk->isPixelEditingLocked())) {
        emit operationBlocked(tr("One of the layers being merged is locked."));
        return;
    }
    // Mirrors applyMergeLayersState's hard requirements, surfaced as friendly
    // messages instead of a generic async failure.
    if (!srcNodeLk || !dstNodeLk || srcNodeLk->parent != dstNodeLk->parent) {
        emit operationBlocked(tr("Layers can only be merged with a layer "
                                 "in the same group."));
        return;
    }
    if (!nodeEffectivelyVisible(srcNodeLk) || !nodeEffectivelyVisible(dstNodeLk)) {
        emit operationBlocked(tr("Hidden layers cannot be merged."));
        return;
    }

    // src is the layer being merged down; dstFlat is the layer below it that
    // keeps the result. Both endpoints have already been validated as unlocked,
    // non-adjustment layers above. The merge runs on the async document-state
    // path so the visual result is the real rendered composite (masks, opacity,
    // blend, clipped adjustments) and consumed adjustment layers are removed.
    mergeLayersAsync(srcFlat, dstFlat);
}

bool ImageController::mergeLayersAsync(int srcFlat, int dstFlat)
{
    return runDocumentStateOperationAsync(
        tr("Merging Layers"),
        tr("Merge Down"),
        [srcFlat, dstFlat](Document& doc) {
            return applyMergeLayersState(&doc, srcFlat, dstFlat);
        });
}

bool ImageController::isLayerEmpty(int flatIndex) const
{
    if (!m_doc) return true;
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer) return true;
    const QImage& img = layer->cpuImage;
    if (img.isNull() || img.size().isEmpty()) return true;

    int w = img.width();
    int h = img.height();
    for (int y = 0; y < h; ++y) {
        const uint32_t* line = reinterpret_cast<const uint32_t*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if ((line[x] & 0xFF000000) != 0)
                return false;
        }
    }
    return true;
}

void ImageController::rasterizeNode(int flatIndex)
{
    if (!m_doc) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
        return;
    auto* layer = node->layer.get();
    if (!layer->textData && !layer->shapeData) return;
    if (node->isPixelEditingLocked()) {
        emit operationBlocked(tr("This layer is locked and cannot be rasterized."));
        return;
    }

    QImage beforeImg = layer->cpuImage.copy();
    QTransform beforeXf = node->transform();
    auto beforeText = layer->textData;
    auto beforeShape = layer->shapeData;

    if (layer->shapeData) {
        QTransform accum = node->accumulatedTransform();
        QRectF baseBounds = ShapeVectorRenderer::buildPath(*layer->shapeData).boundingRect();

        QSize renderSize = m_doc->size;
        float xfRatio = 1.0f;
        if (baseBounds.width() > 0.001) {
            QRectF xfBounds = accum.mapRect(baseBounds);
            xfRatio = static_cast<float>(xfBounds.width() / baseBounds.width());
        }
        float scale = std::max(1.0f, xfRatio);
        int renderW = static_cast<int>(renderSize.width() * scale);
        int renderH = static_cast<int>(renderSize.height() * scale);
        renderW = std::clamp(renderW, 1, 4096);
        renderH = std::clamp(renderH, 1, 4096);

        layer->cpuImage = ShapeRenderer::render(*layer->shapeData,
                                                   QSize(renderW, renderH))
                              .convertToFormat(QImage::Format_RGBA8888);

    } else if (layer->textData) {
        TextRenderer renderer;
        renderer.render(*layer->textData, layer->cpuImage);
    }

    layer->textData.reset();
    layer->shapeData.reset();
    layer->shapeCache = Layer::ShapeRenderCache{};

    markLayerDirty(layer);

    m_history.push(std::make_unique<RasterizeCommand>(
        m_doc, flatIndex,
        std::move(beforeImg), beforeXf,
        std::move(beforeText), std::move(beforeShape),
        layer->cpuImage.copy(), node->transform(),
        tr("Rasterize Layer")));

    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::mergeVisibleLayers()
{
    if (!m_doc || m_doc->flatCount() < 2) return;
    int activeIdx = m_doc->activeFlatIndex;
    if (activeIdx < 0) return;

    // Merge Visible consumes every visible layer; a fully-locked visible layer
    // blocks the whole operation.
    if (anyFullyLockedLayer(/*visibleOnly=*/true)) {
        emit operationBlocked(tr("A visible layer is fully locked."));
        return;
    }

    // Runs on the async document-state path: the result is the real visible
    // composite (every visible adjustment applied to the layers it affects,
    // masks/opacity/blend honoured) and every consumed node — visible pixel
    // layers AND visible adjustment layers — is removed, leaving only the result
    // layer. Invisible layers stay untouched.
    mergeVisibleLayersAsync();
}

bool ImageController::mergeVisibleLayersAsync()
{
    return runDocumentStateOperationAsync(
        tr("Merging Visible Layers"),
        tr("Merge Visible"),
        [](Document& doc) { return applyMergeVisibleState(&doc); });
}

void ImageController::flattenImage()
{
    if (!m_doc || m_doc->flatCount() == 0) return;
    if (anyFullyLockedLayer()) {
        emit operationBlocked(tr("A layer is fully locked; unable to flatten."));
        return;
    }

    // Flush rasterStorage into cpuImage for every layer before cloning the undo snapshot,
    // so that undo correctly restores brush strokes that haven't been committed to cpuImage yet.
    {
        auto flatAll = m_doc->flatten();
        for (auto* n : flatAll) {
            if (n && n->layer && n->layer->rasterStorage.isEnabled()) {
                n->layer->cpuImage = flushLayerToCpuImage(n->layer.get());
                n->layer->rasterStorage.clear();
                n->layer->textureOutdated = true;
            }
        }
    }

    std::vector<std::unique_ptr<LayerTreeNode>> beforeRoots;
    beforeRoots.reserve(m_doc->roots.size());
    for (const auto& root : m_doc->roots) {
        if (root) {
            beforeRoots.push_back(root->clone());
        }
    }
    const int beforeActive = m_doc->activeFlatIndex;

    RenderContext flatCtx;
    flatCtx.document   = m_doc;
    flatCtx.targetType = RenderTargetType::Flatten;
    flatCtx.outputSize = m_doc->size;
    QImage flattened = DocumentCompositor::composite(m_doc, flatCtx);
    if (flattened.isNull()) {
        flattened = QImage(m_doc->size, QImage::Format_RGBA8888);
        flattened.fill(Qt::transparent);
    }

    std::vector<std::unique_ptr<LayerTreeNode>> afterRoots;
    afterRoots.reserve(1);
    auto newNode = std::make_unique<LayerTreeNode>();
    newNode->type = LayerTreeNode::Type::Layer;
    newNode->name = tr("Background");
    newNode->layer = std::make_shared<Layer>();
    newNode->layer->name = newNode->name;
    newNode->layer->cpuImage = std::move(flattened);
    newNode->layer->owner = newNode.get();
    newNode->layer->textureOutdated = true;
    afterRoots.push_back(std::move(newNode));

    auto* flattenedLayer = afterRoots.front()->layer.get();
    if (flattenedLayer && !flattenedLayer->cpuImage.isNull()
        && flattenedLayer->cpuImage.width() * flattenedLayer->cpuImage.height() >= 256 * 256)
        flattenedLayer->enableTiling(256);
    if (flattenedLayer) markLayerDirty(flattenedLayer);

    auto cmd = std::make_unique<FlattenImageCommand>(
        m_doc, std::move(beforeRoots), beforeActive,
        std::move(afterRoots), 0,
        tr("Flatten Image"));
    cmd->execute();
    m_history.push(std::move(cmd));

    emit layerChanged(0);
    emit activeLayerChanged(0);
    emit imageChanged();
}

bool ImageController::flattenImageAsync()
{
    return runDocumentStateOperationAsync(
        tr("Flattening Image"),
        tr("Flatten Image"),
        [](Document& doc) { return applyFlattenImageState(&doc); });
}

void ImageController::applyLayerMask(int flatIndex)
{
    auto* layer = layerAtOrWarn(m_doc, flatIndex);
    if (!layer) return;
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (layer->maskImage.isNull()) return;
    // Applying bakes the mask into the layer pixels — a raster-only pixel edit:
    // Text/Shape layers re-render cpuImage from their vector data, which would
    // silently discard the baked alpha, so checkDestructiveOp blocks them (it
    // also folds in the Lock Image Pixels / Lock All gate).
    if (!checkDestructiveOp(layer)) return;
    if (node && node->isPixelEditingLocked()) {
        emit operationBlocked(tr("The pixels of this layer are locked."));
        return;
    }

    const bool flushedRaster = layer->renderRasterStorage().isEnabled();
    if (flushedRaster) {
        layer->writableRenderCpuImage() = flushLayerToCpuImage(layer);
        layer->renderRasterStorage().clear();
        layer->textureOutdated = true;
    }

    QImage& pixels = layer->writableRenderCpuImage();
    QImage beforeImg = pixels.copy();
    QTransform beforeXf = node ? node->transform() : QTransform();

    int w = pixels.width();
    int h = pixels.height();
    for (int y = 0; y < h; ++y) {
        uint32_t* imgRow = reinterpret_cast<uint32_t*>(pixels.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int maskX = x - layer->maskOrigin.x();
            const int maskY = y - layer->maskOrigin.y();
            uint32_t maskValue = 255;
            if (maskX >= 0 && maskY >= 0
                && maskX < layer->maskImage.width()
                && maskY < layer->maskImage.height()) {
                maskValue = layer->maskImage.constScanLine(maskY)[maskX];
            }
            uint32_t alpha = (imgRow[x] >> 24) & 0xFF;
            uint32_t newAlpha = (alpha * maskValue) / 255;
            imgRow[x] = (imgRow[x] & 0x00FFFFFF) | (newAlpha << 24);
        }
    }

    layer->maskImage = QImage();
    if (layer->maskTextureId) {
        flushGpuChanges();
        layer->maskTextureId = 0;
    }
    if (layer->maskFbo) {
        flushGpuChanges();
        layer->maskFbo = 0;
    }

    // Keep the dab-layer representation (see delete_selected): re-tile the
    // masked result so the content-bounds transform outline keeps working.
    if (flushedRaster)
        layer->replaceRasterStorageWithImage(pixels);

    markLayerDirty(layer);
    if (node)
        node->invalidateEffects();

    m_history.push(std::make_unique<FilterCommand>(
        m_doc, flatIndex, std::move(beforeImg), beforeXf,
        pixels.copy(), node ? node->transform() : QTransform(),
        tr("Apply Layer Mask")));

    // The mask is gone (baked into pixels) — return the edit target to pixels.
    if (m_editingMask && m_doc->activeFlatIndex == flatIndex)
        setEditingMask(false);

    emit layerChanged(flatIndex);
    emit imageChanged();
}

void ImageController::setLayerMaskEnabled(int flatIndex, bool enabled)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer) return;
    node->layer->maskVisible = enabled;
    node->invalidateEffects();
    emit layerChanged(flatIndex);
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

bool ImageController::isLayerMaskEnabled(int flatIndex) const
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    return node && node->layer && node->layer->maskVisible && !node->layer->maskImage.isNull();
}

void ImageController::setEditingMask(bool editing)
{
    if (m_editingMask == editing) return;
    m_editingMask = editing;
    emit maskEditingChanged(editing);
}

void ImageController::setMaskOverlayVisible(bool visible)
{
    if (!m_doc || m_doc->maskOverlayVisible == visible) return;
    m_doc->maskOverlayVisible = visible;
    emit maskOverlayChanged();
}

void ImageController::toggleMaskOverlay()
{
    if (!m_doc) return;
    setMaskOverlayVisible(!m_doc->maskOverlayVisible);
}

bool ImageController::isMaskOverlayVisible() const
{
    return m_doc && m_doc->maskOverlayVisible;
}

void ImageController::setMaskOverlayOpacity(float opacity)
{
    if (!m_doc) return;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (qFuzzyCompare(m_doc->maskOverlayOpacity, opacity)) return;
    m_doc->maskOverlayOpacity = opacity;
    emit maskOverlayChanged();
}

float ImageController::maskOverlayOpacity() const
{
    return m_doc ? m_doc->maskOverlayOpacity : 0.5f;
}

void ImageController::moveNodeIntoGroup(int nodeFlatIndex, int groupFlatIndex)
{
    if (!m_doc || nodeFlatIndex < 0 || groupFlatIndex < 0) return;
    if (nodeFlatIndex >= m_doc->flatCount() || groupFlatIndex >= m_doc->flatCount())
        return;
    if (nodeFlatIndex == groupFlatIndex) return;

    auto cloneRoots = [](const std::vector<std::unique_ptr<LayerTreeNode>>& roots) {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& r : roots) {
            if (r)
                clones.push_back(r->shallowClone()); // COW: structural op, no pixel copy
        }
        return clones;
    };
    auto beforeRoots = cloneRoots(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    auto* groupNode = m_doc->nodeAt(groupFlatIndex);
    if (!groupNode || groupNode->type != LayerTreeNode::Type::Group) return;

    auto* node = m_doc->nodeAt(nodeFlatIndex);
    if (!node) return;

    if (node->parent == groupNode) return;

    const LayerTreeNode* adjOldParentLayer =
        (node->type == LayerTreeNode::Type::Adjustment && node->parent
         && node->parent->type == LayerTreeNode::Type::Layer)
            ? node->parent : nullptr;

    QTransform oldParentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        oldParentAccum = oldParentAccum * p->transform();
    const QTransform nodeWorldBefore = node->accumulatedTransform();
    const bool hasResetBefore = node->layer && node->layer->hasResetTransform;
    const QTransform resetWorldBefore = hasResetBefore
        ? (node->layer->resetTransform * oldParentAccum)
        : QTransform();

    auto owned = m_doc->takeNodeAt(nodeFlatIndex);
    if (!owned) return;

    owned->parent = groupNode;
    LayerTreeNode* movedPtr = owned.get();
    if (owned->type == LayerTreeNode::Type::Adjustment) {
        // Adjustments are not spatial — inside a group they keep operating in
        // document space (Normal Mode scoped to the group's content).
        owned->setBaseTransform(QTransform());
    } else {
        QTransform groupAccum = groupNode->accumulatedTransform();
        owned->setBaseTransform(nodeWorldBefore * groupAccum.inverted());
        if (owned->layer) {
            if (hasResetBefore) {
                owned->layer->resetTransform = resetWorldBefore * groupAccum.inverted();
                owned->layer->hasResetTransform = true;
            } else {
                owned->layer->resetTransform = owned->transform();
                owned->layer->hasResetTransform = true;
            }
        }
    }
    groupNode->children.push_back(std::move(owned));
    if (movedPtr->type == LayerTreeNode::Type::Adjustment)
        rebindAdjustmentSpace(m_doc, movedPtr, adjOldParentLayer);

    const int newGroupIndex = [&]() -> int {
        auto flat = m_doc->flatten();
        for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
            if (flat[i] == groupNode)
                return i;
        }
        return -1;
    }();

    m_doc->activeFlatIndex = newGroupIndex;
    m_doc->selectedFlatIndices.clear();
    if (newGroupIndex >= 0)
        m_doc->selectedFlatIndices.insert(newGroupIndex);

    auto afterRoots = cloneRoots(m_doc->roots);
    const int afterActive = m_doc->activeFlatIndex;
    const std::set<int> afterSelected = m_doc->selectedFlatIndices;

    m_history.push(std::make_unique<LayerTreeStateCommand>(
        m_doc,
        std::move(beforeRoots), beforeActive, beforeSelected,
        std::move(afterRoots), afterActive, afterSelected,
        tr("Move to Group")));

    emit layerChanged(newGroupIndex);
    emit activeLayerChanged(newGroupIndex);
    emit selectionChanged();
    emit imageChanged();
    if (m_doc) ++m_doc->compositionGeneration;
}

void ImageController::flushGpuChanges()
{
    if (!m_doc) return;
    auto* extra = QOpenGLContext::currentContext()
                      ? QOpenGLContext::currentContext()->extraFunctions()
                      : nullptr;
    if (!extra) return;

    auto flat = m_doc->flatten();
    for (auto* node : flat) {
        if (!node->layer) continue;
        if (node->layer->textureId) {
            extra->glDeleteTextures(1, &node->layer->textureId);
            node->layer->textureId = 0;
        }
        if (node->layer->fbo) {
            extra->glDeleteFramebuffers(1, &node->layer->fbo);
            node->layer->fbo = 0;
        }
        if (node->layer->maskTextureId) {
            extra->glDeleteTextures(1, &node->layer->maskTextureId);
            node->layer->maskTextureId = 0;
        }
    }
}

cv::Mat ImageController::makeLayerMask(Layer* layer) const
{
    if (!layer || !m_doc->selection.active() || m_doc->selection.isEmpty()) {
        return {};
    }
    int lw = layer->cpuImage.width();
    int lh = layer->cpuImage.height();
    if (lw <= 0 || lh <= 0) {
        qWarning() << "[DeleteSel] makeLayerMask: cpuImage has zero size"
                   << QSize(lw, lh) << "-> EMPTY mask (rasterStorage="
                   << layer->rasterStorage.isEnabled() << ")";
        return {};
    }
    const QTransform t = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
    cv::Mat mask = m_doc->selectionMaskForLayer(lw, lh, t);
    return mask;
}

cv::Mat ImageController::selectionInMaskSpace(Layer* layer) const
{
    if (!layer || layer->maskImage.isNull()) return {};
    if (!m_doc || !m_doc->selection.active() || m_doc->selection.isEmpty())
        return {};

    const int dw = m_doc->size.width();
    const int dh = m_doc->size.height();
    const QSize baseSize = layer->rasterBaseSize();
    const int lw = baseSize.width();
    const int lh = baseSize.height();
    const int mw = layer->maskImage.width();
    const int mh = layer->maskImage.height();
    if (lw <= 0 || lh <= 0 || dw <= 0 || dh <= 0 || mw <= 0 || mh <= 0)
        return {};
    const QPoint origin = layer->maskOrigin;

    // Same affine as createMaskFromSelection, but the destination offset is the
    // existing mask's origin (not layerMaskBounds) so it lands on the live mask.
    const QTransform tf = layer->owner ? layer->owner->accumulatedTransform()
                                       : QTransform();
    double a00 = dw * tf.m11() / lw;
    double a01 = -dw * tf.m21() / lh;
    double a02 = dw * 0.5 * (1.0 - tf.m11() + tf.m21() + tf.m31());
    double a10 = -dh * tf.m12() / lw;
    double a11 = dh * tf.m22() / lh;
    double a12 = dh * 0.5 * (1.0 + tf.m12() - tf.m22() - tf.m32());
    a02 += a00 * origin.x() + a01 * origin.y();
    a12 += a10 * origin.x() + a11 * origin.y();

    cv::Mat invXf = (cv::Mat_<double>(2, 3) << a00, a01, a02, a10, a11, a12);
    QImage selCopy = m_doc->selection.image().copy();
    cv::Mat docMask(dh, dw, CV_8UC1, selCopy.bits(),
                    static_cast<size_t>(selCopy.bytesPerLine()));
    cv::Mat out(mh, mw, CV_8UC1, cv::Scalar(0));
    // Outside the selection projects to 0 (not selected). selCopy outlives the
    // call, so the docMask view stays valid through warpAffine.
    cv::warpAffine(docMask, out, invXf, cv::Size(mw, mh),
                   cv::INTER_NEAREST | cv::WARP_INVERSE_MAP,
                   cv::BORDER_CONSTANT, cv::Scalar(0));
    return out;
}

void ImageController::blendByMask(cv::Mat& dst, const cv::Mat& src, const cv::Mat& mask)
{
    if (dst.empty() || src.empty() || mask.empty()) return;
    CV_Assert(dst.type() == CV_8UC4 && src.type() == CV_8UC4 && mask.type() == CV_8UC1);

    const int h = std::min({dst.rows, src.rows, mask.rows});
    const int w = std::min({dst.cols, src.cols, mask.cols});
    for (int y = 0; y < h; ++y) {
        uchar* d = dst.ptr<uchar>(y);
        const uchar* s = src.ptr<uchar>(y);
        const uchar* m = mask.ptr<uchar>(y);
        for (int x = 0; x < w; ++x) {
            const int mv = m[x];
            if (mv == 0) continue;
            if (mv == 255) {
                std::memcpy(d + x * 4, s + x * 4, 4);
                continue;
            }
            for (int c = 0; c < 4; ++c) {
                const int dv = d[x * 4 + c];
                d[x * 4 + c] = static_cast<uchar>(
                    dv + ((s[x * 4 + c] - dv) * mv + 127) / 255);
            }
        }
    }
}

bool ImageController::extractSelectedDocRegion(Layer* layer, cv::Mat& outCropped,
                                               QPointF& outDocPos) const
{
    if (!m_doc || !layer || layer->cpuImage.isNull()) return false;
    if (!m_doc->selection.active() || m_doc->selection.isEmpty()) return false;

    // Dab (rasterStorage) layers keep their painted pixels in tiles — cpuImage
    // is the stale base. Composite tiles over the base so the extracted region
    // contains what the user actually sees. Same base size, so the affine
    // mapping below is unchanged.
    const QImage srcImage = layer->rasterStorage.isEnabled()
        ? layer->compositeImage()
        : layer->cpuImage;
    if (srcImage.isNull()) return false;

    const int dw = m_doc->size.width();
    const int dh = m_doc->size.height();
    const int lw = srcImage.width();
    const int lh = srcImage.height();
    if (dw <= 0 || dh <= 0 || lw <= 0 || lh <= 0) return false;

    auto* node = layer->owner;
    const QTransform accumT = node ? node->accumulatedTransform() : QTransform();

    const double a00 = dw * accumT.m11() / lw;
    const double a01 = -dw * accumT.m21() / lh;
    const double a02 = dw * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31());
    const double a10 = -dh * accumT.m12() / lw;
    const double a11 = dh * accumT.m22() / lh;
    const double a12 = dh * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32());
    cv::Mat xform = (cv::Mat_<double>(2, 3) << a00, a01, a02, a10, a11, a12);

    cv::Mat layerMat = ImageEngine::toCvMat(srcImage);
    // Bake the layer mask in layer space, BEFORE warping, so the copy matches
    // the composited (masked) appearance rather than the raw pixels.
    applyLayerMaskToCvImage(layer, layerMat);
    cv::Mat canvas(dh, dw, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    cv::warpAffine(layerMat, canvas, xform, canvas.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));

    cv::Mat selMask(dh, dw, CV_8UC1,
                    const_cast<uchar*>(m_doc->selection.constBits()),
                    static_cast<size_t>(m_doc->selection.image().bytesPerLine()));
    cv::Rect bounds = cv::boundingRect(selMask) & cv::Rect(0, 0, dw, dh);
    if (bounds.width <= 0 || bounds.height <= 0) return false;

    outCropped = canvas(bounds).clone();
    // Scale alpha by the mask so feathered selections keep soft edges.
    for (int y = 0; y < outCropped.rows; ++y) {
        uchar* row = outCropped.ptr<uchar>(y);
        const uchar* m = selMask.ptr<uchar>(bounds.y + y) + bounds.x;
        for (int x = 0; x < outCropped.cols; ++x)
            row[x * 4 + 3] = static_cast<uchar>(row[x * 4 + 3] * m[x] / 255);
    }
    outDocPos = QPointF(bounds.x, bounds.y);
    return true;
}

void ImageController::applyLayerMaskToCvImage(Layer* layer,
                                              cv::Mat& rgba) const
{
    if (!layer || rgba.empty() || rgba.type() != CV_8UC4) return;
    if (!layer->maskVisible || layer->maskImage.isNull() ||
        layer->maskDensity <= 0.0f)
        return;

    const QImage& mask = layer->maskImage;        // Grayscale8, layer-pixel space
    const QPoint origin = layer->maskOrigin;       // layer pixel of mask (0,0)
    const float density = std::clamp(layer->maskDensity, 0.0f, 1.0f);
    const int mw = mask.width();
    const int mh = mask.height();

    // Coverage factor f = 1 + (m/255 - 1)*density, exactly the compositor's mask
    // semantics: m=255 (or outside the mask) fully reveals, density scales the
    // mask's hiding effect (density 0 = mask off). See modulateAlphaByAdjustmentMask.
    for (int y = 0; y < rgba.rows; ++y) {
        uchar* row = rgba.ptr<uchar>(y);
        const int my = y - origin.y();
        const uchar* mrow = (my >= 0 && my < mh) ? mask.constScanLine(my) : nullptr;
        for (int x = 0; x < rgba.cols; ++x) {
            int m = 255;
            if (mrow) {
                const int mx = x - origin.x();
                if (mx >= 0 && mx < mw) m = mrow[mx];
            }
            const float f = 1.0f + (m / 255.0f - 1.0f) * density;
            if (f >= 0.999f) continue;
            row[x * 4 + 3] = static_cast<uchar>(row[x * 4 + 3] * f);
        }
    }
}

void ImageController::applyFilterWithSelection(Layer* layer,
    const std::string& toolName,
    const std::function<cv::Mat(const cv::Mat&)>& filter)
{
    if (!layer) return;

    // For rasterStorage layers, cpuImage is stale — composite tiles into full-size image first.
    const bool flushedRaster = layer->renderRasterStorage().isEnabled();
    if (flushedRaster) {
        layer->writableRenderCpuImage() = flushLayerToCpuImage(layer);
        layer->renderRasterStorage().clear();
        layer->textureOutdated = true;
    }

    auto* node = m_doc->nodeAt(m_doc->activeFlatIndex);
    QImage& pixels = layer->writableRenderCpuImage();
    QImage beforeImg = pixels.copy();
    QTransform beforeT = node ? node->transform() : QTransform();

    cv::Mat cvImg = ImageEngine::toCvMatFast(pixels);
    cv::Mat result = filter(cvImg);

    if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
        cv::Mat layerMask = makeLayerMask(layer);
        if (!layerMask.empty())
            blendByMask(cvImg, result, layerMask);  // feather-aware
        else
            cvImg = result;
    } else {
        cvImg = result;
    }

    if (!cvImg.empty()) {
        pixels = ImageEngine::toQImageFast(cvImg);
        // Keep the dab-layer representation (see delete_selected): re-tile the
        // filtered result so the transform outline keeps tracking the
        // valid-pixel bounds instead of snapping to the layer base.
        if (flushedRaster)
            layer->replaceRasterStorageWithImage(pixels);
        markLayerDirty(layer);
        layer->textureOutdated = true;
        syncLayerToGpu(layer);
        emit imageChanged();

        QImage afterImg = pixels.copy();
        QTransform afterT = node ? node->transform() : QTransform();
        m_history.push(std::make_unique<FilterCommand>(
            m_doc, m_doc->activeFlatIndex,
            std::move(beforeImg), beforeT,
            std::move(afterImg), afterT,
            QString::fromStdString(toolName)));
    }
}

void ImageController::markLayerDirty(Layer* layer, const QRect& rect)
{
    if (!layer) return;
    // Pixels changed: drop the cached alpha content bounds (covers in-place
    // cpuImage writes, where the QImage cacheKey doesn't change).
    layer->invalidateContentBounds();
    if (m_doc && layer->hasEvaluatedRasterContent()
        && !layer->evaluatedCelId().isNull()) {
        if (auto* content = m_doc->animation.celStorage().content(
                layer->evaluatedCelId()))
            content->bounds = layer->contentImageBounds().toAlignedRect();
    }
    // The composite changed whether or not the layer is tiled — bump the
    // generation so generation-keyed caches (the display projection,
    // RenderCache) rebuild. Tiled layers additionally flag their dirty tiles.
    if (m_doc) ++m_doc->compositionGeneration;
    if (!layer->tiledSystem) return;
    if (rect.isEmpty())
        layer->tileManager.markAllDirty();
    else
        layer->tileManager.markDirty(rect);
}

bool ImageController::isHeavyTool(const std::string& toolName)
{
    return toolName == "fill_bucket"
        || toolName == "delete_selected"
        || toolName == "adjust_color"
        || toolName == "adjust_brightness"
        || toolName == "adjust_contrast"
        || toolName == "adjust_saturation"
        || toolName == "adjust_hue"
        || toolName == "auto_contrast"
        || toolName == "gaussian_blur"
        || toolName == "sharpen"
        || toolName == "median_blur"
        || toolName == "box_blur"
        || toolName == "bilateral_blur"
        || toolName == "motion_blur"
        || toolName == "radial_blur"
        || toolName == "zoom_blur"
        || toolName == "edge_detect"
        || toolName == "grayscale"
        || toolName == "invert_colors"
        || toolName == "noise_reduce"
        || toolName == "posterize"
        || toolName == "threshold"
        || toolName == "remove_background";
}

bool ImageController::canApplyTiled(Layer* layer, const std::string& toolName) const
{
    return layer
        && layer->tiledSystem
        && processing::FilterProcessor::isTileable(toolName)
        && !isHeavyTool(toolName)
        && !(m_doc && m_doc->selection.active() && !m_doc->selection.isEmpty());
}

void ImageController::applyFilterTiled(Layer* layer, const std::string& toolName,
                                        const JsonMap& params)
{
    if (!layer || !layer->tiledSystem) return;

    auto* node = m_doc ? m_doc->nodeAt(m_doc->activeFlatIndex) : nullptr;
    QImage beforeImg = layer->cpuImage.copy();
    QTransform beforeT = node ? node->transform() : QTransform();

    QVariantMap qParams;
    for (const auto& [k, v] : params) {
        if (std::holds_alternative<double>(v))
            qParams[QString::fromStdString(k)] = std::get<double>(v);
        else if (std::holds_alternative<std::string>(v))
            qParams[QString::fromStdString(k)] = QString::fromStdString(std::get<std::string>(v));
    }

    QRect dirty = layer->dirtyRegion.boundingRect();
    if (dirty.isEmpty())
        dirty = QRect(0, 0, layer->cpuImage.width(), layer->cpuImage.height());

    auto tiles = layer->tileManager.visibleTiles(dirty);
    int processed = processing::FilterProcessor::processRect(
        layer->cpuImage,
        layer->imageWidth(), layer->imageHeight(),
        dirty,
        layer->tileManager.tileSize(),
        tiles,
        toolName, qParams);

    if (processed > 0) {
        markLayerDirty(layer, dirty);
        layer->textureOutdated = true;
        syncLayerToGpu(layer);
        emit imageChanged();

        QImage afterImg = layer->cpuImage.copy();
        QTransform afterT = node ? node->transform() : QTransform();
        m_history.push(std::make_unique<FilterCommand>(
            m_doc, m_doc->activeFlatIndex,
            std::move(beforeImg), beforeT,
            std::move(afterImg), afterT,
            QString::fromStdString(toolName)));
    }
}

void ImageController::onAsyncJobCompleted(uint64_t jobId)
{
    auto it = m_pendingAsyncJobs.find(jobId);
    if (it == m_pendingAsyncJobs.end()) return;

    auto job = it->second;
    m_pendingAsyncJobs.erase(it);

    if (job->cancelled) {
        emit progressOperationCanceled(jobId);
        return;
    }
    if (job->resultImage.isNull()) {
        emit progressOperationFailed(jobId, tr("The operation did not produce a valid result."));
        return;
    }

    auto* node = m_doc ? m_doc->nodeAt(job->targetFlatIndex) : nullptr;
    if (!node || !node->layer || node->layer.get() != job->weakLayer) {
        emit progressOperationFailed(jobId, tr("The target layer changed before the operation finished."));
        return;
    }

    auto* layer = node->layer.get();
    layer->cpuImage = std::move(job->resultImage);
    // Restore the dab-layer representation after a flush-based async op (see
    // AsyncJob::retileRasterStorage) so the transform outline keeps tracking
    // the valid-pixel bounds instead of snapping to the layer base.
    if (job->retileRasterStorage)
        layer->replaceRasterStorageWithImage(layer->cpuImage);
    markLayerDirty(layer);
    layer->textureOutdated = true;
    syncLayerToGpu(layer);
    emit imageChanged();

    QTransform afterT = node->transform();
    m_history.push(std::make_unique<FilterCommand>(
        m_doc, job->targetFlatIndex,
        std::move(job->beforeImage), job->beforeTransform,
        layer->cpuImage.copy(), afterT,
        QString::fromStdString(job->toolName)));

    emit toolExecuted(QString::fromStdString(job->toolName), true);
    emit progressOperationProgressChanged(jobId, 100);
    emit progressOperationFinished(jobId);
}

void ImageController::onProgressiveBatch(uint64_t jobId, QVector<QRect> tileRects)
{
    auto it = m_pendingAsyncJobs.find(jobId);
    if (it == m_pendingAsyncJobs.end()) return;

    auto& job = it->second;
    Layer* layer = job->weakLayer;
    if (!layer) return;

    // Copy processed tile pixels from worker's copy to layer's cpuImage
    // During BlockingQueuedConnection, the worker is blocked, so job->sourceImage is safe.
    for (const auto& r : tileRects) {
        for (int y = 0; y < r.height(); ++y) {
            std::memcpy(layer->cpuImage.scanLine(r.y() + y) + r.x() * 4,
                       job->sourceImage.constScanLine(r.y() + y) + r.x() * 4,
                       static_cast<size_t>(r.width()) * 4);
        }
        markLayerDirty(layer, r);
    }

    layer->textureOutdated = true;
    syncLayerToGpu(layer);
    emit imageChanged();
}

// ----- Clipboard / Copy-Paste -----
//
// Copy/paste of whole layers and groups goes through LayerTreeNode::clone(),
// the canonical deep copy used everywhere else (reorder, duplicate, undo
// snapshots). It preserves shapeData/adjustment/distortData/rasterStorage and
// re-parents children — a previous bespoke deep-copy here silently dropped
// those, so a copied shape/adjustment layer pasted as a blank node.

bool ImageController::hasClipboard() const
{
    // "Something pastable exists": an image on the system clipboard (external
    // copy) or our internal rich clipboard. paste() resolves which one wins.
    auto* sys = QGuiApplication::clipboard();
    const QMimeData* mime = sys ? sys->mimeData() : nullptr;
    if (mime && mime->hasImage())
        return true;
    return ClipboardManager::instance().hasData();
}

// Mirrors an in-app copy onto the SYSTEM clipboard: the flattened raster (when
// one can be produced) so other applications can paste it, plus the internal
// marker (kInternalClipboardMime = token) so our own paste() can tell that the
// system clipboard still corresponds to the internal rich data. When another
// app later takes the clipboard, the marker disappears and paste() ignores the
// stale internal cache — copying inside the app never permanently shadows the
// system clipboard (GIMP/Photoshop behaviour).
static void publishClipboardToSystem(const QImage& raster, const QString& token)
{
    auto* sys = QGuiApplication::clipboard();
    if (!sys)
        return;
    auto* mime = new QMimeData();
    mime->setData(kInternalClipboardMime, token.toUtf8());
    if (!raster.isNull())
        mime->setImageData(raster);
    sys->setMimeData(mime); // clipboard takes ownership of mime
}

bool ImageController::importExternalImages(const QStringList& paths, const QPointF& dropCanvasNdc)
{
    if (!m_doc || paths.isEmpty()) return false;

    QSet<QString> existingNames;
    for (auto* n : m_doc->flatten())
        existingNames.insert(n->name.toLower());
    auto uniqueName = [&existingNames](const QString& base) {
        QString clean = base.trimmed();
        if (clean.isEmpty()) clean = QStringLiteral("Layer");
        QString candidate = clean;
        int i = 2;
        while (existingNames.contains(candidate.toLower())) {
            candidate = QStringLiteral("%1 %2").arg(clean).arg(i++);
        }
        existingNames.insert(candidate.toLower());
        return candidate;
    };

    const int docW = std::max(1, m_doc->size.width());
    const int docH = std::max(1, m_doc->size.height());
    const float stepX = 24.0f * 2.0f / static_cast<float>(docW);
    const float stepY = 24.0f * 2.0f / static_cast<float>(docH);

    // External imports should be placed at the top of the layer stack.
    int insertAt = 0;

    auto comp = std::make_unique<CompositeCommand>(
        paths.size() > 1 ? tr("Import Images") : tr("Import Image"));

    int importedCount = 0;
    for (int i = 0; i < paths.size(); ++i) {
        const QString path = paths[i];

        ImageLoadResult loaded = imageCodecRegistry().readImage(path);
        if (!loaded.ok)
            continue;
        if (loaded.image.width > 20000 || loaded.image.height > 20000)
            continue;

        OpenColorPolicyContext colorContext =
            ColorManagementService::instance().defaultOpenContext(path);
        colorContext.workingSpace = m_doc->colorProfile().isValid()
            ? m_doc->colorProfile()
            : ColorProfile::sRgb();
        colorContext.mismatchPolicy = ProfileMismatchPolicy::ConvertToWorkingSpace;
        const ColorManagedOpenResult colorManaged =
            ColorManagementService::instance().prepareImageForOpen(loaded.image, colorContext);

        QImage img = convertDocumentImageToQImage(colorManaged.image).convertToFormat(QImage::Format_RGBA8888);
        if (img.isNull()) continue;

        auto node = std::make_unique<LayerTreeNode>();
        node->type = LayerTreeNode::Type::Layer;
        node->name = uniqueName(QFileInfo(path).completeBaseName());
        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->cpuImage = img;
        node->layer->owner = node.get();
        node->setBaseVisible(true);
        node->setBaseOpacity(1.0f);
        node->setBaseBlendMode(BlendMode::Normal);
        node->lockFlags = LockNone;

        const float halfW = static_cast<float>(img.width()) / static_cast<float>(docW);
        const float halfH = static_cast<float>(img.height()) / static_cast<float>(docH);
        const float cx = static_cast<float>(dropCanvasNdc.x()) + stepX * static_cast<float>(i);
        const float cy = static_cast<float>(dropCanvasNdc.y()) - stepY * static_cast<float>(i);
        node->setBaseTransform(QTransform(
            halfW, 0.0, 0.0,
            0.0, halfH, 0.0,
            cx, cy, 1.0));
        node->layer->resetTransform = node->baseTransform();
        node->layer->hasResetTransform = true;

        auto snapshot = node->clone(true);
        comp->add(std::make_unique<AddLayerCommand>(m_doc, insertAt, std::move(snapshot), tr("Import Image")));
        ++insertAt;
        ++importedCount;
    }

    if (importedCount == 0)
        return false;

    comp->execute();
    m_history.push(std::move(comp));
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit layerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
    return true;
}

bool ImageController::importImage(const QImage& img, const QString& name)
{
    if (!m_doc || img.isNull()) return false;

    QImage converted = img.format() == QImage::Format_RGBA8888
        ? img : img.convertToFormat(QImage::Format_RGBA8888);
    if (converted.isNull()) return false;

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = name;
    node->layer = std::make_shared<Layer>();
    node->layer->name = name;
    node->layer->cpuImage = converted;
    node->layer->owner = node.get();
    node->setBaseVisible(true);
    node->setBaseOpacity(1.0f);
    node->setBaseBlendMode(BlendMode::Normal);
    node->lockFlags = LockNone;

    const int docW = std::max(1, m_doc->size.width());
    const int docH = std::max(1, m_doc->size.height());
    const float halfW = static_cast<float>(converted.width()) / static_cast<float>(docW);
    const float halfH = static_cast<float>(converted.height()) / static_cast<float>(docH);
    node->setBaseTransform(QTransform(
        halfW, 0.0, 0.0,
        0.0, halfH, 0.0,
        0.0, 0.0, 1.0));
    node->layer->resetTransform = node->baseTransform();
    node->layer->hasResetTransform = true;

    int insertAt = m_doc && m_doc->activeFlatIndex >= 0 ? 0 : 0;
    int newIndex = m_doc->insertNodeAt(insertAt, std::move(node));
    m_doc->activeFlatIndex = newIndex;
    m_doc->selectedFlatIndices.clear();
    m_doc->selectedFlatIndices.insert(newIndex);

    m_history.push(std::make_unique<AddLayerCommand>(
        m_doc, insertAt,
        m_doc->nodeAt(newIndex) ? m_doc->nodeAt(newIndex)->clone() : nullptr,
        tr("Import Image")));

    ++m_doc->compositionGeneration;
    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit imageChanged();
    return true;
}

// ── Generative Fill (src/ai/) ────────────────────────────────────────────────

namespace {

// Stable Diffusion expects dimensions that are multiples of 8.
int alignDown8(int v) { return (v / 8) * 8; }
int alignUp8(int v)   { return ((v + 7) / 8) * 8; }

// Build an RGBA image from `result` whose alpha is gated by `maskGray`
// (white = keep result, black = transparent), so only the selected area is
// written back and the seam follows the (possibly feathered) mask.
QImage gateByMask(const QImage& result, const QImage& maskGray)
{
    QImage rgba = result.convertToFormat(QImage::Format_RGBA8888);
    QImage g = maskGray.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* m = (y < g.height()) ? g.constScanLine(y) : nullptr;
        QRgb* o = reinterpret_cast<QRgb*>(rgba.scanLine(y));
        for (int x = 0; x < rgba.width(); ++x) {
            int mv = (m && x < g.width()) ? m[x] : 0;
            int a = (qAlpha(o[x]) * mv) / 255;
            o[x] = qRgba(qRed(o[x]), qGreen(o[x]), qBlue(o[x]), a);
        }
    }
    return rgba;
}

} // namespace

void ImageController::setGenerativePreset(const AgentConfig& preset)
{
    m_generativePreset = preset;
    m_hasGenerativePreset = (preset.kind == Generative)
                            && !preset.provider.baseUrl.isEmpty();
}

void ImageController::generativeFill(const QString& prompt, GenFillMode mode,
                                     const QString& negativePrompt,
                                     double strength, int steps, int seed)
{
    if (!m_doc) return;

    if (!m_doc->selection.active() || m_doc->selection.isEmpty()) {
        emit operationBlocked(tr("Generative fill requires a selection."));
        return;
    }
    if (!m_hasGenerativePreset) {
        emit operationBlocked(tr("Configure a Generative preset (AI Agent settings)."));
        return;
    }
    if (m_genProvider) {
        emit operationBlocked(tr("A generative fill is already running."));
        return;
    }

    Layer* layer = activeLayer();
    if (mode == GenFillMode::FillSelection && layer && !checkDestructiveOp(layer))
        return;

    const int docW = m_doc->size.width();
    const int docH = m_doc->size.height();

    // bbox of the selection, expanded by a 25% context margin, clamped to the
    // canvas, then aligned to multiples of 8.
    QRectF b = m_doc->selection.bounds();
    QRect sel = b.toAlignedRect();
    int mx = qMax(8, sel.width() / 4);
    int my = qMax(8, sel.height() / 4);
    int x0 = qMax(0, alignDown8(sel.left() - mx));
    int y0 = qMax(0, alignDown8(sel.top()  - my));
    int x1 = qMin(docW, alignUp8(sel.right()  + mx));
    int y1 = qMin(docH, alignUp8(sel.bottom() + my));
    QRect rect(x0, y0, qMax(8, x1 - x0), qMax(8, y1 - y0));
    rect = rect.intersected(QRect(0, 0, docW, docH));
    if (rect.width() < 8 || rect.height() < 8) {
        emit operationBlocked(tr("Selection is too small for generative fill."));
        return;
    }

    // Context image = current composite cropped to the region.
    QImage composite = ::compositeImage(m_doc);
    QImage imageCrop = composite.copy(rect).convertToFormat(QImage::Format_RGBA8888);

    // Mask = selection (white = repaint) cropped to the region.
    QImage selImg = m_doc->selection.image().convertToFormat(QImage::Format_Grayscale8);
    m_genMaskCrop = selImg.copy(rect);

    m_genRect = rect;
    m_genMode = mode;
    m_genTargetIndex = m_doc->activeFlatIndex;
    m_genBefore = (mode == GenFillMode::FillSelection && layer && !layer->cpuImage.isNull())
                  ? layer->cpuImage.copy() : QImage();

    InpaintRequest req;
    req.image = imageCrop;
    req.mask = m_genMaskCrop;
    req.prompt = prompt;
    req.negativePrompt = negativePrompt.isEmpty()
        ? m_generativePreset.generative.negativePrompt : negativePrompt;
    req.strength = strength >= 0.0 ? strength : m_generativePreset.generative.strength;
    req.steps = steps > 0 ? steps : m_generativePreset.generative.steps;
    req.seed = (seed != -2) ? seed : m_generativePreset.generative.seed;
    req.outSize = rect.size();

    m_genProvider = ImageGenProviderFactory::create(m_generativePreset, this);
    if (!m_genProvider) {
        emit operationBlocked(tr("This provider cannot generate images."));
        return;
    }

    qInfo().noquote() << "[GenFill] start | provider=" << m_genProvider->name()
                      << "| mode=" << (mode == GenFillMode::FillSelection ? "selection" : "new_layer")
                      << "| rect=" << rect
                      << "| outSize=" << req.outSize
                      << "| targetLayer=" << m_genTargetIndex
                      << "| baseUrl=" << m_generativePreset.provider.baseUrl;

    connect(m_genProvider, &ImageGenProvider::finished,
            this, &ImageController::onGenerativeFinished);
    connect(m_genProvider, &ImageGenProvider::failed,
            this, &ImageController::onGenerativeFailed);
    connect(m_genProvider, &ImageGenProvider::progress, this,
            [this](int pct, const QString& stage) {
                if (m_genJobId) {
                    emit progressOperationProgressChanged(m_genJobId, pct);
                    emit progressOperationMessageChanged(m_genJobId, stage);
                }
            });

    m_genJobId = m_nextDocumentOperationId++;
    emit progressOperationStarted(m_genJobId, tr("Generative fill…"), true);

    m_genProvider->requestInpaint(req);
}

void ImageController::onGenerativeFinished(const InpaintResult& result)
{
    const uint64_t jobId = m_genJobId;

    QImage out = result.image;
    qInfo().noquote() << "[GenFill] finished | resultSize=" << result.image.size()
                      << "| seed=" << result.seed
                      << "| targetRect=" << m_genRect
                      << "| mode=" << (m_genMode == GenFillMode::FillSelection ? "selection" : "new_layer");
    if (!out.isNull() && out.size() != m_genRect.size())
        out = out.scaled(m_genRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (out.isNull())
        qWarning() << "[GenFill] finished but result image is NULL";

    if (m_doc && !out.isNull()) {
        // Gate the result by the selection mask so only the selected area changes.
        QImage gated = gateByMask(out, m_genMaskCrop);

        // Diagnostics: how much of the result will actually be written.
        {
            QImage mg = m_genMaskCrop.convertToFormat(QImage::Format_Grayscale8);
            long whitePx = 0, maskTotal = long(mg.width()) * mg.height();
            for (int y = 0; y < mg.height(); ++y) {
                const uchar* r = mg.constScanLine(y);
                for (int x = 0; x < mg.width(); ++x) if (r[x] > 10) ++whitePx;
            }
            QImage ga = gated.convertToFormat(QImage::Format_RGBA8888);
            long opaquePx = 0;
            for (int y = 0; y < ga.height(); ++y) {
                const QRgb* r = reinterpret_cast<const QRgb*>(ga.constScanLine(y));
                for (int x = 0; x < ga.width(); ++x) if (qAlpha(r[x]) > 10) ++opaquePx;
            }
            qInfo().noquote() << "[GenFill] gate | maskSize=" << mg.size()
                              << "| maskWhite=" << whitePx << "/" << maskTotal
                              << "| gatedSize=" << ga.size()
                              << "| gatedOpaque=" << opaquePx;
        }

        if (m_genMode == GenFillMode::FillSelection) {
            auto* n = m_doc->nodeAt(m_genTargetIndex);
            Layer* layer = n ? n->layer.get() : nullptr;
            bool flushedRaster = false;
            if (layer) {
                // Tiled layers keep pixels in rasterStorage/tiles, not cpuImage —
                // flush them in so we operate on (and display) the real pixels.
                flushedRaster = layer->rasterStorage.isEnabled();
                if (flushedRaster) {
                    qInfo() << "[GenFill] flushing rasterStorage -> cpuImage (tiled layer)";
                    layer->cpuImage = flushLayerToCpuImage(layer);
                    layer->rasterStorage.clear();
                    layer->textureOutdated = true;
                }
            }
            if (layer && !layer->cpuImage.isNull()) {
                const int lw = layer->cpuImage.width();
                const int lh = layer->cpuImage.height();
                const int cw = m_doc->size.width();
                const int ch = m_doc->size.height();

                // Snapshot AFTER the flush so undo restores the real pixels.
                QImage before = layer->cpuImage.copy();
                const QTransform beforeT = n->transform();

                // Map the document-space fill rect into the layer's pixel space
                // via the same chain io/ImageIO uses (imgToNdc · accum · ndcToPixel).
                const QTransform world = n->accumulatedTransform();
                const QTransform imgToNdc(2.0 / lw, 0.0, 0.0, -2.0 / lh, -1.0, 1.0);
                const QTransform ndcToPixel(cw / 2.0, 0.0, 0.0, -ch / 2.0, cw / 2.0, ch / 2.0);
                const QTransform layerToCanvas = imgToNdc * world * ndcToPixel;

                const bool translateOnly =
                    qAbs(layerToCanvas.m11() - 1.0) < 1e-3 && qAbs(layerToCanvas.m22() - 1.0) < 1e-3 &&
                    qAbs(layerToCanvas.m12()) < 1e-3 && qAbs(layerToCanvas.m21()) < 1e-3;

                if (translateOnly) {
                    // Canvas-sized / imported / moved layers: pure offset. Grow the
                    // layer when the selection falls outside its current pixels.
                    const QPoint offset(qRound(layerToCanvas.dx()), qRound(layerToCanvas.dy()));
                    const QRect fillR = m_genRect.translated(-offset);

                    const int minX = std::min(0, fillR.left());
                    const int minY = std::min(0, fillR.top());
                    const int maxX = std::max(lw, fillR.left() + fillR.width());
                    const int maxY = std::max(lh, fillR.top() + fillR.height());
                    const int newW = maxX - minX, newH = maxY - minY;
                    const int offX = -minX, offY = -minY;

                    qInfo().noquote() << "[GenFill] apply selection | translateOnly"
                                      << "| layerPx=" << QSize(lw, lh)
                                      << "| offset=" << offset
                                      << "| fillRect(layer)=" << fillR
                                      << "| willExpand=" << (newW > lw || newH > lh)
                                      << "-> newSize=" << QSize(newW, newH);
                    if (newW > lw || newH > lh) {
                        QImage grown(newW, newH, QImage::Format_RGBA8888);
                        grown.fill(Qt::transparent);
                        QPainter p(&grown);
                        p.drawImage(offX, offY, layer->cpuImage);
                        p.drawImage(fillR.left() + offX, fillR.top() + offY, gated);
                        p.end();
                        layer->cpuImage = grown;

                        // Anchor the grown image (same NDC adjustment as expandLayer).
                        const float sx = float(newW) / float(lw);
                        const float sy = float(newH) / float(lh);
                        const float dx = 1.0f - 2.0f * offX / newW - float(lw) / newW;
                        const float dy = float(lh) / newH - 1.0f + 2.0f * offY / newH;
                        QTransform adj; adj.scale(sx, sy); adj.translate(dx, dy);
                        n->setBaseTransform(adj * n->transform());

                        if (layer->tiledSystem) {
                            layer->enableTiling(layer->tileManager.tileSize());
                            layer->tileManager.markAllDirty();
                            layer->pendingGpuUpload = true;
                        }
                    } else {
                        QImage after = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
                        QImage beforeRgba = after.copy();
                        QPainter p(&after);
                        p.drawImage(fillR.topLeft(), gated);
                        p.end();

                        // Diagnostic: did the pixels actually change?
                        long changed = 0;
                        QRect probe = fillR.intersected(QRect(0, 0, after.width(), after.height()));
                        for (int y = probe.top(); y <= probe.bottom(); ++y) {
                            const QRgb* a = reinterpret_cast<const QRgb*>(after.constScanLine(y));
                            const QRgb* b = reinterpret_cast<const QRgb*>(beforeRgba.constScanLine(y));
                            for (int x = probe.left(); x <= probe.right(); ++x)
                                if (a[x] != b[x]) ++changed;
                        }
                        qInfo().noquote() << "[GenFill] direct draw | fillRect=" << fillR
                                          << "| changedPx=" << changed
                                          << "| tiledSystem=" << layer->tiledSystem;

                        layer->cpuImage = after;

                        // Refresh tiles from the new cpuImage so the GPU/live path
                        // re-uploads them (projection reads cpuImage directly).
                        if (layer->tiledSystem) {
                            layer->enableTiling(layer->tileManager.tileSize());
                            layer->tileManager.markAllDirty();
                            layer->pendingGpuUpload = true;
                        }
                    }
                } else {
                    qInfo().noquote() << "[GenFill] apply selection | scaled/rotated layer"
                                      << "| layerPx=" << QSize(lw, lh)
                                      << "-> resample via inverse transform (no expand)";
                    // Scaled / rotated layer: resample the result into the layer's
                    // pixel space through the inverse transform (no expansion).
                    QImage after = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
                    QPainter p(&after);
                    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    p.setTransform(layerToCanvas.inverted());
                    p.drawImage(m_genRect.topLeft(), gated);
                    p.end();
                    layer->cpuImage = after;
                }

                // Keep the dab-layer representation (see delete_selected). The
                // grown-layer branch already adjusted n->transform() for the new
                // cpuImage size, so re-tiling here (baseSize = cpuImage size)
                // stays consistent with the transform.
                if (flushedRaster)
                    layer->replaceRasterStorageWithImage(layer->cpuImage);

                layer->textureOutdated = true;
                markLayerDirty(layer);
                syncLayerToGpu(layer);

                m_history.push(std::make_unique<FilterCommand>(
                    m_doc, m_genTargetIndex, std::move(before), beforeT,
                    layer->cpuImage.copy(), n->transform(),
                    tr("Generative Fill")));

                ++m_doc->compositionGeneration;
                emit layerChanged(m_genTargetIndex);
                emit imageChanged();
                emit documentChanged();
            }
        } else { // FillAsNewLayer
            QImage canvas(m_doc->size, QImage::Format_RGBA8888);
            canvas.fill(Qt::transparent);
            {
                QPainter p(&canvas);
                p.drawImage(m_genRect.topLeft(), gated);
            }
            importImage(canvas, tr("Generative Fill"));
            emit documentChanged();
        }
    }

    if (m_genProvider) { m_genProvider->deleteLater(); m_genProvider = nullptr; }
    m_genJobId = 0;
    m_genBefore = QImage();
    m_genMaskCrop = QImage();
    if (jobId) emit progressOperationFinished(jobId);
}

void ImageController::onGenerativeFailed(const InpaintError& error)
{
    qWarning().noquote() << "[GenFill] FAILED | code=" << static_cast<int>(error.code)
                         << "| message=" << error.message;
    const uint64_t jobId = m_genJobId;
    if (m_genProvider) { m_genProvider->deleteLater(); m_genProvider = nullptr; }
    m_genJobId = 0;
    m_genBefore = QImage();
    m_genMaskCrop = QImage();

    if (error.code == InpaintError::Canceled) {
        if (jobId) emit progressOperationCanceled(jobId);
    } else {
        if (jobId) emit progressOperationFailed(jobId, error.message);
        emit operationBlocked(error.message);
    }
}

bool ImageController::applyAiRemoveResult(const AiRemoveApplyRequest& request)
{
    if (!m_doc || request.generatedPatch.isNull() || request.blendMask.isNull()
        || request.documentRoi.isEmpty())
        return false;

    QImage patch = request.generatedPatch.size() == request.documentRoi.size()
        ? request.generatedPatch.convertToFormat(QImage::Format_RGBA8888)
        : request.generatedPatch.scaled(request.documentRoi.size(), Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);
    QImage mask = request.blendMask.size() == request.documentRoi.size()
        ? request.blendMask.convertToFormat(QImage::Format_Grayscale8)
        : request.blendMask.scaled(request.documentRoi.size(), Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_Grayscale8);
    QImage gated = gateByMask(patch, mask);

    auto countMask = [](const QImage& image) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y) {
            const uchar* row = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x)
                if (row[x] > 0)
                    ++count;
        }
        return count;
    };
    auto countAlpha = [](const QImage& image) {
        int count = 0;
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < rgba.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
            for (int x = 0; x < rgba.width(); ++x)
                if (qAlpha(row[x]) > 0)
                    ++count;
        }
        return count;
    };

    qInfo().noquote() << "[AI][REMOVE] Apply result"
                      << "mode=" << static_cast<int>(request.outputMode)
                      << "roi=" << request.documentRoi
                      << "patch=" << patch.size()
                      << "maskNonZero=" << countMask(mask)
                      << "patchAlphaNonZero=" << countAlpha(patch)
                      << "gatedAlphaNonZero=" << countAlpha(gated);

    if (request.outputMode == AiRemoveOutputMode::NewLayer) {
        QImage canvas(m_doc->size, QImage::Format_RGBA8888);
        canvas.fill(Qt::transparent);
        {
            QPainter p(&canvas);
            p.drawImage(request.documentRoi.topLeft(), gated);
        }
        importImage(canvas, tr("AI Remove Patch"));
        emit documentChanged();
        return true;
    }

    const int target = m_doc->activeFlatIndex;
    auto* node = m_doc->nodeAt(target);
    Layer* layer = node ? node->layer.get() : nullptr;
    if (!layer || !checkDestructiveOp(layer))
        return false;
    const bool flushedRaster = layer->rasterStorage.isEnabled();
    if (flushedRaster) {
        layer->cpuImage = flushLayerToCpuImage(layer);
        layer->rasterStorage.clear();
        layer->textureOutdated = true;
    }
    if (layer->cpuImage.isNull())
        return false;

    const int lw = layer->cpuImage.width();
    const int lh = layer->cpuImage.height();
    const int cw = m_doc->size.width();
    const int ch = m_doc->size.height();
    const QTransform world = node ? node->accumulatedTransform() : QTransform();
    const QTransform imgToNdc(2.0 / lw, 0.0, 0.0, -2.0 / lh, -1.0, 1.0);
    const QTransform ndcToPixel(cw / 2.0, 0.0, 0.0, -ch / 2.0, cw / 2.0, ch / 2.0);
    const QTransform layerToCanvas = imgToNdc * world * ndcToPixel;
    const bool translateOnly =
        qAbs(layerToCanvas.m11() - 1.0) < 1e-3 && qAbs(layerToCanvas.m22() - 1.0) < 1e-3 &&
        qAbs(layerToCanvas.m12()) < 1e-3 && qAbs(layerToCanvas.m21()) < 1e-3;
    if (!translateOnly) {
        emit operationBlocked(tr("Active layer has a transform. Use Output: New Layer."));
        return false;
    }

    QImage before = layer->cpuImage.copy();
    const QTransform beforeT = node ? node->transform() : QTransform();
    const QPoint offset(qRound(layerToCanvas.dx()), qRound(layerToCanvas.dy()));
    const QRect fillR = request.documentRoi.translated(-offset);
    QImage after = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
    {
        QPainter p(&after);
        p.drawImage(fillR.topLeft(), gated);
    }
    layer->cpuImage = after;
    // Keep the dab-layer representation (see delete_selected).
    if (flushedRaster)
        layer->replaceRasterStorageWithImage(layer->cpuImage);
    layer->textureOutdated = true;
    layer->pendingGpuUpload = true;
    markLayerDirty(layer, fillR.intersected(QRect(QPoint(0, 0), after.size())));
    syncLayerToGpu(layer);

    m_history.push(std::make_unique<FilterCommand>(
        m_doc, target, std::move(before), beforeT,
        layer->cpuImage.copy(), node ? node->transform() : QTransform(),
        tr("AI Remove")));
    emit layerChanged(target);
    emit imageChanged();
    emit documentChanged();
    return true;
}

void ImageController::copy()
{
    if (!m_doc) return;
    auto& clip = ClipboardManager::instance();

    if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
        int dw = m_doc->size.width();
        int dh = m_doc->size.height();

        auto* layer = m_doc->activeLayer();
        if (layer && !layer->renderCpuImage().isNull()) {
            auto* node = m_doc->nodeAt(m_doc->activeFlatIndex);
            QTransform accumT = node ? node->accumulatedTransform() : QTransform();
            const bool rotatedOrSheared = accumT.m12() != 0.0 || accumT.m21() != 0.0;

            // Layer-pixel → doc-pixel scale factors (see extractSelectedDocRegion
            // for the full affine derivation). The clipboard copy must capture the
            // *visual* size of the selection so paste reproduces what the user
            // sees. The native-resolution fast path only does that when each layer
            // pixel maps to one doc pixel; if the layer is scaled, cropping raw
            // pixels loses the scale and paste would shrink/enlarge the result.
            const double lwForScale = static_cast<double>(layer->imageWidth());
            const double lhForScale = static_cast<double>(layer->imageHeight());
            const double pxScaleX = lwForScale > 0 ? std::abs(dw * accumT.m11() / lwForScale) : 1.0;
            const double pxScaleY = lhForScale > 0 ? std::abs(dh * accumT.m22() / lhForScale) : 1.0;
            const bool unitScale = std::abs(pxScaleX - 1.0) < 1e-3 &&
                                   std::abs(pxScaleY - 1.0) < 1e-3;

            cv::Mat cropped;
            QPointF docPos;

            if (rotatedOrSheared || !unitScale) {
                // Any rotation/shear/scale must be baked by extracting in doc
                // space so the copied bitmap matches the on-canvas visual size.
                if (!extractSelectedDocRegion(layer, cropped, docPos)) return;
            } else {
                // Unit-scale axis-aligned: extract in layer space (preserves the
                // layer's native resolution) and map the corner to doc coords.
                cv::Mat layerMask = makeLayerMask(layer);
                if (layerMask.empty()) return;

                // Dab (rasterStorage) layers keep painted pixels in tiles;
                // cpuImage is the stale base — composite so the copy includes
                // the dabs (same base size, so the mapping below is unchanged).
                const QImage srcImage = layer->renderRasterStorage().isEnabled()
                    ? layer->compositeImage()
                    : layer->renderCpuImage();
                cv::Mat layerImg = ImageEngine::toCvMat(srcImage);
                // Bake the layer mask so hidden regions are not copied.
                applyLayerMaskToCvImage(layer, layerImg);
                cv::Mat masked = cv::Mat::zeros(layerImg.size(), layerImg.type());
                layerImg.copyTo(masked, layerMask);

                cv::Rect layerBounds = cv::boundingRect(layerMask);
                if (layerBounds.width <= 0 || layerBounds.height <= 0) return;

                cropped = masked(layerBounds).clone();

                // Convert layer-pixel top-left to document-pixel position
                double lw = static_cast<double>(layer->imageWidth());
                double lh = static_cast<double>(layer->imageHeight());
                double a00 = dw * accumT.m11() / lw;
                double a01 = -dw * accumT.m21() / lh;
                double a02 = dw * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31());
                double a10 = -dh * accumT.m12() / lw;
                double a11 = dh * accumT.m22() / lh;
                double a12 = dh * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32());
                double docX = a00 * layerBounds.x + a01 * layerBounds.y + a02;
                double docY = a10 * layerBounds.x + a11 * layerBounds.y + a12;
                docPos = QPointF(docX, docY);
            }

            ClipboardData data;
            data.type = ClipboardType::Pixels;
            data.pixels = ImageEngine::toQImage(cropped);
            data.docPosition = docPos;
            data.sourceDocSize = m_doc->size;
            data.name = QString("Pasted Layer");
            const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            publishClipboardToSystem(data.pixels, token);
            clip.setData(std::move(data), token);
        } else {
            // No active layer — render each visible layer at its transformed position, then composite
            auto flat = m_doc->flatten();
            std::vector<cv::Mat> mats;
            std::vector<float> ops;
            std::vector<bool> vis;
            for (int i = static_cast<int>(flat.size()) - 1; i >= 0; --i) {
                auto* n = flat[i];
                if (!n->layer || !n->isVisible()) continue;
                auto* l = n->layer.get();
                // Composite dab tiles over the stale cpuImage base (see the
                // active-layer branch above).
                const QImage srcLayerImage = l->renderRasterStorage().isEnabled()
                    ? l->compositeImage()
                    : l->renderCpuImage();
                int lw = srcLayerImage.width();
                int lh = srcLayerImage.height();
                if (lw <= 0 || lh <= 0) continue;

                QTransform accumT = n->accumulatedTransform();
                double a00 = dw * accumT.m11() / lw;
                double a01 = -dw * accumT.m21() / lh;
                double a02 = dw * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31());
                double a10 = -dh * accumT.m12() / lw;
                double a11 = dh * accumT.m22() / lh;
                double a12 = dh * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32());
                cv::Mat xform = (cv::Mat_<double>(2,3) << a00, a01, a02, a10, a11, a12);

                cv::Mat layerMat = ImageEngine::toCvMat(srcLayerImage);
                // Honour each layer's mask so the composited copy matches display.
                applyLayerMaskToCvImage(l, layerMat);
                cv::Mat canvas(dh, dw, CV_8UC4, cv::Scalar(0, 0, 0, 0));
                cv::warpAffine(layerMat, canvas, xform, canvas.size(),
                               cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
                mats.push_back(std::move(canvas));
                ops.push_back(n->opacity());
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
            cv::Mat cropped = masked(cropRect).clone();

            ClipboardData data;
            data.type = ClipboardType::Pixels;
            data.pixels = ImageEngine::toQImage(cropped);
            data.docPosition = QPointF(static_cast<float>(cropRect.x),
                                        static_cast<float>(cropRect.y));
            data.sourceDocSize = m_doc->size;
            data.name = QString("Pasted Layer");
            const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            publishClipboardToSystem(data.pixels, token);
            clip.setData(std::move(data), token);
        }
    } else {
        auto* node = m_doc->activeNode();
        if (!node) return;

        auto dup = node->clone(true);
        ClipboardData data;
        data.type = (node->type == LayerTreeNode::Type::Group)
                    ? ClipboardType::Group : ClipboardType::Layer;
        // Carry the copied subtree's animation tracks (keyed by their current
        // ids; remapped to fresh ids on paste).
        captureSubtreeTracks(m_doc, node, data.tracks,
                             &data.rasterTracks, &data.celStorage);
        data.node = std::move(dup);
        data.sourceDocSize = m_doc->size;
        data.name = node->name + " (copy)";
        // Raster mirror for external apps: a layer flattens to its composite
        // (dab tiles included); a group has no cheap flat raster, so only the
        // internal marker is published (external paste unavailable — same as
        // the previous behaviour, but the marker keeps staleness detection).
        QImage raster;
        if (node->type == LayerTreeNode::Type::Layer && node->layer)
            raster = node->layer->compositeImage();
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        publishClipboardToSystem(raster, token);
        clip.setData(std::move(data), token);
    }

    m_pasteCount = 0;
    emit clipboardChanged();
}

void ImageController::paste()
{
    if (!m_doc) return;
    auto& clip = ClipboardManager::instance();

    // ── Resolve the paste source: system clipboard first ────────────────────
    // The internal rich clipboard (layer/transform/mask metadata) wins only
    // while the system clipboard still carries the marker written by our last
    // copy(). If another application has replaced the system clipboard since
    // then, the internal cache is stale: an external image pastes as a new
    // pixel layer; any other external content simply means there is nothing of
    // ours to paste. Falling back to the stale internal data is allowed only
    // when the system clipboard is truly empty/unavailable (safe — nothing was
    // copied over it, e.g. platforms that drop the clipboard when the source
    // application exits).
    const QMimeData* mime = QGuiApplication::clipboard()
        ? QGuiApplication::clipboard()->mimeData()
        : nullptr;
    const bool internalCurrent = clip.hasData()
        && !clip.token().isEmpty()
        && mime && mime->hasFormat(kInternalClipboardMime)
        && QString::fromUtf8(mime->data(kInternalClipboardMime)) == clip.token();
    if (!internalCurrent) {
        if (mime && mime->hasImage()) {
            const QImage img = qvariant_cast<QImage>(mime->imageData());
            if (!img.isNull()) {
                importImage(img, tr("Pasted Layer"));
                return;
            }
        }
        const bool systemEmpty = !mime || mime->formats().isEmpty();
        if (!systemEmpty)
            return; // system clipboard points at other content — stale cache stays ignored
    }
    if (!clip.hasData()) return;

    ++m_doc->compositionGeneration;
    const auto& data = clip.data();
    m_pasteCount++;

    QSize docSize = m_doc->size;
    QSize srcSize = data.sourceDocSize;
    if (!srcSize.isValid() || srcSize.isEmpty())
        srcSize = docSize;

    float dw = static_cast<float>(docSize.width());
    float dh = static_cast<float>(docSize.height());
    float sdw = static_cast<float>(srcSize.width());
    float sdh = static_cast<float>(srcSize.height());

    float offsetX = 10.0f * m_pasteCount;
    float offsetY = 10.0f * m_pasteCount;
    float ndcOffX = 2.0f * offsetX / dw;
    float ndcOffY = -2.0f * offsetY / dh;
    QTransform ndcOffset;
    ndcOffset.translate(ndcOffX, ndcOffY);

    // Cross-document scale correction S = diag(sdw/dw, sdh/dh).
    //
    // A leaf's image is mapped image-pixels → layer NDC → canvas NDC → canvas
    // pixels. The image→NDC and NDC→pixel steps are stretched by the document
    // aspect ratio, and each leaf's own transform encodes the cancellation of the
    // SOURCE doc's aspect. Pasting into a different-sized doc leaves that stale
    // cancellation in place, so a correction is needed to restore the visual
    // pixel size/shape. S is intentionally anisotropic — that is what preserves a
    // plain layer's pixels across docs of different aspect ratios.
    //
    // S is applied per leaf in genuine CANVAS space: conjugated by the transform
    // accumulated BETWEEN the leaf and the canvas (its parent's accumulated
    // transform once inserted). At top level that "between" is identity, so S
    // applies directly (plain layer keeps its pixels). Inside a transformed group
    // the conjugation makes S act outside the group, so the pasted content takes
    // the exact shape a native child of the destination group would have — no
    // shear, no compounding on re-copy. Same-size docs → S is identity (no-op).
    QTransform crossDocScale;
    if (dw > 0.0f && dh > 0.0f && sdw > 0.0f && sdh > 0.0f)
        crossDocScale.scale(double(sdw) / dw, double(sdh) / dh);
    const bool needsCrossDocFix = !crossDocScale.isIdentity();

    // Applies the canvas-space cross-doc scale to every leaf of the just-inserted
    // subtree (`root` itself when it is a leaf). Must run AFTER insertion so each
    // leaf's parent accumulated transform reflects the destination group.
    const auto applyCrossDocFix = [&](LayerTreeNode* root, auto&& self) -> void {
        const bool isLeaf = root->type != LayerTreeNode::Type::Group;
        if (isLeaf) {
            QTransform between = root->parent
                ? root->parent->accumulatedTransform() : QTransform();
            bool inv = false;
            const QTransform bInv = between.inverted(&inv);
            const QTransform corr = inv
                ? (between * crossDocScale * bInv) : crossDocScale;
            root->setBaseTransform(root->transform() * corr);
            stampResetTransform(root);
        }
        for (auto& child : root->children)
            self(child.get(), self);
    };

    int insertAt = m_doc->activeFlatIndex;
    if (insertAt < 0) insertAt = 0;

    if (data.type == ClipboardType::Pixels) {
        auto newNode = std::make_unique<LayerTreeNode>();
        newNode->type = LayerTreeNode::Type::Layer;
        newNode->name = data.name.isEmpty()
            ? QString("Pasted Layer %1").arg(m_pasteCount)
            : data.name;
        newNode->layer = std::make_shared<Layer>();
        newNode->layer->name = newNode->name;
        newNode->layer->cpuImage = std::move(data.pixels);
        newNode->layer->owner = newNode.get();

        int imgW = newNode->layer->cpuImage.width();
        int imgH = newNode->layer->cpuImage.height();

        float rx = sdw > 0 ? data.docPosition.x() / sdw : 0.0f;
        float ry = sdh > 0 ? data.docPosition.y() / sdh : 0.0f;
        float posX = rx * dw + offsetX;
        float posY = ry * dh + offsetY;

        float centerX = posX + imgW * 0.5f;
        float centerY = posY + imgH * 0.5f;
        float tx = 2.0f * centerX / dw - 1.0f;
        float ty = 1.0f - 2.0f * centerY / dh;
        float sx = static_cast<float>(imgW) / dw;
        float sy = static_cast<float>(imgH) / dh;

        QTransform t;
        t.translate(tx, ty);
        t.scale(sx, sy);
        newNode->setBaseTransform(t);
        newNode->layer->resetTransform = t;
        newNode->layer->hasResetTransform = true;

        auto clone = newNode->clone();
        int newIndex = m_doc->insertNodeAt(insertAt, std::move(newNode));
        m_doc->selectNode(newIndex, false);
        m_history.push(std::make_unique<AddLayerCommand>(
            m_doc, insertAt, std::move(clone), tr("Paste")));
        syncLayerToGpu(m_doc->activeLayer());
        emit layerChanged(newIndex);
        emit activeLayerChanged(newIndex);
        emit selectionChanged();
        emit imageChanged();
    }
    else if (data.type == ClipboardType::Layer ||
             data.type == ClipboardType::Group) {
        if (!data.node) return;
        const bool isGroup = data.type == ClipboardType::Group;
        auto dup = data.node->clone(true);
        // The pasted node is a NEW node: give the whole subtree fresh ids (the
        // clone preserved the source ids, which would collide with the original)
        // and install the carried animation tracks under those new ids.
        QHash<LayerId, LayerId> idMap;
        dup->assignNewIds(idMap);
        for (const ClipboardTrack& ct : data.tracks) {
            const LayerId newId = idMap.value(ct.layerId);
            if (!newId.isNull())
                m_doc->animation.ensureTrack(newId, ct.property) = ct.track;
        }
        for (const anim::CelId& celId : data.celStorage.ids()) {
            if (m_doc->animation.celStorage().contains(celId)) continue;
            if (const auto* content = data.celStorage.content(celId))
                m_doc->animation.celStorage().insertCel(celId, *content);
        }
        for (const ClipboardRasterTrack& ct : data.rasterTracks) {
            const LayerId newId = idMap.value(ct.layerId);
            if (!newId.isNull())
                m_doc->animation.ensureRasterTrack(newId) = ct.track;
        }
        dup->name = data.name;
        // The paste offset rides the ROOT only, so a group moves as one block
        // (the children keep their relative positions). Cross-doc scale is NOT
        // applied here — it must run per leaf AFTER insertion (applyCrossDocFix),
        // once each leaf's parent accumulated transform reflects the destination.
        dup->setBaseTransform(dup->transform() * ndcOffset);
        stampResetTransformRecursive(dup.get());

        int newIndex = m_doc->insertNodeAt(insertAt, std::move(dup));
        if (needsCrossDocFix) {
            if (auto* inserted = m_doc->nodeAt(newIndex))
                applyCrossDocFix(inserted, applyCrossDocFix);
        }
        m_doc->selectNode(newIndex, false);

        // Clone for undo/redo AFTER the cross-doc fix so redo restores the final
        // (corrected) transforms.
        auto clone = m_doc->nodeAt(newIndex)
            ? m_doc->nodeAt(newIndex)->clone(true) : nullptr;
        m_history.push(std::make_unique<AddLayerCommand>(
            m_doc, newIndex, std::move(clone),
            isGroup ? tr("Paste Group") : tr("Paste")));
        emit layerChanged(newIndex);
        emit activeLayerChanged(newIndex);
        emit selectionChanged();
        emit imageChanged();
    }
}

// ── Text layer methods ───────────────────────────────────────

// Derives the layer-panel label for an auto-named Text Layer from its content.
// Uses the first non-empty line (trimmed) and elides it to 30 characters total,
// counting the trailing "...". Returns an empty string when the text has no
// line with content, so the caller can keep the existing default name.
static QString textLayerAutoLabel(const QString& text)
{
    constexpr int kMaxLen = 30;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.size() > kMaxLen)
            return line.left(kMaxLen - 3) + QStringLiteral("...");
        return line;
    }
    return QString();
}

void ImageController::createTextLayer(const QString& initialText,
                                       const TextBox& box,
                                       QPointF canvasNdcPos,
                                       float fontSize,
                                       const QColor& color)
{
    if (!m_doc) return;

    auto layer = std::make_shared<Layer>();
    layer->name = initialText.isEmpty() ? QStringLiteral("Text") : initialText;

    auto textData = std::make_shared<TextLayerData>();
    textData->text = initialText;
    textData->box = box;
    textData->flowMode = box.width > 0.0f ? TextFlowMode::Paragraph : TextFlowMode::Point;
    textData->align = TextAlign::Left;
    TextSpan span;
    span.start = 0;
    span.end = static_cast<int>(initialText.size());
    span.fontFamily = "Sans Serif";
    span.fontSize = fontSize;
    span.color = color.isValid() ? color : QColor(Qt::black);
    textData->spans.push_back(span);
    normalizeParagraphs(*textData);   // one ParagraphStyle per paragraph
    textData->dirty = true;
    layer->textData = textData;

    TextRenderer renderer;
    renderer.setEditing(true);
    renderer.render(*textData, layer->cpuImage);

    const float halfW = static_cast<float>(layer->cpuImage.width())
        / static_cast<float>(std::max(1, m_doc->size.width()));
    const float halfH = static_cast<float>(layer->cpuImage.height())
        / static_cast<float>(std::max(1, m_doc->size.height()));

    QTransform t;
    t.setMatrix(
        halfW, 0.0, 0.0,
        0.0, halfH, 0.0,
        static_cast<qreal>(canvasNdcPos.x() + halfW),
        static_cast<qreal>(canvasNdcPos.y() - halfH),
        1.0);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = layer->name;
    // The label starts system-generated so committing the typed text can derive
    // it automatically — until the user renames the layer by hand.
    node->nameIsAuto = true;
    node->layer = layer;
    node->layer->owner = node.get();
    node->setBaseTransform(t);
    node->layer->resetTransform = t;
    node->layer->hasResetTransform = true;

    int insertAt = 0;
    if (m_doc->activeFlatIndex >= 0) {
        auto* activeNode = m_doc->nodeAt(m_doc->activeFlatIndex);
        if (activeNode && activeNode->parent) {
            auto& siblings = activeNode->parent->children;
            auto it = std::find_if(siblings.begin(), siblings.end(),
                [&](const auto& c) { return c.get() == activeNode; });
            if (it != siblings.end())
                insertAt = std::distance(siblings.begin(), it) + 1;
        }
    }

    auto addCmd = std::make_unique<AddLayerCommand>(
        m_doc, insertAt, std::move(node), "create_text");
    addCmd->execute();
    m_history.push(std::move(addCmd));

    emit layerChanged(insertAt);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
}

void ImageController::createShapeLayer(const ShapeData& data)
{
    if (!m_doc) return;

    ShapeData shape = normalizedShapeData(data);
    if (!shapeHasGeometry(shape))
        return;

    auto layer = std::make_shared<Layer>();
    layer->name = nextShapeLayerName(m_doc, shape);
    layer->shapeData = std::make_shared<ShapeData>(shape);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = layer->name;
    node->layer = layer;
    node->layer->owner = node.get();
    if (!ShapeLayerUpdater::rebuildShapeRaster(*m_doc, *node))
        return;
    QTransform t = node->transform();
    node->layer->resetTransform = t;
    node->layer->hasResetTransform = true;

    int insertAt = 0;
    if (m_doc->activeFlatIndex >= 0) {
        auto* activeNode = m_doc->nodeAt(m_doc->activeFlatIndex);
        if (activeNode && activeNode->parent) {
            auto& siblings = activeNode->parent->children;
            auto it = std::find_if(siblings.begin(), siblings.end(),
                [&](const auto& c) { return c.get() == activeNode; });
            if (it != siblings.end())
                insertAt = std::distance(siblings.begin(), it) + 1;
        }
    }

    auto addCmd = std::make_unique<AddLayerCommand>(
        m_doc, insertAt, std::move(node), "create_shape");
    addCmd->execute();
    m_history.push(std::move(addCmd));

    emit layerChanged(m_doc->activeFlatIndex);
    emit activeLayerChanged(m_doc->activeFlatIndex);
    emit imageChanged();
}

void ImageController::modifyShapeLayer(int flatIndex, const ShapeData& newData)
{
    if (!m_doc) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->shapeData) return;
    if (!node->canEditContent()) {
        emit operationBlocked(tr("The content of this shape layer is locked."));
        return;
    }

    ShapeData shape = normalizedShapeData(newData);
    if (!shapeHasGeometry(shape))
        return;

    ShapeData before = *node->layer->shapeData;
    QImage beforeImage = node->layer->cpuImage.copy();
    QTransform beforeXf = node->transform();
    const QTransform beforeBaseXf = shapeLayerTransform(before, beforeImage, m_doc->size);

    *node->layer->shapeData = shape;
    if (!ShapeLayerUpdater::rebuildShapeRaster(*m_doc, *node)) {
        *node->layer->shapeData = before;
        node->layer->cpuImage = beforeImage;
        node->setBaseTransform(beforeXf);
        return;
    }

    QImage rendered = node->layer->cpuImage.copy();
    const QTransform afterBaseXf = node->transform();
    bool invertible = false;
    const QTransform invBeforeBase = beforeBaseXf.inverted(&invertible);
    const QTransform userDelta = invertible ? (invBeforeBase * beforeXf) : QTransform();
    QTransform afterXf = invertible ? (afterBaseXf * userDelta) : beforeXf;
    node->setBaseTransform(afterXf);
    node->layer->textureOutdated = true;
    node->layer->shapeCache.dirty = true;
    node->invalidateEffects();
    node->thumbnailDirty = true;

    m_history.push(std::make_unique<ModifyShapeCommand>(
        m_doc, flatIndex,
        std::move(before), shape,
        std::move(beforeImage), rendered.copy(),
        beforeXf, afterXf,
        "edit_shape"));

    emit layerChanged(flatIndex);
    emit imageChanged();
}

void ImageController::bakeShapeTransform(int flatIndex, const QTransform& beforeTransform)
{
    if (!m_doc) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->shapeData) return;

    ShapeData before = *node->layer->shapeData;
    QImage beforeImage = node->layer->cpuImage.copy();

    // Bake the visible layer transform into ShapeTransform.localToCanvas, keeping
    // VectorPath untouched and regenerating cpuImage as derived raster.
    if (!bakeShapeLayerResolutionInPlace(flatIndex)) {
        setLayerTransform(flatIndex, node->transform(), &beforeTransform);
        return;
    }

    const QTransform afterTransform = node->transform();
    m_history.push(std::make_unique<ModifyShapeCommand>(
        m_doc, flatIndex,
        std::move(before), *node->layer->shapeData,
        std::move(beforeImage), node->layer->cpuImage.copy(),
        beforeTransform, afterTransform,
        "transform_shape"));

    emit layerChanged(flatIndex);
    emit imageChanged();
}

bool ImageController::bakeShapeLayerResolutionInPlace(int flatIndex)
{
    if (!m_doc) return false;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->shapeData) return false;

    const ShapeData before = *node->layer->shapeData;
    const QTransform naturalBase = shapeLayerTransform(before, node->layer->cpuImage, m_doc->size);
    bool invertible = false;
    const QTransform invNaturalBase = naturalBase.inverted(&invertible);
    if (!invertible) return false;

    // Use accumulated (world) transform so shapes inside groups get the
    // correct display delta when the parent group's transform changes.
    // The local transform alone would miss the group's contribution.
    // TODO - review
    const QTransform worldXf = node->accumulatedTransform();
    const QTransform displayDelta = invNaturalBase * worldXf;
    ShapeData after = transformShapeData(before, displayDelta,
                                         canvasToPixelScaleFor(m_doc->size));
    if (!shapeHasGeometry(after)) return false;

    QImage rendered = ShapeRenderer::render(after, m_doc->size);
    if (rendered.isNull()) return false;
    rendered = rendered.convertToFormat(QImage::Format_RGBA8888);

    *node->layer->shapeData = after;
    node->layer->cpuImage = rendered;

    // Compute the natural world transform for the new raster, then factor
    // out the parent chain so the local transform keeps the shape at the
    // same world position inside its group (just like fitTextLayerTransformToImage).
    // TODO - review
    const QTransform newNaturalWorld = shapeLayerTransform(after, rendered, m_doc->size);
    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    bool parentInvOk = false;
    const QTransform parentInv = parentAccum.inverted(&parentInvOk);
    node->setBaseTransform(parentInvOk ? (newNaturalWorld * parentInv) : newNaturalWorld);

    node->layer->textureOutdated = true;
    node->layer->shapeCache.dirty = true;
    node->invalidateEffects();
    node->thumbnailDirty = true;

    syncLayerToGpu(node->layer.get());
    if (m_doc) ++m_doc->compositionGeneration;
    return true;
}

void ImageController::updateTextLayer(int flatIndex, const TextLayerData& data)
{
    if (!m_doc) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->textData) return;

    TextLayerData before = *node->layer->textData;
    *node->layer->textData = data;

    TextRenderer renderer;
    renderer.render(*node->layer->textData, node->layer->cpuImage);
    syncLayerToGpu(node->layer.get());

    QTransform t = node->transform();
    m_history.push(std::make_unique<TextEditCommand>(
        m_doc, flatIndex, std::move(before), data, t, t, "edit_text"));

    emit layerChanged(flatIndex);
    emit imageChanged();
}

void ImageController::applyTextStyle(int flatIndex, const TextSpan& style, bool toSelection)
{
    if (!m_doc) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->textData) return;

    TextLayerData before = *node->layer->textData;
    auto& data = *node->layer->textData;

    int lo = 0, hi = data.text.size();
    if (toSelection) {
        lo = style.start;
        hi = style.end;
    }

    TextSpan newSpan = style;
    newSpan.start = lo;
    newSpan.end = hi;

    for (auto it = data.spans.begin(); it != data.spans.end(); ) {
        if (it->start >= hi || it->end <= lo) { ++it; continue; }
        if (it->start < lo && it->end > hi) {
            TextSpan tail = *it;
            tail.start = hi;
            it->end = lo;
            data.spans.insert(it + 1, tail);
            ++it;
            continue;
        }
        if (it->start >= lo && it->end <= hi) { *it = newSpan; ++it; continue; }
        if (it->start < lo) it->end = lo;
        else it->start = hi;
        ++it;
    }

    std::sort(data.spans.begin(), data.spans.end(),
              [](const TextSpan& a, const TextSpan& b) { return a.start < b.start; });
    std::vector<TextSpan> merged;
    for (auto& s : data.spans) {
        if (s.start >= s.end) continue;
        if (merged.empty()) { merged.push_back(s); continue; }
        auto& last = merged.back();
        if (last.end == s.start && last.sameStyle(s))
            last.end = s.end;
        else
            merged.push_back(s);
    }
    data.spans = std::move(merged);
    data.dirty = true;

    TextRenderer renderer;
    renderer.render(data, node->layer->cpuImage);
    syncLayerToGpu(node->layer.get());

    QTransform t = node->transform();
    m_history.push(std::make_unique<TextEditCommand>(
        m_doc, flatIndex, std::move(before), data, t, t, "apply_style"));

    emit layerChanged(flatIndex);
    emit imageChanged();
}

bool ImageController::commitTextEdit(int flatIndex, const TextLayerData& before,
                                      const QTransform& beforeTransform)
{
    if (!m_doc) return false;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->layer || !node->layer->textData) return false;

    TextLayerData after = *node->layer->textData;
    if (before.text == after.text && before.spans == after.spans
        && before.paragraphs == after.paragraphs
        && before.align == after.align
        && before.box.width == after.box.width
        && before.box.height == after.box.height
        && before.flowMode == after.flowMode
        && qFuzzyCompare(before.lineSpacing, after.lineSpacing)) {
        return false;
    }

    m_history.push(std::make_unique<TextEditCommand>(
        m_doc, flatIndex, before, after,
        beforeTransform, node->transform(), "edit_text"));

    // While the layer still carries its system-generated label, keep it in sync
    // with the typed text (first non-empty line, elided). A manual rename clears
    // nameIsAuto, so a user-chosen name is never overwritten. An empty text keeps
    // the current default name untouched.
    if (node->nameIsAuto) {
        const QString label = textLayerAutoLabel(after.text);
        if (!label.isEmpty()) {
            node->name = label;
            if (node->layer) node->layer->name = label;
        }
    }

    emit layerChanged(flatIndex);
    emit imageChanged();
    return true;
}

void ImageController::undo()
{
    if (m_history.canUndo()) {
        if (auto* aj = AsyncJobSystem::instance())
            aj->cancelAll();
        m_pendingAsyncJobs.clear();
        m_history.undo();
        // SelectionCommand (and composite commands that restore a selection)
        // swap the mask without notifying anyone; the ants texture and the
        // toolbar refine cache key off this signal.
        emit selectionChanged();
        emit imageChanged();
        emit documentChanged();
    }
}

void ImageController::redo()
{
    if (m_history.canRedo()) {
        if (auto* aj = AsyncJobSystem::instance())
            aj->cancelAll();
        m_pendingAsyncJobs.clear();
        m_history.redo();
        emit selectionChanged();
        emit imageChanged();
        emit documentChanged();
    }
}

void ImageController::jumpToHistoryState(int targetIndex)
{
    int curr = m_history.currentIndex();
    if (targetIndex == curr) return;
    if (targetIndex < 0 || targetIndex >= m_history.size()) return;

    if (targetIndex < curr) {
        for (int i = curr; i > targetIndex; --i)
            m_history.undo();
    } else {
        for (int i = curr + 1; i <= targetIndex; ++i)
            m_history.redo();
    }
    emit selectionChanged();
}

void ImageController::clearHistory()
{
    m_history.clear();
}

QStringList ImageController::historyStateNames() const
{
    QStringList names;
    for (int i = 0; i < m_history.size(); ++i) {
        const auto* cmd = m_history.commandAt(i);
        names.append(cmd ? cmd->name() : QString());
    }
    return names;
}
