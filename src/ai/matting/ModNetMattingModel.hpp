#pragma once

#include "ai/matting/AiMattingModel.hpp"
#include "ai/matting/OnnxAlphaModel.hpp"

#include <memory>

// MODNet portrait/alpha-matting model. Best for people, hair and soft edges; like
// BiRefNet it constrains its matte to a supplied base mask when refining a SAM
// selection.
class ModNetMattingModel : public AiMattingModel {
public:
    static std::shared_ptr<ModNetMattingModel> load(const QString& modelId, QString* error);

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
