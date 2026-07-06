#include "ai/compat/AiModelCompatibilityChecker.hpp"

#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelDescriptor.hpp"

#include <QCoreApplication>

namespace AiModelCompatibilityChecker {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("AiModelCompatibility", s); }

// True when any valid model matches the capability OR the task bucket.
bool anyValidFor(AiModelRegistry* reg, const QString& capability, const QString& task)
{
    for (const AiModelDescriptor& d : reg->modelsByCapability(capability))
        if (d.isValid())
            return true;
    if (!task.isEmpty())
        for (const AiModelDescriptor& d : reg->modelsByTask(task))
            if (d.isValid())
                return true;
    return false;
}

} // namespace

bool hasRequiredModel(const QString& capabilityOrTask)
{
    auto* reg = AiModelRegistry::instance();
    return anyValidFor(reg, capabilityOrTask, capabilityOrTask);
}

AiModelCompatibility check()
{
    AiModelCompatibility out;
    auto* reg = AiModelRegistry::instance();

    for (const AiModelDescriptor& d : reg->validModels()) {
        out.validModels++;
        switch (d.origin) {
        case AiModelOrigin::Bundled:    out.bundledModels++; break;
        case AiModelOrigin::Downloaded: out.downloadedModels++; break;
        case AiModelOrigin::Custom:     out.customModels++; break;
        }
    }
    out.invalidModels = reg->invalidModels().size();

    out.hasObjectSelectionModel = anyValidFor(reg, QStringLiteral("object_selection"),
                                              QStringLiteral("segmentation"));
    out.hasMattingModel = anyValidFor(reg, QStringLiteral("alpha_matte"),
                                      QStringLiteral("matting"));
    out.hasBackgroundRemovalModel = anyValidFor(reg, QStringLiteral("background_removal"),
                                                QStringLiteral("background_removal"))
        || anyValidFor(reg, QStringLiteral("foreground_mask"), QString());

    if (out.validModels == 0) {
        out.status = AiCompatibilityStatus::MissingDependency;
        out.messages.push_back({ AiCompatibilitySeverity::Warning,
            tr("No AI models installed"),
            tr("No compatible AI model is installed. Install or enable a model in "
               "Settings > AI / Machine Learning > Models."),
            QString(), tr("AI Settings"), QStringLiteral("open_ai_settings") });
    } else {
        out.status = AiCompatibilityStatus::Compatible;
        if (!out.hasObjectSelectionModel)
            out.messages.push_back({ AiCompatibilitySeverity::Info,
                tr("No segmentation model"),
                tr("No segmentation model is installed for Object Selection."),
                QString(), tr("AI Settings"), QStringLiteral("open_ai_settings") });
    }
    if (out.invalidModels > 0)
        out.messages.push_back({ AiCompatibilitySeverity::Warning,
            tr("Invalid models ignored"),
            QCoreApplication::translate("AiModelCompatibility",
                "%n model folder(s) could not be loaded and were ignored.",
                nullptr, out.invalidModels),
            QString(), QString(), QString() });

    return out;
}

} // namespace AiModelCompatibilityChecker
