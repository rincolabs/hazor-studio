#include "ai/sd/SdGenerationTypes.hpp"

// The names here are the module's wire vocabulary: they appear in workflow JSON
// documents and in the log, so they are stable strings rather than anything
// derived from the enumerator spelling. Where a name also exists in
// stable-diffusion.cpp's own vocabulary (the weight types), it matches, so a
// value copied out of the library's docs works in a Hazor workflow.
namespace sdlocal {

QString operationName(SdOperation operation)
{
    switch (operation) {
    case SdOperation::Img2Img:  return QStringLiteral("img2img");
    case SdOperation::Inpaint:  return QStringLiteral("inpaint");
    case SdOperation::Canny:    return QStringLiteral("canny");
    case SdOperation::Upscale:  return QStringLiteral("upscale");
    case SdOperation::ADetailer:return QStringLiteral("adetailer");
    case SdOperation::Txt2Img:  break;
    }
    return QStringLiteral("txt2img");
}

bool operationFromName(const QString& name, SdOperation& out)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("txt2img") || n == QLatin1String("text2img")) {
        out = SdOperation::Txt2Img;
        return true;
    }
    if (n == QLatin1String("img2img") || n == QLatin1String("image2image")) {
        out = SdOperation::Img2Img;
        return true;
    }
    if (n == QLatin1String("inpaint") || n == QLatin1String("inpainting")) {
        out = SdOperation::Inpaint;
        return true;
    }
    if (n == QLatin1String("canny")) { out = SdOperation::Canny; return true; }
    if (n == QLatin1String("upscale")) { out = SdOperation::Upscale; return true; }
    if (n == QLatin1String("adetailer")) { out = SdOperation::ADetailer; return true; }
    return false;
}

QString generationStateName(SdGenerationState state)
{
    switch (state) {
    case SdGenerationState::Loading:    return QStringLiteral("loading");
    case SdGenerationState::Generating: return QStringLiteral("generating");
    case SdGenerationState::Completed:  return QStringLiteral("completed");
    case SdGenerationState::Failed:     return QStringLiteral("failed");
    case SdGenerationState::Cancelled:  return QStringLiteral("cancelled");
    case SdGenerationState::Idle:       break;
    }
    return QStringLiteral("idle");
}

QString weightTypeName(SdWeightType type)
{
    switch (type) {
    case SdWeightType::F32:  return QStringLiteral("f32");
    case SdWeightType::F16:  return QStringLiteral("f16");
    case SdWeightType::Q8_0: return QStringLiteral("q8_0");
    case SdWeightType::Q5_1: return QStringLiteral("q5_1");
    case SdWeightType::Q5_0: return QStringLiteral("q5_0");
    case SdWeightType::Q4_1: return QStringLiteral("q4_1");
    case SdWeightType::Q4_0: return QStringLiteral("q4_0");
    case SdWeightType::Auto: break;
    }
    return QStringLiteral("auto");
}

bool weightTypeFromName(const QString& name, SdWeightType& out)
{
    const QString n = name.trimmed().toLower();
    if (n.isEmpty() || n == QLatin1String("auto"))  { out = SdWeightType::Auto; return true; }
    if (n == QLatin1String("f32") || n == QLatin1String("fp32")) { out = SdWeightType::F32; return true; }
    if (n == QLatin1String("f16") || n == QLatin1String("fp16")) { out = SdWeightType::F16; return true; }
    if (n == QLatin1String("q8_0")) { out = SdWeightType::Q8_0; return true; }
    if (n == QLatin1String("q5_1")) { out = SdWeightType::Q5_1; return true; }
    if (n == QLatin1String("q5_0")) { out = SdWeightType::Q5_0; return true; }
    if (n == QLatin1String("q4_1")) { out = SdWeightType::Q4_1; return true; }
    if (n == QLatin1String("q4_0")) { out = SdWeightType::Q4_0; return true; }
    return false;
}

} // namespace sdlocal
