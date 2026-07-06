#define BOOST_TEST_MODULE HttpParserTest
#include <ostream>
#include <QByteArray>
inline std::ostream& operator<<(std::ostream& os, const QByteArray&) { return os; }
#include <boost/test/included/unit_test.hpp>

#include "mcp/HttpParser.hpp"
#include "mcp/McpTypes.hpp"
#include <QString>
#include <string>

inline std::ostream& operator<<(std::ostream& os, HttpParser::State s) {
    return os << static_cast<int>(s);
}

BOOST_AUTO_TEST_SUITE(httpparser)

BOOST_AUTO_TEST_CASE(parse_simple_get)
{
    HttpParser parser;
    QByteArray data = "GET /mcp/tools HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto state = parser.feed(data);
    BOOST_CHECK_EQUAL(state, HttpParser::State::Complete);

    auto req = parser.request();
    BOOST_CHECK_EQUAL(req.method, "GET");
    BOOST_CHECK_EQUAL(req.path, "/mcp/tools");
    BOOST_CHECK_EQUAL(req.headers["host"], "localhost");
}

BOOST_AUTO_TEST_CASE(parse_post_with_body)
{
    HttpParser parser;
    QByteArray body = "{\"tool\":\"grayscale\",\"params\":{}}";
    QByteArray data = QString(
        "POST /mcp/execute HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %1\r\n"
        "\r\n"
        "%2"
    ).arg(body.size()).arg(QString::fromUtf8(body)).toUtf8();

    auto state = parser.feed(data);
    BOOST_CHECK_EQUAL(state, HttpParser::State::Complete);

    auto req = parser.request();
    BOOST_CHECK_EQUAL(req.method, "POST");
    BOOST_CHECK_EQUAL(req.path, "/mcp/execute");
    BOOST_CHECK_EQUAL(req.body, body.toStdString());
}

BOOST_AUTO_TEST_CASE(parse_fragmented_body)
{
    HttpParser parser;
    QByteArray body = "{\"tool\":\"test\",\"params\":{\"x\":1}}";

    QByteArray headerPart = QString(
        "POST /test HTTP/1.1\r\n"
        "Content-Length: %1\r\n"
        "\r\n"
    ).arg(body.size()).toUtf8();

    auto state = parser.feed(headerPart);
    BOOST_CHECK_EQUAL(state, HttpParser::State::Body);

    state = parser.feed(body.left(10));
    BOOST_CHECK_EQUAL(state, HttpParser::State::Body);

    state = parser.feed(body.mid(10));
    BOOST_CHECK_EQUAL(state, HttpParser::State::Complete);

    BOOST_CHECK_EQUAL(parser.request().body, body.toStdString());
}

BOOST_AUTO_TEST_CASE(parse_malformed_request_line)
{
    HttpParser parser;
    auto state = parser.feed("INVALID\r\n");
    BOOST_CHECK_EQUAL(state, HttpParser::State::Error);
}

BOOST_AUTO_TEST_CASE(build_response)
{
    McpResponse resp;
    resp.statusCode = 200;
    resp.body = "{\"success\":true}";

    QByteArray raw = HttpParser::buildResponse(resp);
    std::string r = raw.toStdString();
    BOOST_CHECK(r.find("HTTP/1.1 200 OK") != std::string::npos);
    BOOST_CHECK(r.find("Content-Type: application/json") != std::string::npos);
    BOOST_CHECK(r.find("Connection: close") != std::string::npos);
    BOOST_CHECK(r.find("{\"success\":true}") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(build_error_response)
{
    McpResponse resp;
    resp.statusCode = 404;
    resp.body = "{\"error\":\"not found\"}";

    QByteArray raw = HttpParser::buildResponse(resp);
    std::string r = raw.toStdString();
    BOOST_CHECK(r.find("HTTP/1.1 404 Not Found") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(reset_parser)
{
    HttpParser parser;
    parser.feed("GET /a HTTP/1.1\r\n\r\n");
    BOOST_CHECK_EQUAL(parser.state(), HttpParser::State::Complete);

    parser.reset();
    BOOST_CHECK_EQUAL(parser.state(), HttpParser::State::RequestLine);
    BOOST_CHECK(parser.request().body.empty());
}

BOOST_AUTO_TEST_CASE(content_type_header)
{
    HttpParser parser;
    QByteArray data = "POST /x HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n";
    parser.feed(data);
    BOOST_CHECK_EQUAL(parser.request().headers["content-type"], "text/plain");
}

BOOST_AUTO_TEST_SUITE_END()
