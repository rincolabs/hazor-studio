#include "ai/sd/SdCanvasBridge.hpp"

#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

} // namespace

SdCanvasBridge::SdCanvasBridge(QObject* parent)
    : QObject(parent)
{
}

void SdCanvasBridge::setController(ImageController* controller)
{
    m_controller = controller;
    // A run already in flight keeps going: it holds its own latch, and
    // insertImage() is what refuses to write into a document that is no longer
    // the active one.
}

bool SdCanvasBridge::hasDocument() const
{
    return m_controller && m_controller->document();
}

bool SdCanvasBridge::beginRun(QString* error)
{
    if (!hasDocument()) {
        if (error)
            *error = tr_("Open a document first.");
        return false;
    }
    m_runController = m_controller;
    m_inRun = true;
    return true;
}

void SdCanvasBridge::endRun()
{
    m_runController = nullptr;
    m_inRun = false;
}

bool SdCanvasBridge::captureImage(SdImageSource source, int layerIndex, QImage& out,
                                  QString* error) const
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc || doc->size.isEmpty()) {
        if (error)
            *error = tr_("Open a document first.");
        return false;
    }

    // Hazor's CPU compositor is the visual source of truth — the same path
    // export, flatten and the AI selection snapshots use — so what the engine
    // sees is what the user sees.
    RenderContext ctx;
    ctx.document = doc;
    ctx.outputSize = doc->size;
    ctx.targetType = RenderTargetType::Export;
    ctx.highQuality = true;

    QImage image;
    if (source == SdImageSource::Layer) {
        const int flatIndex = layerIndex >= 0 ? layerIndex : doc->activeFlatIndex;
        image = DocumentCompositor::compositeOnlyFlatIndex(doc, flatIndex, ctx);
        if (image.isNull()) {
            if (error) {
                *error = layerIndex >= 0
                    ? tr_("No raster layer at index %1.").arg(layerIndex)
                    : tr_("The active layer could not be rendered.");
            }
            return false;
        }
    } else {
        image = DocumentCompositor::composite(doc, ctx);
    }
    if (image.isNull()) {
        if (error)
            *error = tr_("The document could not be rendered.");
        return false;
    }
    out = image.convertToFormat(QImage::Format_RGBA8888);
    return !out.isNull();
}

bool SdCanvasBridge::captureMask(QImage& out, QString* error) const
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc || doc->size.isEmpty()) {
        if (error)
            *error = tr_("Open a document first.");
        return false;
    }
    if (!doc->selection.active() || doc->selection.isEmpty()) {
        if (error)
            *error = tr_("Make a selection to use as the mask.");
        return false;
    }

    const QImage& mask = doc->selection.image();
    if (mask.isNull()) {
        if (error)
            *error = tr_("The selection has no mask data.");
        return false;
    }
    // The engine wants a single-channel mask; the conversion to that layout
    // happens in the service, which is the only place that knows sd_image_t.
    // Here it only has to be document-sized coverage in a format the service
    // can read, with white meaning "regenerate".
    out = mask.convertToFormat(QImage::Format_RGBA8888);
    return !out.isNull();
}

bool SdCanvasBridge::insertImage(const QImage& image, const QString& layerName, QString* error)
{
    // importImage() walks the document model and pushes an undo command, so it
    // belongs to the GUI thread. The executor marshals the result there before
    // calling this; asserting it here is what makes that contract visible.
    Q_ASSERT(QThread::currentThread() == thread());

    if (image.isNull()) {
        if (error)
            *error = tr_("The generated image is empty.");
        return false;
    }

    // The document the run was started against has to still be the active one.
    // Three separate ways this can fail — the controller was destroyed, the
    // user switched tabs, the document was closed — and all three must drop the
    // pixels rather than guess at a destination.
    if (m_inRun && (!m_runController || m_runController != m_controller)) {
        if (error)
            *error = tr_("The document is no longer open; the image was not inserted.");
        qWarning() << "[SD] result dropped: the document was closed or changed during generation";
        return false;
    }
    if (!hasDocument()) {
        if (error)
            *error = tr_("The document is no longer open; the image was not inserted.");
        return false;
    }

    // Hazor's existing "QImage -> new pixel layer" path: it centres the layer
    // on the canvas, inserts it above the current stack, pushes the undoable
    // AddLayerCommand, selects it and bumps the composition generation so the
    // canvas and the Layers panel refresh. Nothing generative about the layer
    // it produces — it is an ordinary raster layer.
    if (!m_controller->importImage(image, layerName)) {
        if (error)
            *error = tr_("The layer could not be created.");
        return false;
    }
    return true;
}

} // namespace sdlocal
