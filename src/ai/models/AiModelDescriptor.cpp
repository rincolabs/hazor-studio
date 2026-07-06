#include "ai/models/AiModelDescriptor.hpp"

namespace AiModelTaxonomy {

QString originToString(AiModelOrigin origin)
{
    switch (origin) {
    case AiModelOrigin::Bundled:    return QStringLiteral("Bundled");
    case AiModelOrigin::Downloaded: return QStringLiteral("Downloaded");
    case AiModelOrigin::Custom:     return QStringLiteral("Custom");
    }
    return QStringLiteral("Custom");
}

QString originToKey(AiModelOrigin origin)
{
    switch (origin) {
    case AiModelOrigin::Bundled:    return QStringLiteral("bundled");
    case AiModelOrigin::Downloaded: return QStringLiteral("downloaded");
    case AiModelOrigin::Custom:     return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

QString statusToString(AiModelStatus status)
{
    switch (status) {
    case AiModelStatus::Valid:              return QStringLiteral("Valid");
    case AiModelStatus::Invalid:            return QStringLiteral("Invalid");
    case AiModelStatus::MissingFiles:       return QStringLiteral("Missing files");
    case AiModelStatus::UnsupportedSchema:  return QStringLiteral("Unsupported schema");
    case AiModelStatus::UnsupportedBackend: return QStringLiteral("Unsupported backend");
    case AiModelStatus::InvalidMetadata:    return QStringLiteral("Invalid metadata");
    }
    return QStringLiteral("Invalid");
}

QString roleToTask(const QString& role)
{
    const QString r = role.trimmed().toLower();
    if (r == QLatin1String("segmenter") || r == QLatin1String("segmentation")) return QStringLiteral("segmentation");
    if (r == QLatin1String("matting"))                                         return QStringLiteral("matting");
    if (r == QLatin1String("background-removal") || r == QLatin1String("background_removal"))
        return QStringLiteral("background_removal");
    if (r == QLatin1String("inpainting"))                                      return QStringLiteral("inpainting");
    if (r == QLatin1String("upscale"))                                         return QStringLiteral("upscale");
    return r;
}

QString taskToRole(const QString& task)
{
    const QString t = task.trimmed().toLower();
    if (t == QLatin1String("segmentation"))       return QStringLiteral("segmenter");
    if (t == QLatin1String("matting"))            return QStringLiteral("matting");
    if (t == QLatin1String("background_removal")) return QStringLiteral("background-removal");
    if (t == QLatin1String("inpainting"))         return QStringLiteral("inpainting");
    if (t == QLatin1String("upscale"))            return QStringLiteral("upscale");
    return t;
}

QString familyToType(const QString& family)
{
    const QString f = family.trimmed().toLower();
    if (f == QLatin1String("sam") || f == QLatin1String("sam1") || f == QLatin1String("sam2")
        || f == QLatin1String("mobile-sam") || f == QLatin1String("mobilesam"))
        return QStringLiteral("segment_anything");
    if (f.isEmpty())
        return QString();
    // birefnet / modnet / rmbg / u2net / isnet / lama / realesrgan map 1:1.
    return f;
}

QString taskToFolder(const QString& task)
{
    const QString t = task.trimmed().toLower();
    if (t == QLatin1String("segmentation"))       return QStringLiteral("segmenters");
    if (t == QLatin1String("background_removal")) return QStringLiteral("background-removal");
    if (t.isEmpty())                              return QStringLiteral("other");
    // matting / inpainting / upscale map 1:1.
    return t;
}

QStringList defaultCapabilitiesForTask(const QString& task)
{
    const QString t = task.trimmed().toLower();
    if (t == QLatin1String("segmentation"))
        return { QStringLiteral("point_prompt"), QStringLiteral("box_prompt"),
                 QStringLiteral("object_selection"), QStringLiteral("selection_mask") };
    if (t == QLatin1String("matting"))
        return { QStringLiteral("alpha_matte"), QStringLiteral("refine_mask"),
                 QStringLiteral("soft_edges"), QStringLiteral("foreground_extraction") };
    if (t == QLatin1String("background_removal"))
        return { QStringLiteral("background_removal"), QStringLiteral("foreground_mask"),
                 QStringLiteral("alpha_matte") };
    if (t == QLatin1String("inpainting"))
        return { QStringLiteral("inpaint"), QStringLiteral("inpainting"),
                 QStringLiteral("remove_object") };
    if (t == QLatin1String("upscale"))
        return { QStringLiteral("upscale") };
    return {};
}

QStringList allTasks()
{
    return { QStringLiteral("segmentation"), QStringLiteral("matting"),
             QStringLiteral("background_removal"), QStringLiteral("inpainting"),
             QStringLiteral("upscale") };
}

QString taskDisplayName(const QString& task)
{
    const QString t = task.trimmed().toLower();
    if (t == QLatin1String("segmentation"))       return QStringLiteral("Object Selection / Segmenters");
    if (t == QLatin1String("matting"))            return QStringLiteral("Matting / Refine Edge");
    if (t == QLatin1String("background_removal")) return QStringLiteral("Background Removal");
    if (t == QLatin1String("inpainting"))         return QStringLiteral("Inpainting / Remove Object");
    if (t == QLatin1String("upscale"))            return QStringLiteral("Upscale");
    return task;
}

} // namespace AiModelTaxonomy
