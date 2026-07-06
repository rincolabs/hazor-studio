#pragma once

#include "ai/AiImageSnapshot.hpp"
#include "ai/AiSelectionTypes.hpp"
#include "ai/matting/AiRefineTypes.hpp"
#include "ai/sam/SamEmbeddingCache.hpp"

#include <QImage>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

class Sam1Segmenter;
class AiMattingModel;
class AiBackgroundRemovalModel;
struct SamEmbedding;

// Compute-only orchestrator that turns a request (snapshot + prompt + model) into
// a document-space mask. It is the single entry point the tool/UI uses; it knows
// nothing about tensors (that's the segmenter) and nothing about
// Document/Layer/Selection (that's the controller). Runs on the AI worker
// thread; one instance is owned per tool controller.
//
//   Tool ──> AiMaskPipeline ──> Sam1Segmenter ──> AiRuntimeManager ──> ONNX
//
// Etapa 3 will add matting/background-removal models behind the same surface.
class AiMaskPipeline {
public:
    struct Request {
        AiImageSnapshot snapshot;
        AiPrompt prompt;                 // document-pixel coordinates
        QString segmenterModelId;
        AiSelectionOptions options;
        AiToolMode mode = AiToolMode::SelectObject;

        // ── Etapa 3: refinement / matting ──
        AiRefineOptions refine;          // edge/cleanup + matting model selection
        AiBackgroundRemovalEngine bgEngine = AiBackgroundRemovalEngine::Auto;
        QImage baseMask;                 // document-sized grayscale, for RefineMask
    };

    struct Result {
        AiMaskResult mask;
        QString error;
        QString providerUsed;
        bool fromCache = false;
        bool refined = false;            // a matting model contributed to the mask
        bool cpuFallbackUsed = false;    // a CUDA OOM forced a one-shot CPU retry
        bool ok() const { return mask.isValid(); }
    };

    // Reports coarse phase labels ("Building embedding", "Selecting object"…)
    // for the tool's status UI. Called from the worker thread.
    using Progress = std::function<void(const QString&)>;

    AiMaskPipeline();
    ~AiMaskPipeline();

    void setEmbeddingCacheEnabled(bool enabled) { m_cacheEnabled = enabled; }
    void setMaxCachedDocuments(int n) { m_cache.setMaxEntries(n); }

    // Click / box prompt → object selection (optionally edge-refined).
    Result runObjectSelection(const Request& request, const std::atomic<bool>* cancel = nullptr,
                              const Progress& progress = nullptr);
    // Auto main-subject mask for Remove Background. The engine (SAM+refine / RMBG /
    // BiRefNet / Auto) comes from request.bgEngine; only background-removal/matting
    // models are used (plus SAM only when the user explicitly picks SAM + Refine).
    Result runRemoveBackground(const Request& request, const std::atomic<bool>* cancel = nullptr,
                               const Progress& progress = nullptr);
    // Auto main-subject mask for Select Subject. Always SAM-driven (same model and
    // decoder as a box/click prompt), so its result is consistent with a manual box
    // around the same object. Never falls back to a matting/background model.
    Result runSelectSubject(const Request& request, const std::atomic<bool>* cancel = nullptr,
                            const Progress& progress = nullptr);
    // Refine an existing mask (request.baseMask) with a matting model and/or the
    // edge/cleanup post-processor, without re-running SAM.
    Result runRefineExistingMask(const Request& request, const std::atomic<bool>* cancel = nullptr,
                                 const Progress& progress = nullptr);

    // Cache maintenance, all invoked from the worker thread or guarded internally.
    SamEmbeddingCache& embeddingCache() { return m_cache; }
    void invalidateDocument(quint64 documentId) { m_cache.invalidateDocument(documentId); }
    void reset();   // drop the loaded segmenter + clear cache (provider change)

private:
    // The actual work; the public run* methods wrap these with the CUDA-OOM→CPU
    // retry (spec §19). Splitting keeps the retry orchestration out of the
    // mask/segmentation logic, which is unchanged.
    Result runObjectSelectionInternal(const Request& request, const std::atomic<bool>* cancel,
                                      const Progress& progress);
    Result runRemoveBackgroundInternal(const Request& request, const std::atomic<bool>* cancel,
                                       const Progress& progress);
    // SAM main-subject selection shared by Select Subject and the Remove
    // Background "SAM + Refine" engine. The progress label adapts to request.mode.
    Result runSamSubjectInternal(const Request& request, const std::atomic<bool>* cancel,
                                 const Progress& progress);
    Result runRefineExistingMaskInternal(const Request& request, const std::atomic<bool>* cancel,
                                          const Progress& progress);

    // Runs `run` and, on a CUDA out-of-memory failure, retries it exactly once on
    // CPU (spec §19): drops the GPU sessions, pins the runtime to CPU and re-runs
    // the same request. With CPU fallback disabled, replaces the error with a
    // friendly hint. `label` is the model id, for logging only.
    Result runWithCpuFallback(const QString& label, const std::function<Result()>& run);

    std::shared_ptr<Sam1Segmenter> ensureSegmenter(const QString& modelId, QString* error,
                                                   const Progress& progress);
    std::shared_ptr<const SamEmbedding> ensureEmbedding(const Request& request,
                                                        Sam1Segmenter& segmenter,
                                                        bool* fromCache,
                                                        QString* error,
                                                        const std::atomic<bool>* cancel,
                                                        const Progress& progress);
    std::shared_ptr<AiMattingModel> ensureMatting(const QString& modelId, QString* error,
                                                  const Progress& progress);
    std::shared_ptr<AiBackgroundRemovalModel> ensureBgRemoval(const QString& modelId, QString* error,
                                                              const Progress& progress);

    // Applies the matting model (when one resolves) then the edge/cleanup adapter
    // to a base mask, returning a document-space AiMaskResult. Used by every
    // refine-capable path. `refined` is set when a matting model contributed.
    AiMaskResult refineMask(const Request& request, const QImage& baseMask,
                            bool* refined, const std::atomic<bool>* cancel,
                            const Progress& progress);

    SamEmbeddingCache m_cache;
    std::shared_ptr<Sam1Segmenter> m_segmenter;  // currently loaded SAM model
    std::shared_ptr<AiMattingModel> m_matting;   // currently loaded matting model
    std::shared_ptr<AiBackgroundRemovalModel> m_bgRemoval; // loaded bg-removal model
    bool m_cacheEnabled = true;
};
