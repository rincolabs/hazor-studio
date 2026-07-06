#include "ai/matting/AiRefineModelFactory.hpp"

#include "ai/matting/BiRefNetMattingModel.hpp"
#include "ai/matting/ModNetMattingModel.hpp"
#include "ai/matting/RmbgBackgroundRemovalModel.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelDescriptor.hpp"

#include <QDebug>

namespace {

// Models are resolved by task/capability via the registry — never by file name or
// hardcoded paths.
QList<AiModelDescriptor> mattingModels()
{
    return AiModelRegistry::instance()->modelsByTask(QStringLiteral("matting"));
}

QList<AiModelDescriptor> backgroundRemovalModels()
{
    auto* reg = AiModelRegistry::instance();
    QList<AiModelDescriptor> out = reg->modelsByTask(QStringLiteral("background_removal"));
    // Matting models can matte the whole frame, so they double as removers.
    for (const AiModelDescriptor& d : reg->modelsByTask(QStringLiteral("matting")))
        out << d;
    return out;
}

// Preference order for "Auto": better edge quality first.
int mattingRank(const QString& family)
{
    const QString f = family.toLower();
    if (f == QLatin1String("birefnet")) return 0;
    if (f == QLatin1String("modnet"))   return 1;
    return 2;
}

} // namespace

namespace AiRefineModelFactory {

QStringList installedMattingModels()
{
    QStringList ids;
    for (const AiModelDescriptor& d : mattingModels())
        ids << d.id;
    return ids;
}

QStringList installedBackgroundRemovalModels()
{
    QStringList ids;
    for (const AiModelDescriptor& d : backgroundRemovalModels())
        if (!ids.contains(d.id))
            ids << d.id;
    return ids;
}

QString resolveMattingModelId(const QString& requested)
{
    const auto models = mattingModels();
    if (!requested.isEmpty() && requested != QLatin1String("auto")) {
        for (const AiModelDescriptor& d : models)
            if (d.id == requested)
                return requested;
        return QString(); // explicitly requested but not a usable matting model
    }
    // Auto: honour the user-configured default matting model when it is installed.
    const QString uid = AiModelRegistry::instance()->userDefaultModelId(QStringLiteral("matting"));
    if (!uid.isEmpty())
        for (const AiModelDescriptor& d : models)
            if (d.id == uid)
                return uid;
    // Otherwise the best-ranked matting model.
    QString best;
    int bestRank = 1000;
    for (const AiModelDescriptor& d : models) {
        const int r = mattingRank(d.family);
        if (r < bestRank) { bestRank = r; best = d.id; }
    }
    return best;
}

QString resolveBackgroundRemovalModelId(const QString& requested)
{
    const auto models = backgroundRemovalModels();
    if (!requested.isEmpty() && requested != QLatin1String("auto")) {
        for (const AiModelDescriptor& d : models)
            if (d.id == requested)
                return requested;
        return QString();
    }
    // Auto: honour the user-configured default for either compatible category
    // (a dedicated background-removal model, or a matting model used as a remover).
    auto* reg = AiModelRegistry::instance();
    for (const char* task : { "background_removal", "matting" }) {
        const QString uid = reg->userDefaultModelId(QString::fromLatin1(task));
        if (uid.isEmpty())
            continue;
        for (const AiModelDescriptor& d : models)
            if (d.id == uid)
                return uid;
    }
    // Otherwise prefer a dedicated background-removal model (RMBG) over a matting one.
    QString fallback;
    for (const AiModelDescriptor& d : models) {
        if (d.task == QLatin1String("background_removal"))
            return d.id;
        if (fallback.isEmpty())
            fallback = d.id;
    }
    return fallback;
}

std::shared_ptr<AiMattingModel> createMatting(const QString& modelId, QString* error)
{
    const auto d = AiModelRegistry::instance()->modelById(modelId);
    if (!d || d->task != QLatin1String("matting")) {
        if (error) *error = QStringLiteral("Matting model is not installed: %1").arg(modelId);
        return nullptr;
    }
    if (d->family.toLower() == QLatin1String("modnet"))
        return ModNetMattingModel::load(modelId, error);
    // BiRefNet and any other generic matting family use the BiRefNet path.
    return BiRefNetMattingModel::load(modelId, error);
}

std::shared_ptr<AiBackgroundRemovalModel> createBackgroundRemoval(const QString& modelId, QString* error)
{
    const auto d = AiModelRegistry::instance()->modelById(modelId);
    if (!d || (d->task != QLatin1String("background_removal") && d->task != QLatin1String("matting"))) {
        if (error) *error = QStringLiteral("Background-removal model is not installed: %1").arg(modelId);
        return nullptr;
    }
    // RMBG is the dedicated implementation; any other single-file model is run
    // through the same generic alpha core.
    return RmbgBackgroundRemovalModel::load(modelId, error);
}

} // namespace AiRefineModelFactory
