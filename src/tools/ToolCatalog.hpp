#pragma once

#include "ToolCall.hpp"
#include <vector>
#include <string>

class ToolCatalog {
public:
    static const std::vector<ToolDefinition>& allTools();
    static const ToolDefinition* findTool(const std::string& name);
    static std::string toJsonSchema();
    static std::string systemPrompt();
    static std::string systemPrompt(const std::vector<std::string>& enabledTools);

private:
    static std::vector<ToolDefinition> buildCatalog();
    static std::vector<ToolDefinition> s_catalog;
};
