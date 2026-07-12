#pragma once

#include <QColor>
#include <QFuture>
#include <QSize>
#include <QString>
#include <QStringList>
#include <Qt>

#include <atomic>
#include <memory>

#include "AnimationTypes.hpp"
#include "io/ImageIO.hpp"

class Document;

namespace anim {

// The concrete output the user asked for. MP4 and GIF write a single file
// through FFmpeg; the *Sequence formats write one image per frame through the
// normal image codec registry.
enum class ExportFormat {
    Mp4,
    Gif,
    PngSequence,
    JpegSequence,
    WebpSequence,
};

bool isSequenceFormat(ExportFormat format);
bool formatSupportsAlpha(ExportFormat format);
// Canonical lowercase file extension for the format (mp4/gif/png/jpg/webp).
QString extensionForFormat(ExportFormat format);

// How the composited canvas frame is mapped onto the (possibly different)
// output resolution. Mirrors the classic Fit / Fill / Stretch controls.
enum class FitMode {
    Fit,      // letterbox: whole canvas visible, background may show
    Fill,     // crop: fill the output, trim the overflow
    Stretch,  // distort to the exact output aspect ratio
};

// Shared, thread-safe progress + cancellation channel. Owned by the UI as a
// shared_ptr and read by a poll timer while the worker thread advances it.
struct ExportProgress {
    std::atomic<qint64> completedFrames{0};
    std::atomic<qint64> totalFrames{0};
    std::atomic_bool cancelRequested{false};
};

struct AnimationExportRequest {
    ExportFormat format = ExportFormat::Mp4;

    // ── Output destination ──────────────────────────────────────────────────
    // Single-file formats use outputFile; sequences use the directory + prefix.
    QString outputFile;
    QString outputDirectory;
    QString fileNamePrefix = QStringLiteral("frame");
    int framePadding = 4;
    bool overwriteExisting = false;

    // ── Frame range (already resolved to concrete document frames) ──────────
    Frame startFrame = 0;
    Frame endFrame = 0;

    double fps = 24.0;

    // ── Output frame layout ─────────────────────────────────────────────────
    QSize targetSize;                                   // exported pixel size
    FitMode fitMode = FitMode::Fit;
    Qt::TransformationMode resampleMode = Qt::SmoothTransformation;
    QColor backgroundColor = QColor(0, 0, 0);           // used where alpha is off
    bool transparency = true;                           // keep alpha where supported

    // ── MP4 ─────────────────────────────────────────────────────────────────
    QString videoCodec = QStringLiteral("libx264");     // libx264 / libx265
    int crf = 20;                                       // lower = better quality
    QString encoderPreset = QStringLiteral("medium");
    QString pixelFormat = QStringLiteral("yuv420p");
    bool hardwareEncoder = false;

    // ── GIF ─────────────────────────────────────────────────────────────────
    int gifColors = 256;
    bool gifDither = true;
    int gifLoop = 0;                                    // 0 forever, -1 once, N times

    // ── Image sequence ──────────────────────────────────────────────────────
    // quality / compression / preserveAlpha / format. `format` is filled in by
    // the service from the chosen ExportFormat.
    ExportOptions imageOptions;
};

struct AnimationExportResult {
    bool ok = false;
    bool cancelled = false;
    qint64 exportedFrameCount = 0;
    QString outputPath;          // file (mp4/gif) or directory (sequence)
    QStringList writtenFiles;    // sequence: every per-frame file written
    QString errorMessage;
};

// Animation export built on the normal CPU composite pipeline. The live
// document is snapshotted before any work is dispatched; only that isolated
// clone is evaluated and rendered, so exporting never changes the editor
// playhead or the live nodes' evaluated state.
class AnimationExportService {
public:
    // Build on the document's owner thread. Layer pixel buffers are COW-shared,
    // while the tree, layer metadata, evaluated state and animation model are
    // independently owned by the snapshot.
    static std::shared_ptr<Document> createRenderSnapshot(const Document& source);

    // ── FFmpeg discovery (MP4 / GIF only) ───────────────────────────────────
    // Resolves the executable: an app-bundled binary first, then the
    // HAZOR_FFMPEG environment override, then the system PATH. Returns an empty
    // string when nothing runnable is found.
    static QString detectFfmpegExecutable();
    static bool isFfmpegAvailable();

    // Synchronous worker operation for callers that already manage threading.
    static AnimationExportResult exportAnimation(
        const std::shared_ptr<Document>& snapshot,
        const AnimationExportRequest& request,
        const std::shared_ptr<ExportProgress>& progress = {});

    // Convenience entry point: snapshots synchronously on the caller/owner
    // thread, then evaluates, composites and encodes on Qt's worker pool. The
    // returned future never captures the live Document.
    static QFuture<AnimationExportResult> exportAnimationAsync(
        const Document& source,
        AnimationExportRequest request,
        std::shared_ptr<ExportProgress> progress = {});
};

} // namespace anim
