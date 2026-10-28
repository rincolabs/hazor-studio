#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

class ImageController;

// The only part of the Stable Diffusion module that knows Hazor has documents.
//
// Everything on the engine side of this class deals in QImages: the workflow
// executor asks for pixels and hands pixels back, and stable-diffusion.cpp
// never sees a Layer, a Document or a compositor. That is also what keeps the
// executor compilable without the editor — ImageController is forward-declared
// here and included only by the implementation.
//
// Its other job is making sure a result lands in the document it was asked for.
// A generation takes minutes; tabs get switched and documents get closed in
// that time, and pixels pushed into the wrong document would be worse than
// pixels lost.
namespace sdlocal {

enum class SdImageSource {
    Composite,     // every visible layer, flattened — the default
    CurrentLayer,  // the active layer alone
};

class SdCanvasBridge : public QObject {
    Q_OBJECT
public:
    explicit SdCanvasBridge(QObject* parent = nullptr);

    // (Re)binds to the active document's controller; nullptr means no document
    // is open. Same lifecycle as the right-side panels.
    void setController(ImageController* controller);
    ImageController* controller() const { return m_controller; }
    bool hasDocument() const;

    // Latches the document this run targets. Everything inserted afterwards is
    // checked against that latch, so a run started on one document can only
    // ever write to that document.
    bool beginRun(QString* error);
    void endRun();

    // Pixels in document space, Format_RGBA8888. Must be called on the GUI
    // thread: both read the live document through Hazor's existing compositor.
    bool captureImage(SdImageSource source, QImage& out, QString* error) const;
    // The active selection as an 8-bit coverage mask at document resolution.
    // White is the selected area, which is also the area the engine
    // regenerates, so no polarity flip happens anywhere.
    bool captureMask(QImage& out, QString* error) const;

    // Inserts the generated image as an ordinary pixel layer, through the same
    // path Paste and Generative Fill use: centred, undoable, selected, with the
    // composition generation bumped so the canvas and the Layers panel refresh.
    // No generative layer type, no compositor change.
    bool insertImage(const QImage& image, const QString& layerName, QString* error);

private:
    ImageController* m_controller = nullptr;

    // The controller the in-flight run was started against. A QPointer so a
    // destroyed controller reads as null rather than as a dangling match.
    QPointer<ImageController> m_runController;
    bool m_inRun = false;
};

} // namespace sdlocal
