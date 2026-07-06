#include "ToolExecutor.hpp"
#include "controller/ImageController.hpp"
#include "ToolCatalog.hpp"

ToolExecutor::ToolExecutor(ImageController* controller)
    : m_controller(controller)
{
}

ToolResult ToolExecutor::execute(const ToolCall& call)
{
    ToolResult result;
    if (!m_controller) {
        result.message = "No controller available";
        return result;
    }

    result.success = m_controller->executeTool(call.name, call.params);
    result.message = result.success
        ? "Tool '" + call.name + "' executed successfully"
        : "Tool '" + call.name + "' failed or not found";
    return result;
}

std::vector<ToolResult> ToolExecutor::executeBatch(const std::vector<ToolCall>& calls)
{
    std::vector<ToolResult> results;
    results.reserve(calls.size());
    for (auto& call : calls)
        results.push_back(execute(call));
    return results;
}
