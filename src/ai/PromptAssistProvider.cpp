#include "PromptAssistProvider.hpp"

PromptAssistProvider::PromptAssistProvider(QObject* parent)
    : QObject(parent)
{
    connect(&m_client, &LLMClient::textResponse, this, [this](const QString& text) {
        emit refined(text.trimmed());
    });
    connect(&m_client, &LLMClient::errorOccurred, this, [this](const QString& err) {
        emit failed(err);
    });
}

void PromptAssistProvider::setAssistantConfig(const AgentConfig& config)
{
    // Force Assistant so applyConfig (which rejects Generative) accepts it.
    AgentConfig c = config;
    c.kind = Assistant;
    m_client.applyConfig(c);
}

void PromptAssistProvider::refine(const QString& rawPrompt)
{
    const QString system =
        "You refine image-generation prompts. Given a short user request, reply "
        "with ONE improved, vivid prompt for an inpainting model. Output only the "
        "prompt text, no quotes, no preamble, no tool calls.";
    m_client.sendAsync(system, rawPrompt);
}
