#include "SvgShapeConverter.hpp"

#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QPainterPath>
#include <QRegularExpression>
#include <QVector>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kCircleKappa = 0.5522847498307936;

QMutex& conversionCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, SvgShapeConversionResult>& conversionCache()
{
    static QHash<QString, SvgShapeConversionResult> cache;
    return cache;
}

PathCommand makeMoveTo(const QPointF& p)
{
    PathCommand c;
    c.type = PathCommandType::MoveTo;
    c.p1 = p;
    return c;
}

PathCommand makeLineTo(const QPointF& p)
{
    PathCommand c;
    c.type = PathCommandType::LineTo;
    c.p1 = p;
    return c;
}

PathCommand makeQuadTo(const QPointF& c1, const QPointF& end)
{
    PathCommand c;
    c.type = PathCommandType::QuadTo;
    c.p1 = c1;
    c.p2 = end;
    return c;
}

PathCommand makeCubicTo(const QPointF& c1, const QPointF& c2, const QPointF& end)
{
    PathCommand c;
    c.type = PathCommandType::CubicTo;
    c.p1 = c1;
    c.p2 = c2;
    c.p3 = end;
    return c;
}

PathCommand makeClosePath()
{
    PathCommand c;
    c.type = PathCommandType::ClosePath;
    return c;
}

QPainterPath painterPathFor(const VectorPath& vectorPath)
{
    QPainterPath path;
    path.setFillRule(vectorPath.fillRule);
    for (const auto& command : vectorPath.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
            path.moveTo(command.p1);
            break;
        case PathCommandType::LineTo:
            path.lineTo(command.p1);
            break;
        case PathCommandType::QuadTo:
            path.quadTo(command.p1, command.p2);
            break;
        case PathCommandType::CubicTo:
            path.cubicTo(command.p1, command.p2, command.p3);
            break;
        case PathCommandType::ClosePath:
            path.closeSubpath();
            break;
        }
    }
    return path;
}

QPointF flipPointY(const QPointF& point, const QRectF& bounds)
{
    return QPointF(point.x(), bounds.top() + bounds.bottom() - point.y());
}

VectorPath flipPathY(VectorPath path, const QRectF& bounds)
{
    for (auto& command : path.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
        case PathCommandType::LineTo:
            command.p1 = flipPointY(command.p1, bounds);
            break;
        case PathCommandType::QuadTo:
            command.p1 = flipPointY(command.p1, bounds);
            command.p2 = flipPointY(command.p2, bounds);
            break;
        case PathCommandType::CubicTo:
            command.p1 = flipPointY(command.p1, bounds);
            command.p2 = flipPointY(command.p2, bounds);
            command.p3 = flipPointY(command.p3, bounds);
            break;
        case PathCommandType::ClosePath:
            break;
        }
    }
    path.localBounds = bounds;
    return path;
}

double attrDouble(const QXmlStreamAttributes& attrs,
                  const QString& name,
                  double fallback = 0.0)
{
    const QString value = attrs.value(name).toString().trimmed();
    if (value.isEmpty())
        return fallback;
    const auto match = QRegularExpression(QStringLiteral("^([+-]?(?:\\d+\\.?\\d*|\\.\\d+))"))
        .match(value);
    return match.hasMatch() ? match.captured(1).toDouble() : fallback;
}

QString attrString(const QXmlStreamAttributes& attrs, const QString& name)
{
    return attrs.value(name).toString().trimmed();
}

struct SvgStyle {
    Qt::FillRule fillRule = Qt::WindingFill;
};

struct SvgParseContext {
    VectorPath path;
    QRectF viewBox;
    bool hasViewBox = false;
    bool hasClosedPath = false;
    bool hasOpenPath = false;
    QString error;
};

class NumberScanner {
public:
    explicit NumberScanner(QString text)
        : m_text(std::move(text))
    {
    }

    bool hasMore()
    {
        skipSpace();
        return m_pos < m_text.size();
    }

    bool nextIsCommand()
    {
        skipSpace();
        if (m_pos >= m_text.size())
            return false;
        const QChar ch = m_text[m_pos];
        return ch.isLetter();
    }

    QChar readCommand()
    {
        skipSpace();
        return m_pos < m_text.size() ? m_text[m_pos++] : QChar();
    }

    bool hasNumber()
    {
        skipSpace();
        if (m_pos >= m_text.size())
            return false;
        const QChar ch = m_text[m_pos];
        return ch.isDigit() || ch == QLatin1Char('+') || ch == QLatin1Char('-') || ch == QLatin1Char('.');
    }

    bool readNumber(double& out)
    {
        skipSpace();
        const int start = m_pos;
        if (m_pos < m_text.size()
            && (m_text[m_pos] == QLatin1Char('+') || m_text[m_pos] == QLatin1Char('-'))) {
            ++m_pos;
        }

        bool sawDigit = false;
        while (m_pos < m_text.size() && m_text[m_pos].isDigit()) {
            sawDigit = true;
            ++m_pos;
        }

        if (m_pos < m_text.size() && m_text[m_pos] == QLatin1Char('.')) {
            ++m_pos;
            while (m_pos < m_text.size() && m_text[m_pos].isDigit()) {
                sawDigit = true;
                ++m_pos;
            }
        }

        if (!sawDigit) {
            m_pos = start;
            return false;
        }

        if (m_pos < m_text.size()
            && (m_text[m_pos] == QLatin1Char('e') || m_text[m_pos] == QLatin1Char('E'))) {
            const int expStart = m_pos++;
            if (m_pos < m_text.size()
                && (m_text[m_pos] == QLatin1Char('+') || m_text[m_pos] == QLatin1Char('-'))) {
                ++m_pos;
            }
            bool expDigit = false;
            while (m_pos < m_text.size() && m_text[m_pos].isDigit()) {
                expDigit = true;
                ++m_pos;
            }
            if (!expDigit)
                m_pos = expStart;
        }

        bool ok = false;
        out = m_text.mid(start, m_pos - start).toDouble(&ok);
        if (!ok) {
            m_pos = start;
            return false;
        }
        skipComma();
        return true;
    }

    bool readPoint(QPointF& out)
    {
        double x = 0.0;
        double y = 0.0;
        if (!readNumber(x) || !readNumber(y))
            return false;
        out = QPointF(x, y);
        return true;
    }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size()) {
            const QChar ch = m_text[m_pos];
            if (!ch.isSpace() && ch != QLatin1Char(','))
                break;
            ++m_pos;
        }
    }

    void skipComma()
    {
        while (m_pos < m_text.size()) {
            const QChar ch = m_text[m_pos];
            if (ch.isSpace() || ch == QLatin1Char(','))
                ++m_pos;
            else
                break;
        }
    }

    QString m_text;
    int m_pos = 0;
};

void appendMove(VectorPath& path, const QTransform& xf, const QPointF& p)
{
    path.commands.push_back(makeMoveTo(xf.map(p)));
}

void appendLine(VectorPath& path, const QTransform& xf, const QPointF& p)
{
    path.commands.push_back(makeLineTo(xf.map(p)));
}

void appendQuad(VectorPath& path, const QTransform& xf, const QPointF& c, const QPointF& end)
{
    path.commands.push_back(makeQuadTo(xf.map(c), xf.map(end)));
}

void appendCubic(VectorPath& path,
                 const QTransform& xf,
                 const QPointF& c1,
                 const QPointF& c2,
                 const QPointF& end)
{
    path.commands.push_back(makeCubicTo(xf.map(c1), xf.map(c2), xf.map(end)));
}

void appendClose(SvgParseContext& ctx)
{
    ctx.path.commands.push_back(makeClosePath());
    ctx.hasClosedPath = true;
}

bool parsePathData(const QString& data,
                   const QTransform& xf,
                   SvgParseContext& ctx)
{
    NumberScanner scanner(data);
    QChar command;
    QPointF current;
    QPointF subPathStart;
    QPointF lastCubicControl;
    QPointF lastQuadControl;
    bool hasLastCubic = false;
    bool hasLastQuad = false;

    auto makeAbsolute = [&](const QPointF& p, bool relative) {
        return relative ? current + p : p;
    };
    auto hasInvalidTrailingData = [&]() {
        if (scanner.hasMore() && !scanner.nextIsCommand()) {
            ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
            return true;
        }
        return false;
    };

    while (scanner.hasMore()) {
        if (scanner.nextIsCommand())
            command = scanner.readCommand();
        if (command.isNull()) {
            ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
            return false;
        }

        const bool relative = command.isLower();
        const QChar op = command.toUpper();

        if (op == QLatin1Char('A')) {
            ctx.error = QStringLiteral("This SVG contains unsupported features.");
            return false;
        }

        if (op == QLatin1Char('M')) {
            QPointF p;
            if (!scanner.readPoint(p)) {
                ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                return false;
            }
            current = makeAbsolute(p, relative);
            subPathStart = current;
            appendMove(ctx.path, xf, current);
            hasLastCubic = false;
            hasLastQuad = false;

            while (scanner.hasNumber()) {
                if (!scanner.readPoint(p)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                current = makeAbsolute(p, relative);
                appendLine(ctx.path, xf, current);
            }
            if (hasInvalidTrailingData())
                return false;
            command = relative ? QLatin1Char('l') : QLatin1Char('L');
            continue;
        }

        if (op == QLatin1Char('Z')) {
            appendClose(ctx);
            current = subPathStart;
            hasLastCubic = false;
            hasLastQuad = false;
            command = QChar();
            continue;
        }

        if (op == QLatin1Char('L')) {
            QPointF p;
            while (scanner.hasNumber()) {
                if (!scanner.readPoint(p)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                current = makeAbsolute(p, relative);
                appendLine(ctx.path, xf, current);
            }
            if (hasInvalidTrailingData())
                return false;
            hasLastCubic = false;
            hasLastQuad = false;
            continue;
        }

        if (op == QLatin1Char('H')) {
            double x = 0.0;
            while (scanner.hasNumber()) {
                if (!scanner.readNumber(x)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                current.setX(relative ? current.x() + x : x);
                appendLine(ctx.path, xf, current);
            }
            if (hasInvalidTrailingData())
                return false;
            hasLastCubic = false;
            hasLastQuad = false;
            continue;
        }

        if (op == QLatin1Char('V')) {
            double y = 0.0;
            while (scanner.hasNumber()) {
                if (!scanner.readNumber(y)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                current.setY(relative ? current.y() + y : y);
                appendLine(ctx.path, xf, current);
            }
            if (hasInvalidTrailingData())
                return false;
            hasLastCubic = false;
            hasLastQuad = false;
            continue;
        }

        if (op == QLatin1Char('C')) {
            QPointF c1;
            QPointF c2;
            QPointF end;
            while (scanner.hasNumber()) {
                if (!scanner.readPoint(c1) || !scanner.readPoint(c2) || !scanner.readPoint(end)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                c1 = makeAbsolute(c1, relative);
                c2 = makeAbsolute(c2, relative);
                end = makeAbsolute(end, relative);
                appendCubic(ctx.path, xf, c1, c2, end);
                current = end;
                lastCubicControl = c2;
                hasLastCubic = true;
                hasLastQuad = false;
            }
            if (hasInvalidTrailingData())
                return false;
            continue;
        }

        if (op == QLatin1Char('S')) {
            QPointF c2;
            QPointF end;
            while (scanner.hasNumber()) {
                if (!scanner.readPoint(c2) || !scanner.readPoint(end)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                const QPointF c1 = hasLastCubic ? current + (current - lastCubicControl) : current;
                c2 = makeAbsolute(c2, relative);
                end = makeAbsolute(end, relative);
                appendCubic(ctx.path, xf, c1, c2, end);
                current = end;
                lastCubicControl = c2;
                hasLastCubic = true;
                hasLastQuad = false;
            }
            if (hasInvalidTrailingData())
                return false;
            continue;
        }

        if (op == QLatin1Char('Q')) {
            QPointF c;
            QPointF end;
            while (scanner.hasNumber()) {
                if (!scanner.readPoint(c) || !scanner.readPoint(end)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                c = makeAbsolute(c, relative);
                end = makeAbsolute(end, relative);
                appendQuad(ctx.path, xf, c, end);
                current = end;
                lastQuadControl = c;
                hasLastQuad = true;
                hasLastCubic = false;
            }
            if (hasInvalidTrailingData())
                return false;
            continue;
        }

        if (op == QLatin1Char('T')) {
            QPointF end;
            while (scanner.hasNumber()) {
                if (!scanner.readPoint(end)) {
                    ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
                    return false;
                }
                const QPointF c = hasLastQuad ? current + (current - lastQuadControl) : current;
                end = makeAbsolute(end, relative);
                appendQuad(ctx.path, xf, c, end);
                current = end;
                lastQuadControl = c;
                hasLastQuad = true;
                hasLastCubic = false;
            }
            if (hasInvalidTrailingData())
                return false;
            continue;
        }

        ctx.error = QStringLiteral("This SVG contains unsupported features.");
        return false;
    }

    return true;
}

QVector<QPointF> parsePoints(const QString& points)
{
    QVector<QPointF> out;
    NumberScanner scanner(points);
    while (scanner.hasMore()) {
        QPointF p;
        if (!scanner.readPoint(p))
            return {};
        out.push_back(p);
    }
    return out;
}

QRectF parseViewBox(const QString& value)
{
    NumberScanner scanner(value);
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    if (!scanner.readNumber(x) || !scanner.readNumber(y)
        || !scanner.readNumber(w) || !scanner.readNumber(h)) {
        return {};
    }
    return QRectF(x, y, w, h);
}

QTransform parseTransform(const QString& value)
{
    QTransform result;
    int pos = 0;
    const QRegularExpression itemRe(QStringLiteral("([A-Za-z]+)\\s*\\(([^)]*)\\)"));
    auto match = itemRe.match(value, pos);
    while (match.hasMatch()) {
        const QString name = match.captured(1).toLower();
        NumberScanner scanner(match.captured(2));
        QVector<double> args;
        while (scanner.hasMore()) {
            double n = 0.0;
            if (!scanner.readNumber(n))
                break;
            args.push_back(n);
        }

        QTransform op;
        if (name == QLatin1String("matrix") && args.size() >= 6) {
            op.setMatrix(args[0], args[1], 0.0,
                         args[2], args[3], 0.0,
                         args[4], args[5], 1.0);
        } else if (name == QLatin1String("translate") && !args.isEmpty()) {
            op.translate(args[0], args.size() >= 2 ? args[1] : 0.0);
        } else if (name == QLatin1String("scale") && !args.isEmpty()) {
            op.scale(args[0], args.size() >= 2 ? args[1] : args[0]);
        } else if (name == QLatin1String("rotate") && !args.isEmpty()) {
            if (args.size() >= 3) {
                op.translate(args[1], args[2]);
                op.rotate(args[0]);
                op.translate(-args[1], -args[2]);
            } else {
                op.rotate(args[0]);
            }
        }

        result = result * op;
        pos = match.capturedEnd();
        match = itemRe.match(value, pos);
    }
    return result;
}

SvgStyle styleFor(const QXmlStreamAttributes& attrs, SvgStyle parent)
{
    auto apply = [&parent](const QString& key, const QString& value) {
        const QString normalizedKey = key.trimmed().toLower();
        const QString normalizedValue = value.trimmed().toLower();
        if (normalizedKey == QLatin1String("fill-rule")) {
            parent.fillRule = normalizedValue == QLatin1String("evenodd")
                ? Qt::OddEvenFill
                : Qt::WindingFill;
        }
    };

    for (const auto& attr : attrs)
        apply(attr.name().toString(), attr.value().toString());

    const QString style = attrs.value(QStringLiteral("style")).toString();
    const QStringList declarations = style.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& declaration : declarations) {
        const int colon = declaration.indexOf(QLatin1Char(':'));
        if (colon > 0)
            apply(declaration.left(colon), declaration.mid(colon + 1));
    }
    return parent;
}

bool hasUnsupportedPaintServer(const QXmlStreamAttributes& attrs)
{
    const QString fill = attrs.value(QStringLiteral("fill")).toString();
    const QString stroke = attrs.value(QStringLiteral("stroke")).toString();
    const QString style = attrs.value(QStringLiteral("style")).toString();
    return fill.contains(QLatin1String("url("), Qt::CaseInsensitive)
        || stroke.contains(QLatin1String("url("), Qt::CaseInsensitive)
        || style.contains(QLatin1String("url("), Qt::CaseInsensitive);
}

void appendRect(SvgParseContext& ctx, const QTransform& xf, QRectF r, double rx, double ry)
{
    if (r.width() <= 0.0 || r.height() <= 0.0)
        return;
    rx = std::clamp(rx, 0.0, r.width() * 0.5);
    ry = std::clamp(ry, 0.0, r.height() * 0.5);
    if (rx <= 0.0 || ry <= 0.0) {
        appendMove(ctx.path, xf, r.topLeft());
        appendLine(ctx.path, xf, r.topRight());
        appendLine(ctx.path, xf, r.bottomRight());
        appendLine(ctx.path, xf, r.bottomLeft());
        appendClose(ctx);
        return;
    }

    const double ox = rx * kCircleKappa;
    const double oy = ry * kCircleKappa;
    appendMove(ctx.path, xf, QPointF(r.left() + rx, r.top()));
    appendLine(ctx.path, xf, QPointF(r.right() - rx, r.top()));
    appendCubic(ctx.path, xf,
                QPointF(r.right() - rx + ox, r.top()),
                QPointF(r.right(), r.top() + ry - oy),
                QPointF(r.right(), r.top() + ry));
    appendLine(ctx.path, xf, QPointF(r.right(), r.bottom() - ry));
    appendCubic(ctx.path, xf,
                QPointF(r.right(), r.bottom() - ry + oy),
                QPointF(r.right() - rx + ox, r.bottom()),
                QPointF(r.right() - rx, r.bottom()));
    appendLine(ctx.path, xf, QPointF(r.left() + rx, r.bottom()));
    appendCubic(ctx.path, xf,
                QPointF(r.left() + rx - ox, r.bottom()),
                QPointF(r.left(), r.bottom() - ry + oy),
                QPointF(r.left(), r.bottom() - ry));
    appendLine(ctx.path, xf, QPointF(r.left(), r.top() + ry));
    appendCubic(ctx.path, xf,
                QPointF(r.left(), r.top() + ry - oy),
                QPointF(r.left() + rx - ox, r.top()),
                QPointF(r.left() + rx, r.top()));
    appendClose(ctx);
}

void appendEllipse(SvgParseContext& ctx,
                   const QTransform& xf,
                   const QPointF& center,
                   double rx,
                   double ry)
{
    if (rx <= 0.0 || ry <= 0.0)
        return;
    const double ox = rx * kCircleKappa;
    const double oy = ry * kCircleKappa;
    appendMove(ctx.path, xf, QPointF(center.x(), center.y() - ry));
    appendCubic(ctx.path, xf,
                QPointF(center.x() + ox, center.y() - ry),
                QPointF(center.x() + rx, center.y() - oy),
                QPointF(center.x() + rx, center.y()));
    appendCubic(ctx.path, xf,
                QPointF(center.x() + rx, center.y() + oy),
                QPointF(center.x() + ox, center.y() + ry),
                QPointF(center.x(), center.y() + ry));
    appendCubic(ctx.path, xf,
                QPointF(center.x() - ox, center.y() + ry),
                QPointF(center.x() - rx, center.y() + oy),
                QPointF(center.x() - rx, center.y()));
    appendCubic(ctx.path, xf,
                QPointF(center.x() - rx, center.y() - oy),
                QPointF(center.x() - ox, center.y() - ry),
                QPointF(center.x(), center.y() - ry));
    appendClose(ctx);
}

void parseElement(QXmlStreamReader& xml,
                  SvgParseContext& ctx,
                  const QTransform& parentTransform,
                  const SvgStyle& parentStyle)
{
    if (!ctx.error.isEmpty())
        return;

    const QString name = xml.name().toString();
    const QXmlStreamAttributes attrs = xml.attributes();
    SvgStyle style = styleFor(attrs, parentStyle);
    QTransform transform = parentTransform * parseTransform(attrString(attrs, QStringLiteral("transform")));

    if (hasUnsupportedPaintServer(attrs)) {
        ctx.error = QStringLiteral("This SVG contains unsupported features.");
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("svg")) {
        const QRectF viewBox = parseViewBox(attrString(attrs, QStringLiteral("viewBox")));
        if (viewBox.isValid() && viewBox.width() > 0.0 && viewBox.height() > 0.0) {
            ctx.viewBox = viewBox;
            ctx.hasViewBox = true;
        } else {
            const double w = attrDouble(attrs, QStringLiteral("width"));
            const double h = attrDouble(attrs, QStringLiteral("height"));
            if (w > 0.0 && h > 0.0) {
                ctx.viewBox = QRectF(0.0, 0.0, w, h);
                ctx.hasViewBox = true;
            }
        }

        while (xml.readNextStartElement())
            parseElement(xml, ctx, transform, style);
        return;
    }

    if (name == QLatin1String("g")) {
        while (xml.readNextStartElement())
            parseElement(xml, ctx, transform, style);
        return;
    }

    ctx.path.fillRule = style.fillRule;

    if (name == QLatin1String("path")) {
        const QString d = attrString(attrs, QStringLiteral("d"));
        if (d.isEmpty()) {
            ctx.error = QStringLiteral("This SVG does not contain a valid vector path.");
        } else {
            parsePathData(d, transform, ctx);
        }
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("rect")) {
        const double x = attrDouble(attrs, QStringLiteral("x"));
        const double y = attrDouble(attrs, QStringLiteral("y"));
        const double w = attrDouble(attrs, QStringLiteral("width"));
        const double h = attrDouble(attrs, QStringLiteral("height"));
        double rx = attrDouble(attrs, QStringLiteral("rx"));
        double ry = attrDouble(attrs, QStringLiteral("ry"));
        if (rx <= 0.0) rx = ry;
        if (ry <= 0.0) ry = rx;
        appendRect(ctx, transform, QRectF(x, y, w, h), rx, ry);
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("circle")) {
        const double r = attrDouble(attrs, QStringLiteral("r"));
        appendEllipse(ctx, transform,
                      QPointF(attrDouble(attrs, QStringLiteral("cx")),
                              attrDouble(attrs, QStringLiteral("cy"))),
                      r,
                      r);
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("ellipse")) {
        appendEllipse(ctx, transform,
                      QPointF(attrDouble(attrs, QStringLiteral("cx")),
                              attrDouble(attrs, QStringLiteral("cy"))),
                      attrDouble(attrs, QStringLiteral("rx")),
                      attrDouble(attrs, QStringLiteral("ry")));
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("line")) {
        appendMove(ctx.path, transform,
                   QPointF(attrDouble(attrs, QStringLiteral("x1")),
                           attrDouble(attrs, QStringLiteral("y1"))));
        appendLine(ctx.path, transform,
                   QPointF(attrDouble(attrs, QStringLiteral("x2")),
                           attrDouble(attrs, QStringLiteral("y2"))));
        ctx.hasOpenPath = true;
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("polyline") || name == QLatin1String("polygon")) {
        const QVector<QPointF> points = parsePoints(attrString(attrs, QStringLiteral("points")));
        if (!points.isEmpty()) {
            appendMove(ctx.path, transform, points.first());
            for (int i = 1; i < points.size(); ++i)
                appendLine(ctx.path, transform, points[i]);
            if (name == QLatin1String("polygon"))
                appendClose(ctx);
            else
                ctx.hasOpenPath = true;
        }
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("defs")
        || name == QLatin1String("title")
        || name == QLatin1String("desc")
        || name == QLatin1String("metadata")) {
        xml.skipCurrentElement();
        return;
    }

    if (name == QLatin1String("text")
        || name == QLatin1String("image")
        || name == QLatin1String("mask")
        || name == QLatin1String("clipPath")
        || name == QLatin1String("filter")
        || name == QLatin1String("pattern")
        || name == QLatin1String("linearGradient")
        || name == QLatin1String("radialGradient")) {
        ctx.error = QStringLiteral("This SVG contains unsupported features.");
    }

    xml.skipCurrentElement();
}

QString rectToString(const QRectF& rect)
{
    return QStringLiteral("%1 %2 %3 %4")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
}

} // namespace

SvgShapeConversionResult SvgShapeConverter::convertFromResource(const QString& resourcePath)
{
    {
        QMutexLocker locker(&conversionCacheMutex());
        const auto it = conversionCache().constFind(resourcePath);
        if (it != conversionCache().constEnd())
            return it.value();
    }

    const auto finish = [&resourcePath](const SvgShapeConversionResult& result) {
        QMutexLocker locker(&conversionCacheMutex());
        conversionCache().insert(resourcePath, result);
        return result;
    };

    SvgShapeConversionResult result;

    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("Could not load this custom shape.");
        return finish(result);
    }

    QXmlStreamReader xml(&file);
    SvgParseContext ctx;
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("svg")) {
            result.errorMessage = QStringLiteral("Could not load this custom shape.");
            return finish(result);
        }
        parseElement(xml, ctx, QTransform(), SvgStyle());
    }

    if (!ctx.error.isEmpty()) {
        result.errorMessage = ctx.error;
        return finish(result);
    }
    if (xml.hasError()) {
        result.errorMessage = QStringLiteral("Could not load this custom shape.");
        return finish(result);
    }
    if (ctx.path.commands.empty()) {
        result.errorMessage = QStringLiteral("This SVG does not contain a valid vector path.");
        return finish(result);
    }

    const QRectF geometryBounds = painterPathFor(ctx.path).boundingRect();
    if (geometryBounds.isEmpty()) {
        result.errorMessage = QStringLiteral("This SVG does not contain a valid vector path.");
        return finish(result);
    }

    ctx.path.localBounds = ctx.hasViewBox ? ctx.viewBox : geometryBounds;
    ctx.path.closed = ctx.hasClosedPath && !ctx.hasOpenPath;

    ShapeData shape;
    shape.path = flipPathY(ctx.path, ctx.path.localBounds);
    shape.metadata.presetId = QStringLiteral("custom-svg-icon");
    shape.metadata.parameters.insert(QStringLiteral("sourceResourcePath"), resourcePath);
    shape.metadata.parameters.insert(QStringLiteral("sourceViewBox"), rectToString(ctx.path.localBounds));
    shape.metadata.parametricEditable = false;

    result.shapeData = shape;
    result.success = true;
    return finish(result);
}
