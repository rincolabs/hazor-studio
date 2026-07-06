#pragma once

#include "ai/matting/AiMattingModel.hpp"
#include "ai/matting/OnnxAlphaModel.hpp"

#include <memory>

// BiRefNet edge-refinement / foreground matting model. Produces a high-quality
// soft matte of the salient foreground; when given a base mask it constrains the
// matte to that region so it refines (rather than replaces) a SAM selection.
class BiRefNetMattingModel : public AiMattingModel {
public:
    static std::shared_ptr<BiRefNetMattingModel> load(const QString& modelId, QString* error);

    QString modelId() const override { return m_core.modelId(); }
    QString providerUsed() const override { return m_core.providerUsed(); }

    AiAlphaResult refine(const AiImageSnapshot& snapshot,
                         const QImage* baseMask,
                         const AiRefineOptions& options,
                         const std::atomic<bool>* cancel,
                         QString* error) override;

private:
    OnnxAlphaModel m_core;
};
