#pragma once

#include <QImage>
#include <QSize>
#include <QPointF>
#include <QString>

#include <cstdint>

// A thread-safe, immutable copy of the pixels the AI segmenter should see, taken
// on the UI thread and handed to worker jobs. It deliberately knows nothing
// about Document/Layer: the canvas fills it from whatever source the user picked
// (current layer or full composite), already rendered into document space.
//
// The snapshot is captured at a "working resolution" (its own size may be
// smaller than the document for speed); all coordinate mapping between the
// document and the snapshot goes through the scale helpers here so the rest of
// the pipeline never has to know the document was downscaled.
struct AiImageSnapshot {
    // Pixels in document orientation, Format_RGBA8888 (or RGB888). May be smaller
    // than documentSize when a working-resolution limit is applied.
    QImage image;

    QSize documentSize;          // full document size in pixels

    // Identity used to validate/cancel stale jobs and to key the embedding cache.
    quint64 documentId = 0;      // stable id of the source document (pointer-derived)
    int     sourceLayerIndex = -1; // active layer flat index, or -1 for composite
    quint64 sourceRevision = 0;  // content hash of `image`; changes iff pixels change

    QString sourceDescription;   // "all-visible" | "current-layer" (for logs)

    bool isValid() const
    {
        return !image.isNull() && documentSize.width() > 0 && documentSize.height() > 0;
    }

    int snapshotWidth()  const { return image.width(); }
    int snapshotHeight() const { return image.height(); }

    double scaleDocToSnapX() const
    {
        return documentSize.width() > 0
            ? double(image.width()) / double(documentSize.width()) : 1.0;
    }
    double scaleDocToSnapY() const
    {
        return documentSize.height() > 0
            ? double(image.height()) / double(documentSize.height()) : 1.0;
    }

    QPointF documentToSnapshot(const QPointF& docPt) const
    {
        return QPointF(docPt.x() * scaleDocToSnapX(), docPt.y() * scaleDocToSnapY());
    }
    QPointF snapshotToDocument(const QPointF& snapPt) const
    {
        const double sx = scaleDocToSnapX();
        const double sy = scaleDocToSnapY();
        return QPointF(sx > 0 ? snapPt.x() / sx : 0.0,
                       sy > 0 ? snapPt.y() / sy : 0.0);
    }

    // Stable 64-bit hash of the snapshot pixels — used as sourceRevision so the
    // embedding cache is invalidated exactly when the visible pixels change
    // (without needing a per-layer generation counter on the document model).
    static quint64 hashImage(const QImage& img);
};
