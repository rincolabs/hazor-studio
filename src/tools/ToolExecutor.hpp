#pragma once

#include "ToolCall.hpp"
#include <vector>

class ImageController;

class ToolExecutor {
public:
    explicit ToolExecutor(ImageController* controller = nullptr);

    void setController(ImageController* ctrl) { m_controller = ctrl; }
    ImageController* controller() const { return m_controller; }

    ToolResult execute(const ToolCall& call);
    std::vector<ToolResult> executeBatch(const std::vector<ToolCall>& calls);

private:
    ImageController* m_controller = nullptr;
};
