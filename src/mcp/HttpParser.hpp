#pragma once

#include "McpTypes.hpp"
#include <QByteArray>
#include <optional>

class HttpParser {
public:
    enum class State {
        RequestLine,
        Headers,
        Body,
        Complete,
        Error
    };

    void reset();
    State feed(const QByteArray& data);
    State state() const { return m_state; }
    McpRequest request() const { return m_request; }
    std::string errorMessage() const { return m_error; }

    static QByteArray buildResponse(const McpResponse& resp);

private:
    State m_state = State::RequestLine;
    McpRequest m_request;
    QByteArray m_buffer;
    int m_bodyBytesRead = 0;
    int m_contentLength = 0;
    std::string m_error;
};
