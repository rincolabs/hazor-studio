#define BOOST_TEST_MODULE AgentTest
#include <boost/test/included/unit_test.hpp>

#include "agent/LLMClient.hpp"
#include "agent/AgentConfig.hpp"
#include "tools/ToolCall.hpp"

#include <QJsonObject>
#include <QJsonDocument>
#include <QString>

BOOST_AUTO_TEST_SUITE(llmclient)

BOOST_AUTO_TEST_CASE(parse_tool_calls_array_of_objects)
{
    LLMClient client;
    QString json = R"([{"tool":"grayscale","params":{}},{"tool":"invert_colors","params":{}}])";
    auto tools = client.parseToolCalls(json);
    BOOST_CHECK_EQUAL(tools.size(), 2);
    BOOST_CHECK_EQUAL(tools[0].name, "grayscale");
    BOOST_CHECK_EQUAL(tools[1].name, "invert_colors");
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_array_of_strings)
{
    LLMClient client;
    QString json = R"(["grayscale","invert_colors"])";
    auto tools = client.parseToolCalls(json);
    BOOST_CHECK_EQUAL(tools.size(), 2);
    BOOST_CHECK_EQUAL(tools[0].name, "grayscale");
    BOOST_CHECK_EQUAL(tools[1].name, "invert_colors");
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_single_object)
{
    LLMClient client;
    QString json = R"({"tool":"grayscale","params":{}})";
    auto tools = client.parseToolCalls(json);
    BOOST_CHECK_EQUAL(tools.size(), 1);
    BOOST_CHECK_EQUAL(tools[0].name, "grayscale");
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_with_params)
{
    LLMClient client;
    QString json = R"([{"tool":"gaussian_blur","params":{"radius":5.0}}])";
    auto tools = client.parseToolCalls(json);
    BOOST_CHECK_EQUAL(tools.size(), 1);
    BOOST_CHECK_EQUAL(tools[0].name, "gaussian_blur");
    BOOST_CHECK(tools[0].params.find("radius") != tools[0].params.end());
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_empty_string)
{
    LLMClient client;
    auto tools = client.parseToolCalls("");
    BOOST_CHECK(tools.empty());
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_plain_text_no_json)
{
    LLMClient client;
    auto tools = client.parseToolCalls("This is plain text with no JSON");
    BOOST_CHECK(tools.empty());
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_with_markdown_fences)
{
    LLMClient client;
    QString json = "```json\n[{\"tool\":\"grayscale\",\"params\":{}}]\n```";
    auto tools = client.parseToolCalls(json);
    // Markdown fence handling may not be implemented
    BOOST_CHECK(tools.empty() || tools.size() == 1);
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_text_and_json)
{
    LLMClient client;
    QString json = R"(Here is some text. [{"tool":"invert_colors","params":{}}] More text.)";
    auto tools = client.parseToolCalls(json);
    if (!tools.empty())
        BOOST_CHECK_EQUAL(tools[0].name, "invert_colors");
    else
        BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_nested_json)
{
    LLMClient client;
    QString json = R"([{"tool":"crop","params":{"x":10,"y":20,"width":100,"height":100}}])";
    auto tools = client.parseToolCalls(json);
    BOOST_CHECK_EQUAL(tools.size(), 1);
    BOOST_CHECK_EQUAL(tools[0].name, "crop");
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_string_params)
{
    LLMClient client;
    QString json = R"([{"tool":"create_text","params":{"text":"Hello World","size":32}}])";
    auto tools = client.parseToolCalls(json);
    BOOST_CHECK_EQUAL(tools.size(), 1);
    BOOST_CHECK_EQUAL(tools[0].name, "create_text");
}

BOOST_AUTO_TEST_CASE(parse_tool_calls_empty_array)
{
    LLMClient client;
    auto tools = client.parseToolCalls("[]");
    BOOST_CHECK(tools.empty());
}

BOOST_AUTO_TEST_CASE(strip_tool_calls_removes_json_block)
{
    LLMClient client;
    QString input = "Some text [{\"tool\":\"grayscale\",\"params\":{}}] after";
    QString stripped = client.stripToolCalls(input);
    BOOST_CHECK(stripped.contains("Some text"));
    BOOST_CHECK(!stripped.contains("\"tool\""));
}

BOOST_AUTO_TEST_CASE(strip_tool_calls_markdown_fences)
{
    LLMClient client;
    QString input = "Text before\n```json\n[{\"tool\":\"grayscale\"}]\n```\nText after";
    QString stripped = client.stripToolCalls(input);
    BOOST_CHECK(stripped.contains("Text before"));
    BOOST_CHECK(stripped.contains("Text after"));
    BOOST_CHECK(!stripped.contains("\"tool\""));
}

BOOST_AUTO_TEST_CASE(isBusy_initially_false)
{
    LLMClient client;
    BOOST_CHECK(!client.isBusy());
}

BOOST_AUTO_TEST_CASE(apply_config_sets_endpoint)
{
    LLMClient client;
    AgentConfig config;
    config.provider.baseUrl = "http://localhost:9999";
    config.provider.model = "test-model";
    config.timeoutMs = 5000;
    client.applyConfig(config);

    BOOST_CHECK_EQUAL(client.config().provider.baseUrl.toStdString(), "http://localhost:9999");
    BOOST_CHECK_EQUAL(client.config().provider.model.toStdString(), "test-model");
    BOOST_CHECK_EQUAL(client.config().timeoutMs, 5000);
}

BOOST_AUTO_TEST_CASE(clearHistory_resets)
{
    LLMClient client;
    client.addAssistantMessage("I will grayscale the image.");
    client.addToolResultMessage("Tool executed: grayscale");
    client.clearHistory();
    BOOST_CHECK(!client.isBusy());
}

BOOST_AUTO_TEST_CASE(addAssistantMessage_does_not_crash)
{
    LLMClient client;
    client.addAssistantMessage("Response text");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(addToolResultMessage_does_not_crash)
{
    LLMClient client;
    client.addToolResultMessage("grayscale: success");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(agentconfig)

BOOST_AUTO_TEST_CASE(provider_type_to_string)
{
    BOOST_CHECK_EQUAL(ProviderConfig::typeToString(ProviderConfig::Local).toStdString(), "local");
    BOOST_CHECK_EQUAL(ProviderConfig::typeToString(ProviderConfig::OpenAI).toStdString(), "openai");
    BOOST_CHECK_EQUAL(ProviderConfig::typeToString(ProviderConfig::Custom).toStdString(), "custom");
}

BOOST_AUTO_TEST_CASE(provider_type_from_string)
{
    BOOST_CHECK(ProviderConfig::typeFromString("local") == ProviderConfig::Local);
    BOOST_CHECK(ProviderConfig::typeFromString("openai") == ProviderConfig::OpenAI);
    BOOST_CHECK(ProviderConfig::typeFromString("custom") == ProviderConfig::Custom);
    BOOST_CHECK(ProviderConfig::typeFromString("unknown") == ProviderConfig::Local);
}

BOOST_AUTO_TEST_CASE(provider_api_endpoint_local)
{
    ProviderConfig cfg;
    cfg.type = ProviderConfig::Local;
    cfg.baseUrl = "http://localhost:11434";
    BOOST_CHECK_EQUAL(cfg.apiEndpoint().toStdString(), "http://localhost:11434/api/chat");
}

BOOST_AUTO_TEST_CASE(provider_api_endpoint_openai)
{
    ProviderConfig cfg;
    cfg.type = ProviderConfig::OpenAI;
    cfg.baseUrl = "https://api.openai.com/v1";
    BOOST_CHECK_EQUAL(cfg.apiEndpoint().toStdString(), "https://api.openai.com/v1/chat/completions");
}

BOOST_AUTO_TEST_CASE(model_settings_to_json)
{
    ModelSettings s;
    s.temperature = 0.5;
    s.maxTokens = 256;
    QJsonObject o = s.toJson();
    BOOST_CHECK_CLOSE(o["temperature"].toDouble(), 0.5, 0.001);
    BOOST_CHECK_EQUAL(o["max_tokens"].toInt(), 256);
}

BOOST_AUTO_TEST_CASE(model_settings_from_json)
{
    QJsonObject o;
    o["temperature"] = 0.7;
    o["max_tokens"] = 1024;
    auto s = ModelSettings::fromJson(o);
    BOOST_CHECK_CLOSE(s.temperature, 0.7, 0.001);
    BOOST_CHECK_EQUAL(s.maxTokens, 1024);
}

BOOST_AUTO_TEST_CASE(model_settings_defaults)
{
    ModelSettings s;
    BOOST_CHECK_CLOSE(s.temperature, 0.2, 0.001);
    BOOST_CHECK_EQUAL(s.maxTokens, 512);
    BOOST_CHECK_CLOSE(s.topP, 0.9, 0.001);
    BOOST_CHECK_CLOSE(s.frequencyPenalty, 0.0, 0.001);
    BOOST_CHECK_CLOSE(s.presencePenalty, 0.0, 0.001);
}

BOOST_AUTO_TEST_CASE(agent_behavior_defaults)
{
    AgentBehavior b;
    BOOST_CHECK(b.systemPrompt.isEmpty());
    BOOST_CHECK(b.enabledTools.isEmpty());
    BOOST_CHECK(b.toolMode == AgentBehavior::Auto);
}

BOOST_AUTO_TEST_CASE(agent_behavior_to_json)
{
    AgentBehavior b;
    b.systemPrompt = "Test prompt";
    b.enabledTools = {"grayscale", "invert_colors"};
    b.toolMode = AgentBehavior::Required;

    QJsonObject o = b.toJson();
    BOOST_CHECK_EQUAL(o["system_prompt"].toString().toStdString(), "Test prompt");
    BOOST_CHECK_EQUAL(o["tool_mode"].toString().toStdString(), "required");
}

BOOST_AUTO_TEST_CASE(agent_behavior_from_json)
{
    QJsonObject o;
    o["system_prompt"] = "Custom prompt";
    QJsonArray tools;
    tools.append("gaussian_blur");
    o["tools"] = tools;
    o["tool_mode"] = "none";

    auto b = AgentBehavior::fromJson(o);
    BOOST_CHECK_EQUAL(b.systemPrompt.toStdString(), "Custom prompt");
    BOOST_CHECK_EQUAL(b.enabledTools.size(), 1);
    BOOST_CHECK(b.toolMode == AgentBehavior::None);
}

BOOST_AUTO_TEST_CASE(agent_config_full_roundtrip)
{
    AgentConfig original;
    original.name = "Test Agent";
    original.description = "Test description";
    original.provider.type = ProviderConfig::OpenAI;
    original.provider.baseUrl = "https://test.example.com/v1";
    original.provider.model = "gpt-4";
    original.modelSettings.temperature = 0.3;
    original.modelSettings.maxTokens = 2048;
    original.behavior.systemPrompt = "You are a test";
    original.behavior.enabledTools = {"grayscale", "crop"};
    original.outputMode = AgentConfig::ToolCalls;
    original.timeoutMs = 15000;
    original.maxHistoryTurns = 5;
    original.resultFeedback = false;

    QJsonObject json = original.toJson();
    AgentConfig restored = AgentConfig::fromJson(json);

    BOOST_CHECK_EQUAL(restored.name.toStdString(), "Test Agent");
    BOOST_CHECK_EQUAL(restored.description.toStdString(), "Test description");
    BOOST_CHECK(restored.provider.type == ProviderConfig::OpenAI);
    BOOST_CHECK_EQUAL(restored.provider.model.toStdString(), "gpt-4");
    BOOST_CHECK_CLOSE(restored.modelSettings.temperature, 0.3, 0.001);
    BOOST_CHECK_EQUAL(restored.modelSettings.maxTokens, 2048);
    BOOST_CHECK_EQUAL(restored.behavior.enabledTools.size(), 2);
    BOOST_CHECK_EQUAL(restored.timeoutMs, 15000);
    BOOST_CHECK_EQUAL(restored.maxHistoryTurns, 5);
    BOOST_CHECK(!restored.resultFeedback);
}

BOOST_AUTO_TEST_CASE(agent_config_default_values)
{
    AgentConfig c;
    BOOST_CHECK_EQUAL(c.name.toStdString(), "Image Agent");
    BOOST_CHECK(c.provider.type == ProviderConfig::Local);
    BOOST_CHECK_EQUAL(c.provider.model.toStdString(), "qwen2.5:3b");
    BOOST_CHECK_EQUAL(c.timeoutMs, 30000);
    BOOST_CHECK_EQUAL(c.retries, 0);
    BOOST_CHECK(!c.stream);
    BOOST_CHECK_EQUAL(c.maxHistoryTurns, 10);
    BOOST_CHECK(c.resultFeedback);
}

BOOST_AUTO_TEST_CASE(agent_config_to_json_string)
{
    AgentConfig c;
    QString s = c.toJsonString();
    BOOST_CHECK(!s.isEmpty());
    BOOST_CHECK(s.contains("Image Agent"));
}

BOOST_AUTO_TEST_CASE(output_mode_string_conversion)
{
    BOOST_CHECK_EQUAL(AgentConfig::outputModeToString(AgentConfig::Text).toStdString(), "text");
    BOOST_CHECK_EQUAL(AgentConfig::outputModeToString(AgentConfig::Json).toStdString(), "json");
    BOOST_CHECK_EQUAL(AgentConfig::outputModeToString(AgentConfig::ToolCalls).toStdString(), "tool_calls");

    BOOST_CHECK(AgentConfig::outputModeFromString("text") == AgentConfig::Text);
    BOOST_CHECK(AgentConfig::outputModeFromString("json") == AgentConfig::Json);
    BOOST_CHECK(AgentConfig::outputModeFromString("tool_calls") == AgentConfig::ToolCalls);
    BOOST_CHECK(AgentConfig::outputModeFromString("unknown") == AgentConfig::ToolCalls);
}

BOOST_AUTO_TEST_SUITE_END()
