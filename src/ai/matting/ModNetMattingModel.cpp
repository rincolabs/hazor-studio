#include "ai/matting/ModNetMattingModel.hpp"

#include "ai/AiMaskPostProcessor.hpp"

std::shared_ptr<ModNetMattingModel> ModNetMattingModel::load(const QString& modelId, QString* error)
{
    auto m = std::shared_ptr<ModNetMattingModel>(new ModNetMattingModel());
    if (!m->m_core.load(modelId, QStringLiteral("modnet"), error))
        return nullptr;
    return m;
}

AiAlphaResult ModNetMattingModel::refine(const AiImageSnapshot& snapshot,
                                         const QImage* baseMask,
                                         const AiRefineOptions& /*options*/,
                                         const std::atomic<bool>* cancel,
                                         QString* error)
{
    AiAlphaResult r = m_core.runMatte(snapshot, cancel, "[AI][MATTE][MODNet]", error);
    if (!r.isValid())
        return r;
    if (baseMask)
        r.alpha = AiMaskPostProcessor::intersectWithBase(r.alpha, baseMask);
    return r;
}
