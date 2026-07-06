#include "AiSessionBundle.hpp"

void AiSessionBundle::addSession(const QString& componentName,
                                 std::shared_ptr<AiInferenceSession> session)
{
    if (componentName.isEmpty() || !session)
        return;
    m_sessions.insert(componentName, std::move(session));
}

std::shared_ptr<AiInferenceSession> AiSessionBundle::session(const QString& componentName) const
{
    return m_sessions.value(componentName);
}

bool AiSessionBundle::isComplete(const QStringList& requiredComponents) const
{
    for (const QString& component : requiredComponents) {
        if (!m_sessions.contains(component))
            return false;
    }
    return true;
}
