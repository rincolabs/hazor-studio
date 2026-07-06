#include "HttpParser.hpp"
#include <QString>
#include <QList>
#include <QTextStream>
#include <sstream>

void HttpParser::reset()
{
    m_state = State::RequestLine;
    m_request = {};
    m_buffer.clear();
    m_bodyBytesRead = 0;
    m_contentLength = 0;
    m_error.clear();
}

HttpParser::State HttpParser::feed(const QByteArray& data)
{
    m_buffer.append(data);

    if (m_state == State::RequestLine) {
        int idx = m_buffer.indexOf("\r\n");
        if (idx == -1)
            return State::RequestLine;

        QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 2);

        QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 2) {
            m_error = "Malformed request line";
            m_state = State::Error;
            return State::Error;
        }

        m_request.method = parts[0].toStdString();
        m_request.path = parts[1].toStdString();
        m_state = State::Headers;
    }

    if (m_state == State::Headers) {
        while (true) {
            int idx = m_buffer.indexOf("\r\n");
            if (idx == -1)
                return State::Headers;

            if (idx == 0) {
                m_buffer.remove(0, 2);
                m_state = State::Body;
                break;
            }

            QByteArray line = m_buffer.left(idx);
            m_buffer.remove(0, idx + 2);

            int colon = line.indexOf(':');
            if (colon != -1) {
                std::string key = line.left(colon).trimmed().toLower().toStdString();
                std::string val = line.mid(colon + 1).trimmed().toStdString();
                m_request.headers[key] = val;

                if (key == "content-length") {
                    m_contentLength = std::stoi(val);
                }
            }
        }
    }

    if (m_state == State::Body) {
        int available = m_buffer.size();
        int needed = m_contentLength - m_bodyBytesRead;

        if (available >= needed) {
            m_request.body += m_buffer.left(needed).toStdString();
            m_buffer.clear();
            m_bodyBytesRead = 0;
            m_state = State::Complete;
        } else {
            m_bodyBytesRead += available;
            m_request.body += m_buffer.toStdString();
            m_buffer.clear();
            return State::Body;
        }
    }

    return m_state;
}

QByteArray HttpParser::buildResponse(const McpResponse& resp)
{
    std::ostringstream os;
    os << "HTTP/1.1 " << resp.statusCode << " ";

    switch (resp.statusCode) {
        case 200: os << "OK"; break;
        case 400: os << "Bad Request"; break;
        case 404: os << "Not Found"; break;
        case 405: os << "Method Not Allowed"; break;
        case 422: os << "Unprocessable Entity"; break;
        case 500: os << "Internal Server Error"; break;
        default:  os << "Unknown"; break;
    }
    os << "\r\n";
    os << "Content-Type: " << resp.contentType << "\r\n";
    os << "Content-Length: " << resp.body.size() << "\r\n";
    os << "Connection: close\r\n";
    os << "\r\n";
    os << resp.body;

    return QByteArray::fromStdString(os.str());
}
