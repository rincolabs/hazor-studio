#pragma once

#include <QFuture>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>

#include "AnimationTypes.hpp"
#include "io/ImageIO.hpp"

class Document;

namespace anim {

struct SequenceExportOptions {
    QString outputDirectory;
    QString fileNamePrefix = QStringLiteral("frame");
    // When omitted, the document playback range is exported.
    std::optional<Frame> startFrame;
    std::optional<Frame> endFrame;
    int framePadding = 4;
    bool overwriteExisting = false;
    ExportOptions imageOptions;
};

struct SequenceExportResult {
    bool ok = false;
    bool cancelled = false;
    qint64 exportedFrameCount = 0;
    QStringList writtenFiles;
    QString errorMessage;
};

// Animation sequence export built on the normal CPU export pipeline. The live
// document is snapshotted before work is dispatched; only that isolated clone
// is evaluated and rendered, so exporting never changes the editor playhead or
// the live nodes' evaluated state.
class AnimationExportService {
public:
    // Build on the document's owner thread. Layer pixel buffers are COW-shared,
    // while the tree, layer metadata, evaluated state and animation model are
    // independently owned by the snapshot (the same contract used by the
    // existing asynchronous CPU projection builder).
    static std::shared_ptr<Document> createRenderSnapshot(const Document& source);

    // Synchronous worker operation for callers that already manage threading.
    static SequenceExportResult exportSequence(
        const std::shared_ptr<Document>& snapshot,
        const SequenceExportOptions& options,
        const std::shared_ptr<std::atomic_bool>& cancelRequested = {});

    // Convenience entry point: snapshots synchronously on the caller/owner
    // thread, then evaluates, composites and writes the sequence on Qt's worker
    // pool. The returned future never captures the live Document.
    static QFuture<SequenceExportResult> exportSequenceAsync(
        const Document& source,
        SequenceExportOptions options,
        std::shared_ptr<std::atomic_bool> cancelRequested = {});
};

} // namespace anim
