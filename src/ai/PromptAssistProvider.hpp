#pragma once

#include <QObject>
#include <QString>

#include "agent/AgentConfig.hpp"
#include "agent/LLMClient.hpp"

// Optional text layer that refines a user's raw inpaint prompt before it reaches
// the image generator. Reuses LLMClient (so any Assistant provider — including
// Anthropic — can drive it). 100% opt-in; this is how Anthropic participates in
// generative fill: it improves the prompt, it never generates pixels.
class PromptAssistProvider : public QObject {
    Q_OBJECT

public:
    explicit PromptAssistProvider(QObject* parent = nullptr);

    // Configure with an Assistant preset (the model that will refine prompts).
    void setAssistantConfig(const AgentConfig& config);

    // Refine a raw prompt. Emits refined() with the enriched prompt, or
    // failed() — callers should fall back to the raw prompt on failure.
    void refine(const QString& rawPrompt);

signals:
    void refined(const QString& enrichedPrompt);
    void failed(const QString& error);

private:
    LLMClient m_client;
};
