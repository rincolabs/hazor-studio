#include "ai/matting/BiRefNetMattingModel.hpp"

#include "ai/AiMaskPostProcessor.hpp"

#include <QDebug>

std::shared_ptr<BiRefNetMattingModel> BiRefNetMattingModel::load(const QString& modelId,
                                                                 QString* error)
{
    auto m = std::shared_ptr<BiRefNetMattingModel>(new BiRefNetMattingModel());
    if (!m->m_core.load(modelId, QStringLiteral("birefnet"), error))
        return nullptr;
    return m;
}

AiAlphaResult BiRefNetMattingModel::refine(const AiImageSnapshot& snapshot,
                                           const QImage* baseMask,
                                           const AiRefineOptions& /*options*/,
                                           const std::atomic<bool>* cancel,
                                           QString* error)
{
    AiAlphaResult r = m_core.runMatte(snapshot, cancel, "[AI][MATTE][BiRefNet]", error);
    if (!r.isValid())
        return r;
    // Constrain the full-frame matte to the user's region when refining a base
    // selection (edge/cleanup shaping is applied later by the pipeline).
    if (baseMask)
        r.alpha = AiMaskPostProcessor::intersectWithBase(r.alpha, baseMask);
    return r;
}
