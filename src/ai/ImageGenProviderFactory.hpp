#pragma once

#include "agent/AgentConfig.hpp"

class ImageGenProvider;
class QObject;

// Builds an ImageGenProvider from a Generative preset (AgentConfig). The
// provider subclass is chosen from provider.type, with baseUrl heuristics for
// the providers that share the Custom slot (Stability / Replicate):
//   Local            -> A1111 (local Stable Diffusion)
//   OpenAI           -> OpenAI Images
//   Google           -> Gemini Image
//   Custom           -> A1111, unless the baseUrl points at Stability or an
//                       aggregator (Replicate / fal), in which case that
//                       provider is used instead.
namespace ImageGenProviderFactory {

// Returns a heap-allocated provider parented to `parent` (nullptr on failure,
// e.g. a non-Generative preset). Caller owns it via the parent.
ImageGenProvider* create(const AgentConfig& preset, QObject* parent = nullptr);

} // namespace ImageGenProviderFactory
