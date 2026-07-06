#pragma once

#include "ai/models/AiModelDescriptor.hpp"
#include "ai/runtime/AiInferenceSession.hpp"

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

class AiSessionBundle {
public:
    QString modelId;
    QString family;

    void addSession(const QString& componentName,
                    std::shared_ptr<AiInferenceSession> session);
    std::shared_ptr<AiInferenceSession> session(const QString& componentName) const;

    QStringList componentNames() const { return m_sessions.keys(); }
    bool isComplete(const QStringList& requiredComponents) const;
    bool isEmpty() const { return m_sessions.isEmpty(); }

private:
    QHash<QString, std::shared_ptr<AiInferenceSession>> m_sessions;
};
