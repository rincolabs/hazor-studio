#include "ImageGenProviderFactory.hpp"

#include "ImageGenProvider.hpp"
#include "A1111ImageGenProvider.hpp"
#include "OpenAIImageGenProvider.hpp"
#include "StabilityImageGenProvider.hpp"
#include "GeminiImageGenProvider.hpp"
#include "AggregatorImageGenProvider.hpp"

namespace ImageGenProviderFactory {

ImageGenProvider* create(const AgentConfig& preset, QObject* parent)
{
    if (preset.kind != Generative)
        return nullptr;

    const ProviderConfig& p = preset.provider;
    const QString url = p.baseUrl;

    switch (p.type) {
        case ProviderConfig::OpenAI:
            return new OpenAIImageGenProvider(p.baseUrl, p.apiKey, p.model, parent);

        case ProviderConfig::Google:
            return new GeminiImageGenProvider(p.baseUrl, p.apiKey, p.model, parent);

        case ProviderConfig::Anthropic:
            // Anthropic does not generate images — it only assists prompts.
            return nullptr;

        case ProviderConfig::Custom:
            // Custom shares its slot: route by base URL, default to local SD.
            if (url.contains("stability", Qt::CaseInsensitive))
                return new StabilityImageGenProvider(p.baseUrl, p.apiKey, parent);
            if (url.contains("replicate", Qt::CaseInsensitive) ||
                url.contains("fal.", Qt::CaseInsensitive))
                return new AggregatorImageGenProvider(p.baseUrl, p.apiKey, p.model, parent);
            return new A1111ImageGenProvider(p.baseUrl, p.model, parent);

        case ProviderConfig::Local:
        default:
            return new A1111ImageGenProvider(p.baseUrl, p.model, parent);
    }
}

} // namespace ImageGenProviderFactory
