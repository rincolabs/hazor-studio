#pragma once

#include <QString>
#include <QLatin1String>

// A classified runtime error, built from the provider that was active and the
// raw message ONNX Runtime reported. Used to decide whether a failed Run() should
// trigger the automatic CUDA→CPU fallback (spec §19). Header-only: it carries no
// ONNX types and is safe to include anywhere.
struct AiRuntimeError {
    QString provider;   // provider id active when the error happened ("cuda"…)
    QString message;    // raw error text from the runtime/session

    static AiRuntimeError fromMessage(const QString& provider, const QString& message)
    {
        return AiRuntimeError{ provider, message };
    }

    // Known out-of-memory signatures across CUDA / cuDNN / the BFC arena.
    bool isOutOfMemory() const
    {
        const QString m = message.toLower();
        return m.contains(QLatin1String("failed to allocate memory"))
            || m.contains(QLatin1String("out of memory"))
            || m.contains(QLatin1String("cuda out of memory"))
            || m.contains(QLatin1String("bfc_arena"))
            || m.contains(QLatin1String("allocaterawinternal"))
            || m.contains(QLatin1String("cuda_error_out_of_memory"))
            || m.contains(QLatin1String("cudnn_status_alloc_failed"));
    }

    // True when the failure is attributable to the CUDA stack.
    bool isCudaProviderError() const
    {
        const QString p = provider.toLower();
        const QString m = message.toLower();
        return p == QLatin1String("cuda")
            || m.contains(QLatin1String("cuda"))
            || m.contains(QLatin1String("cudnn"))
            || m.contains(QLatin1String("cublas"));
    }
};
