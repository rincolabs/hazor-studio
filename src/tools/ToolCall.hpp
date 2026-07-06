#pragma once

#include <string>
#include <variant>
#include <unordered_map>
#include <vector>

struct ToolParam {
    std::string name;
    std::string type;
    double minVal = 0.0;
    double maxVal = 1.0;
    double defaultVal = 0.0;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::string category;
    std::vector<ToolParam> params;
};

using JsonValue = std::variant<double, std::string>;

struct ToolCall {
    std::string name;
    std::unordered_map<std::string, JsonValue> params;
};

struct ToolResult {
    bool success = false;
    std::string message;
};
