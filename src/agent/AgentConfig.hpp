#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <vector>

// What a preset is used for. Assistant = text/chat agent (LLMClient);
// Generative = image generation / inpaint (ImageGenProvider, src/ai/).
// Presets without an explicit kind load as Assistant (backwards compatible).
enum AgentKind { Assistant, Generative };

inline QString agentKindToString(AgentKind k) {
    return k == Generative ? QStringLiteral("generative")
                           : QStringLiteral("assistant");
}

inline AgentKind agentKindFromString(const QString& s) {
    return s == "generative" ? Generative : Assistant;
}

struct ProviderConfig {
    // Only providers with a genuinely distinct API earn a dedicated value;
    // every OpenAI-compatible service (Mistral, Groq, xAI, DeepSeek,
    // OpenRouter, Together, local SD via A1111, ...) uses Custom + baseUrl.
    enum Type { Local, OpenAI, Anthropic, Google, Custom };

    Type type = Local;
    QString baseUrl = "http://localhost:11434";
    QString apiKey = "ollama";
    QString model = "qwen2.5:3b";

    static QString typeToString(Type t) {
        switch (t) {
            case Local:     return QStringLiteral("local");
            case OpenAI:    return QStringLiteral("openai");
            case Anthropic: return QStringLiteral("anthropic");
            case Google:    return QStringLiteral("google");
            case Custom:    return QStringLiteral("custom");
        }
        return QStringLiteral("local");
    }

    static Type typeFromString(const QString& s) {
        if (s == "openai")    return OpenAI;
        if (s == "anthropic") return Anthropic;
        if (s == "google")    return Google;
        if (s == "custom")    return Custom;
        return Local;
    }

    static QString defaultBaseUrl(Type t) {
        switch (t) {
            case Local:     return QStringLiteral("http://localhost:11434");
            case OpenAI:    return QStringLiteral("https://api.openai.com/v1");
            case Anthropic: return QStringLiteral("https://api.anthropic.com");
            case Google:    return QStringLiteral("https://generativelanguage.googleapis.com");
            case Custom:    return QString();
        }
        return QStringLiteral("http://localhost:11434");
    }

    QString displayName() const {
        switch (type) {
            case Local:     return QStringLiteral("Local (Ollama / llama.cpp)");
            case OpenAI:    return QStringLiteral("OpenAI");
            case Anthropic: return QStringLiteral("Anthropic (Claude)");
            case Google:    return QStringLiteral("Google (Gemini)");
            case Custom:    return QStringLiteral("Custom (OpenAI-compatible)");
        }
        return QStringLiteral("Local");
    }

    // Chat/completions endpoint for kind==Assistant. For kind==Generative the
    // endpoint is resolved by ImageGenProviderFactory (src/ai/), so this only
    // covers the Assistant case.
    QString apiEndpoint(AgentKind kind = Assistant) const {
        QString url = baseUrl;
        while (url.endsWith('/')) url.chop(1);
        if (kind == Generative)
            return url;   // resolved by the image-gen provider, not here
        switch (type) {
            case Local:
                return url + "/api/chat";
            case OpenAI:
            case Custom:
                return url + "/chat/completions";
            case Anthropic:
                return url + "/v1/messages";
            case Google:
                return url + "/v1beta/models/" + model + ":generateContent";
        }
        return url;
    }

    QString modelsEndpoint(AgentKind kind = Assistant) const {
        QString url = baseUrl;
        while (url.endsWith('/')) url.chop(1);
        if (kind == Generative) {
            // For image generation the "models" listing differs entirely.
            switch (type) {
                case Local:
                case Custom:    return url + "/sdapi/v1/sd-models";  // A1111 / Forge
                case OpenAI:    return url + "/models";
                case Google:    return url + "/v1beta/models";
                case Anthropic: return url;                          // not used
            }
            return url;
        }
        switch (type) {
            case Local:
                return url + "/api/tags";
            case OpenAI:
            case Custom:
                return url + "/models";
            case Anthropic:
                return url + "/v1/models";
            case Google:
                return url + "/v1beta/models";
        }
        return url;
    }
};

// Parameters used only when kind==Generative (mirrors ModelSettings, which is
// only meaningful for chat providers). Feeds the InpaintRequest in src/ai/.
struct GenerativeSettings {
    int steps = 20;
    double strength = 0.75;        // denoising strength (img2img)
    int seed = -1;                 // -1 = random
    QString negativePrompt;
    bool promptAssistEnabled = false;
    QString promptAssistPreset;    // name of an Assistant preset used to refine prompts

    QJsonObject toJson() const {
        QJsonObject o;
        o["steps"] = steps;
        o["strength"] = strength;
        o["seed"] = seed;
        o["negative_prompt"] = negativePrompt;
        o["prompt_assist_enabled"] = promptAssistEnabled;
        o["prompt_assist_preset"] = promptAssistPreset;
        return o;
    }

    static GenerativeSettings fromJson(const QJsonObject& o) {
        GenerativeSettings g;
        g.steps = o.value("steps").toInt(20);
        g.strength = o.value("strength").toDouble(0.75);
        g.seed = o.value("seed").toInt(-1);
        g.negativePrompt = o.value("negative_prompt").toString();
        g.promptAssistEnabled = o.value("prompt_assist_enabled").toBool(false);
        g.promptAssistPreset = o.value("prompt_assist_preset").toString();
        return g;
    }
};

struct ModelSettings {
    double temperature = 0.2;
    int maxTokens = 4096;
    double topP = 0.9;
    double frequencyPenalty = 0.0;
    double presencePenalty = 0.0;

    QJsonObject toJson() const {
        QJsonObject o;
        o["temperature"] = temperature;
        o["max_tokens"] = maxTokens;
        o["top_p"] = topP;
        o["frequency_penalty"] = frequencyPenalty;
        o["presence_penalty"] = presencePenalty;
        return o;
    }

    static ModelSettings fromJson(const QJsonObject& o) {
        ModelSettings s;
        s.temperature = o.value("temperature").toDouble(0.2);
        s.maxTokens = o.value("max_tokens").toInt(4096);
        s.topP = o.value("top_p").toDouble(0.9);
        s.frequencyPenalty = o.value("frequency_penalty").toDouble(0.0);
        s.presencePenalty = o.value("presence_penalty").toDouble(0.0);
        return s;
    }
};

struct AgentBehavior {
    QString systemPrompt;
    QStringList enabledTools;
    enum ToolMode { Auto, Required, None };
    ToolMode toolMode = Auto;

    static QString toolModeToString(ToolMode m) {
        switch (m) {
            case Auto:     return QStringLiteral("auto");
            case Required: return QStringLiteral("required");
            case None:     return QStringLiteral("none");
        }
        return QStringLiteral("auto");
    }

    static ToolMode toolModeFromString(const QString& s) {
        if (s == "required") return Required;
        if (s == "none")     return None;
        return Auto;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["system_prompt"] = systemPrompt;
        QJsonArray arr;
        for (const auto& t : enabledTools)
            arr.append(t);
        o["tools"] = arr;
        o["tool_mode"] = toolModeToString(toolMode);
        return o;
    }

    static AgentBehavior fromJson(const QJsonObject& o) {
        AgentBehavior b;
        b.systemPrompt = o.value("system_prompt").toString();
        for (const auto& v : o.value("tools").toArray())
            b.enabledTools.append(v.toString());
        b.toolMode = toolModeFromString(o.value("tool_mode").toString("auto"));
        return b;
    }
};

struct AgentConfig {
    enum OutputMode { Text, Json, ToolCalls };

    QString name = "Image Agent";
    QString description;
    AgentKind kind = Assistant;
    ProviderConfig provider;
    ModelSettings modelSettings;
    AgentBehavior behavior;
    GenerativeSettings generative;
    OutputMode outputMode = ToolCalls;
    int timeoutMs = 30000;
    int retries = 0;
    bool stream = false;
    QString logLevel = "info";
    bool cache = false;
    int maxHistoryTurns = 10;
    bool resultFeedback = true;

    static QString outputModeToString(OutputMode m) {
        switch (m) {
            case Text:      return QStringLiteral("text");
            case Json:      return QStringLiteral("json");
            case ToolCalls: return QStringLiteral("tool_calls");
        }
        return QStringLiteral("tool_calls");
    }

    static OutputMode outputModeFromString(const QString& s) {
        if (s == "text")       return Text;
        if (s == "json")       return Json;
        if (s == "tool_calls") return ToolCalls;
        return ToolCalls;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["name"] = name;
        o["description"] = description;
        o["kind"] = agentKindToString(kind);

        QJsonObject p;
        p["type"] = ProviderConfig::typeToString(provider.type);
        p["base_url"] = provider.baseUrl;
        p["api_key"] = provider.apiKey;
        p["model"] = provider.model;
        o["provider"] = p;

        o["model_settings"] = modelSettings.toJson();
        o["agent"] = behavior.toJson();
        o["generative"] = generative.toJson();
        o["output_mode"] = outputModeToString(outputMode);

        QJsonObject adv;
        adv["timeout_ms"] = timeoutMs;
        adv["retries"] = retries;
        adv["stream"] = stream;
        adv["log_level"] = logLevel;
        adv["cache"] = cache;
        adv["max_history_turns"] = maxHistoryTurns;
        adv["result_feedback"] = resultFeedback;
        o["advanced"] = adv;

        return o;
    }

    static AgentConfig fromJson(const QJsonObject& o) {
        AgentConfig c;
        c.name = o.value("name").toString("Image Agent");
        c.description = o.value("description").toString();
        c.kind = agentKindFromString(o.value("kind").toString("assistant"));

        QJsonObject p = o.value("provider").toObject();
        c.provider.type = ProviderConfig::typeFromString(p.value("type").toString("local"));
        c.provider.baseUrl = p.value("base_url").toString(ProviderConfig::defaultBaseUrl(c.provider.type));
        c.provider.apiKey = p.value("api_key").toString("ollama");
        c.provider.model = p.value("model").toString("qwen2.5:3b");

        c.modelSettings = ModelSettings::fromJson(o.value("model_settings").toObject());
        c.behavior = AgentBehavior::fromJson(o.value("agent").toObject());
        c.generative = GenerativeSettings::fromJson(o.value("generative").toObject());
        c.outputMode = outputModeFromString(o.value("output_mode").toString("tool_calls"));

        QJsonObject adv = o.value("advanced").toObject();
        c.timeoutMs = adv.value("timeout_ms").toInt(30000);
        c.retries = adv.value("retries").toInt(0);
        c.stream = adv.value("stream").toBool(false);
        c.logLevel = adv.value("log_level").toString("info");
        c.cache = adv.value("cache").toBool(false);
        c.maxHistoryTurns = adv.value("max_history_turns").toInt(10);
        c.resultFeedback = adv.value("result_feedback").toBool(true);

        return c;
    }

    QJsonDocument toJsonDocument() const {
        return QJsonDocument(toJson());
    }

    QString toJsonString() const {
        return QString::fromUtf8(toJsonDocument().toJson(QJsonDocument::Indented));
    }
};
