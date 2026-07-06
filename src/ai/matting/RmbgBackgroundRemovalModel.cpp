#include "ai/matting/RmbgBackgroundRemovalModel.hpp"

std::shared_ptr<RmbgBackgroundRemovalModel> RmbgBackgroundRemovalModel::load(const QString& modelId,
                                                                             QString* error)
{
    auto m = std::shared_ptr<RmbgBackgroundRemovalModel>(new RmbgBackgroundRemovalModel());
    if (!m->m_core.load(modelId, QStringLiteral("rmbg"), error))
        return nullptr;
    return m;
}

AiAlphaResult RmbgBackgroundRemovalModel::removeBackground(const AiImageSnapshot& snapshot,
                                                           const AiBackgroundRemovalOptions& /*options*/,
                                                           const std::atomic<bool>* cancel,
                                                           QString* error)
{
    // Edge/cleanup shaping from options.refine is applied by the pipeline; this
    // model only owns the foreground matte.
    return m_core.runMatte(snapshot, cancel, "[AI][MATTE][RMBG]", error);
}
