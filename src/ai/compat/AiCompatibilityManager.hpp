#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QObject>
#include <QList>

#include <optional>

// Central authority for AI compatibility. Aggregates the platform / runtime /
// provider / CUDA-cuDNN / model checkers into a single AiCompatibilityReport and
// is the only thing the Settings dialog and the AI Object Selection tool consult
// (they never run their own platform/CUDA checks — spec §3).
//
// The report is cached because building it can spawn nvidia-smi / ldconfig;
// refresh() invalidates it and is wired to runtime/model change signals.
class AiCompatibilityManager : public QObject {
    Q_OBJECT
public:
    static AiCompatibilityManager* instance();

    AiCompatibilityReport buildReport() const;

    QList<AiProviderCompatibility> compatibleProviders() const; // isSelectable() only
    QList<AiProviderCompatibility> allDetectedProviders() const;

    AiRuntimeCompatibility runtimeCompatibility() const;
    AiModelCompatibility modelsCompatibility() const;
    AiPlatformCompatibility platformCompatibility() const;

public slots:
    // Drops the cached report; the next buildReport() recomputes (and re-probes).
    void refresh();

signals:
    void compatibilityChanged();

private:
    explicit AiCompatibilityManager(QObject* parent = nullptr);

    const AiCompatibilityReport& report() const;
    AiCompatibilityReport computeReport() const;

    mutable std::optional<AiCompatibilityReport> m_cache;
};
