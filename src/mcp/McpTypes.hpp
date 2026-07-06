#pragma once

#include <string>
#include <unordered_map>

struct McpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct McpResponse {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
};
