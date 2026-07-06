#pragma once

#include "ai/onnx/OnnxSessionHandle.hpp"

#include <QString>
#include <QStringList>
#include <QHash>

#include <memory>

// A loaded model as the rest of the app sees it: one or more named ONNX
// sub-sessions (e.g. SAM's "encoder" and "decoder"). Carries no ONNX types in
// its interface beyond OnnxSessionHandle, which is itself pimpl'd. Built and
// owned by AiRuntimeManager.
class AiInferenceSession {
public:
    QString modelId() const { return m_modelId; }
    QString providerUsed() const { return m_providerUsed; }

    bool hasHandle(const QString& fileType) const { return m_handles.contains(fileType); }
    std::shared_ptr<OnnxSessionHandle> handle(const QString& fileType) const
    {
        return m_handles.value(fileType);
    }
    QStringList fileTypes() const { return m_handles.keys(); }
    bool isComplete() const { return !m_handles.isEmpty(); }

private:
    friend class AiRuntimeManager;
    AiInferenceSession() = default;

    QString m_modelId;
    QString m_providerUsed;
    QHash<QString, std::shared_ptr<OnnxSessionHandle>> m_handles;
};
