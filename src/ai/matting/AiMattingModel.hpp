#pragma once

#include "ai/AiImageSnapshot.hpp"
#include "ai/matting/AiRefineTypes.hpp"

#include <QImage>
#include <QString>

#include <atomic>

// Etapa 3 model interfaces. These deliberately mirror the spec's separation of
// concerns: SAM detects objects (Sam1Segmenter), matting models improve edge
// quality, and background-removal models produce a foreground matte directly.
// Keeping them as distinct interfaces is what stops the AI layer collapsing into
// one giant class. All methods are heavy and must run on the AI worker thread.
//
//   Tool/UI ─> AiMaskPipeline ─> AiSegmenter (SAM)
//                              ─> AiMattingModel (BiRefNet, MODNet)
//                              ─> AiBackgroundRemovalModel (RMBG)
//                              ─> AiRuntimeManager ─> ONNX

// Refines / generates a soft alpha matte for an image. `baseMask`, when non-null,
// is a document-sized grayscale hint (e.g. a SAM selection) the model should keep
// the matte within; pass null to matte the whole frame. Edge/cleanup shaping from
// `options` is applied by the pipeline via AiMaskPostProcessor, not here — this
// interface only owns the model inference. Returns an invalid result with *error
// set on failure; never throws.
class AiMattingModel {
public:
    virtual ~AiMattingModel() = default;

    virtual QString modelId() const = 0;
    virtual QString providerUsed() const = 0;

    virtual AiAlphaResult refine(const AiImageSnapshot& snapshot,
                                 const QImage* baseMask,
                                 const AiRefineOptions& options,
                                 const std::atomic<bool>* cancel,
                                 QString* error) = 0;
};

// Produces a foreground matte directly from the image, with no prompt. RMBG is
// the reference implementation. Same threading/error contract as AiMattingModel.
class AiBackgroundRemovalModel {
public:
    virtual ~AiBackgroundRemovalModel() = default;

    virtual QString modelId() const = 0;
    virtual QString providerUsed() const = 0;

    virtual AiAlphaResult removeBackground(const AiImageSnapshot& snapshot,
                                           const AiBackgroundRemovalOptions& options,
                                           const std::atomic<bool>* cancel,
                                           QString* error) = 0;
};
