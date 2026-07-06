#include "ai/compat/AiCompatibilityManager.hpp"

#include "ai/compat/AiPlatformDetector.hpp"
#include "ai/compat/AiOsCompatibilityProfile.hpp"
#include "ai/compat/AiRuntimeCompatibilityProfile.hpp"
#include "ai/compat/AiCudaCompatibilityChecker.hpp"
#include "ai/compat/AiProviderCompatibilityChecker.hpp"
#include "ai/compat/AiModelCompatibilityChecker.hpp"

#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/models/AiModelRegistry.hpp"

#include <QCoreApplication>

QString aiCompatibilityStatusLabel(AiCompatibilityStatus status)
{
    switch (status) {
    case AiCompatibilityStatus::Compatible:          return QCoreApplication::translate("AiCompatibility", "Ready");
    case AiCompatibilityStatus::PartiallyCompatible: return QCoreApplication::translate("AiCompatibility", "Partially available");
    case AiCompatibilityStatus::MissingDependency:   return QCoreApplication::translate("AiCompatibility", "Missing dependencies");
    case AiCompatibilityStatus::Incompatible:        return QCoreApplication::translate("AiCompatibility", "Incompatible");
    case AiCompatibilityStatus::Unsupported:         return QCoreApplication::translate("AiCompatibility", "Unsupported");
    case AiCompatibilityStatus::Disabled:            return QCoreApplication::translate("AiCompatibility", "Disabled");
    case AiCompatibilityStatus::Unknown:             return QCoreApplication::translate("AiCompatibility", "Unknown");
    }
    return QString();
}

AiCompatibilityManager* AiCompatibilityManager::instance()
{
    static AiCompatibilityManager* s_instance = new AiCompatibilityManager();
    return s_instance;
}

AiCompatibilityManager::AiCompatibilityManager(QObject* parent)
    : QObject(parent)
{
    // Recompute when the runtime config or installed models change. Direct
    // connections are fine — everything lives on the UI thread.
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::runtimeStatusChanged,
            this, &AiCompatibilityManager::refresh);
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::settingsChanged,
            this, &AiCompatibilityManager::refresh);
    connect(AiModelRegistry::instance(), &AiModelRegistry::installedModelsChanged,
            this, &AiCompatibilityManager::refresh);
}

void AiCompatibilityManager::refresh()
{
    m_cache.reset();
    emit compatibilityChanged();
}

const AiCompatibilityReport& AiCompatibilityManager::report() const
{
    if (!m_cache)
        m_cache = computeReport();
    return *m_cache;
}

AiCompatibilityReport AiCompatibilityManager::computeReport() const
{
    auto* rt = AiRuntimeManager::instance();

    AiCompatibilityReport r;

    const AiPlatformInfo platform = AiPlatformDetector::detect();
    r.platform = AiOsCompatibilityProfile::classify(platform);
    r.runtime = AiRuntimeCompatibilityProfile::check();

    const bool runtimeAvailable = r.runtime.onnxRuntimeAvailable;
    const QStringList runtimeProviders = rt->deviceInfo().availableProviders;
    const AiCudaInfo cuda = AiCudaCompatibilityChecker::detect();
    r.providers = AiProviderCompatibilityChecker::check(platform, runtimeAvailable,
                                                        runtimeProviders, cuda);

    r.models = AiModelCompatibilityChecker::check();

    // ── Global status (the "AI status:" line in Settings) ──
    const bool enabled = rt->settings().enabled;
    if (!runtimeAvailable) {
        r.globalStatus = AiCompatibilityStatus::MissingDependency;
    } else if (!enabled) {
        r.globalStatus = AiCompatibilityStatus::Disabled;
        r.messages.push_back({ AiCompatibilitySeverity::Info,
            QCoreApplication::translate("AiCompatibility", "AI features are disabled"),
            QCoreApplication::translate("AiCompatibility",
                "AI features are disabled. Tools that require AI will not be activated."),
            QString(), QString(), QString() });
    } else if (!r.models.hasObjectSelectionModel && r.models.validModels == 0) {
        r.globalStatus = AiCompatibilityStatus::MissingDependency;
    } else if (r.platform.status == AiCompatibilityStatus::Unsupported
               || r.platform.status == AiCompatibilityStatus::PartiallyCompatible) {
        r.globalStatus = AiCompatibilityStatus::PartiallyCompatible;
    } else {
        r.globalStatus = AiCompatibilityStatus::Compatible;
    }

    return r;
}

AiCompatibilityReport AiCompatibilityManager::buildReport() const
{
    return report();
}

QList<AiProviderCompatibility> AiCompatibilityManager::compatibleProviders() const
{
    QList<AiProviderCompatibility> out;
    for (const AiProviderCompatibility& p : report().providers)
        if (p.isSelectable())
            out.push_back(p);
    return out;
}

QList<AiProviderCompatibility> AiCompatibilityManager::allDetectedProviders() const
{
    return report().providers;
}

AiRuntimeCompatibility AiCompatibilityManager::runtimeCompatibility() const { return report().runtime; }
AiModelCompatibility AiCompatibilityManager::modelsCompatibility() const { return report().models; }
AiPlatformCompatibility AiCompatibilityManager::platformCompatibility() const { return report().platform; }
