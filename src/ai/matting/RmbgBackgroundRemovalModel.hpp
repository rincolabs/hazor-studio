#pragma once

#include "ai/matting/AiMattingModel.hpp"
#include "ai/matting/OnnxAlphaModel.hpp"

#include <memory>

// RMBG dedicated background-removal model. Produces a foreground matte directly
// from the image with no prompt — the simplest path to a non-destructive
// background mask when no SAM click/box is available.
class RmbgBackgroundRemovalModel : public AiBackgroundRemovalModel {
public:
    static std::shared_ptr<RmbgBackgroundRemovalModel> load(const QString& modelId, QString* error);

    QString modelId() const override { return m_core.modelId(); }
    QString providerUsed() const override { return m_core.providerUsed(); }

    AiAlphaResult removeBackground(const AiImageSnapshot& snapshot,
                                   const AiBackgroundRemovalOptions& options,
                                   const std::atomic<bool>* cancel,
                                   QString* error) override;

private:
    OnnxAlphaModel m_core;
};
