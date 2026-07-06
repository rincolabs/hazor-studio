#include "CurvesData.hpp"

#include <QStringList>
#include <algorithm>
#include <cmath>

namespace curves {

PointList identityPoints()
{
    return { QPoint(0, 0), QPoint(255, 255) };
}

CurvesData::CurvesData()
{
    for (auto& c : channels)
        c = identityPoints();
}

PointList sanitize(const PointList& pts)
{
    PointList out = pts;
    if (out.size() < 2)
        out = identityPoints();

    for (auto& p : out) {
        p.setX(std::clamp(p.x(), 0, 255));
        p.setY(std::clamp(p.y(), 0, 255));
    }
    std::sort(out.begin(), out.end(),
              [](const QPoint& a, const QPoint& b) { return a.x() < b.x(); });

    // Drop interior points sharing an x with the previous point (keep first).
    PointList dedup;
    dedup.reserve(out.size());
    for (const auto& p : out) {
        if (!dedup.empty() && dedup.back().x() == p.x())
            continue;
        dedup.push_back(p);
    }
    out.swap(dedup);

    if (out.size() < 2)
        out = identityPoints();
    // Endpoints are kept where they are (the black/white input sliders move the
    // first/last point horizontally); the LUT extends flat beyond them. The two
    // endpoints always exist and are never removed.
    return out;
}

bool CurvesData::isChannelIdentity(Channel c) const
{
    const PointList& p = points(c);
    return p.size() == 2 && p.front() == QPoint(0, 0) && p.back() == QPoint(255, 255);
}

bool CurvesData::isIdentity() const
{
    for (int i = 0; i < kChannelCount; ++i) {
        if (i == static_cast<int>(Channel::Alpha))
            continue; // alpha is not applied
        if (!isChannelIdentity(static_cast<Channel>(i)))
            return false;
    }
    return true;
}

void CurvesData::resetChannel(Channel c)
{
    points(c) = identityPoints();
}

void CurvesData::resetAll()
{
    for (auto& c : channels)
        c = identityPoints();
}

namespace {

// Monotone cubic Hermite (Fritsch–Carlson / PCHIP). Fills lut[0..255] with the
// curve's output for every integer input. Smooth and overshoot-free — the
// standard feel for tone curves in image editors.
void pchipLut(const PointList& rawPts, std::array<unsigned char, 256>& lut)
{
    const PointList pts = sanitize(rawPts);
    const int n = static_cast<int>(pts.size());

    auto clampByte = [](double v) -> unsigned char {
        return static_cast<unsigned char>(std::clamp<long>(std::lround(v), 0, 255));
    };

    if (n == 1) {
        for (int i = 0; i < 256; ++i)
            lut[i] = clampByte(pts[0].y());
        return;
    }

    std::vector<double> x(n), y(n), m(n), delta(n - 1);
    for (int i = 0; i < n; ++i) {
        x[i] = pts[i].x();
        y[i] = pts[i].y();
    }
    for (int i = 0; i < n - 1; ++i) {
        const double dx = x[i + 1] - x[i];
        delta[i] = dx != 0.0 ? (y[i + 1] - y[i]) / dx : 0.0;
    }

    // Initial tangents.
    m[0] = delta[0];
    m[n - 1] = delta[n - 2];
    for (int i = 1; i < n - 1; ++i) {
        if (delta[i - 1] * delta[i] <= 0.0)
            m[i] = 0.0;
        else
            m[i] = (delta[i - 1] + delta[i]) / 2.0;
    }

    // Fritsch–Carlson monotonicity constraint.
    for (int i = 0; i < n - 1; ++i) {
        if (delta[i] == 0.0) {
            m[i] = 0.0;
            m[i + 1] = 0.0;
            continue;
        }
        const double a = m[i] / delta[i];
        const double b = m[i + 1] / delta[i];
        const double s = a * a + b * b;
        if (s > 9.0) {
            const double tau = 3.0 / std::sqrt(s);
            m[i] = tau * a * delta[i];
            m[i + 1] = tau * b * delta[i];
        }
    }

    int seg = 0;
    for (int xi = 0; xi < 256; ++xi) {
        if (xi <= x[0]) {
            lut[xi] = clampByte(y[0]);
            continue;
        }
        if (xi >= x[n - 1]) {
            lut[xi] = clampByte(y[n - 1]);
            continue;
        }
        while (seg < n - 2 && xi >= x[seg + 1])
            ++seg;
        const double dx = x[seg + 1] - x[seg];
        const double t = (xi - x[seg]) / dx;
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;
        const double v = h00 * y[seg] + h10 * dx * m[seg]
                       + h01 * y[seg + 1] + h11 * dx * m[seg + 1];
        lut[xi] = clampByte(v);
    }
}

} // namespace

void lutFromPoints(const PointList& pts, std::array<unsigned char, 256>& lut)
{
    pchipLut(pts, lut);
}

void CurvesData::buildChannelLut(Channel c, std::array<unsigned char, 256>& lut) const
{
    pchipLut(points(c), lut);
}

void CurvesData::buildCombinedLuts(std::array<unsigned char, 256>& rLut,
                                   std::array<unsigned char, 256>& gLut,
                                   std::array<unsigned char, 256>& bLut) const
{
    std::array<unsigned char, 256> master{}, red{}, green{}, blue{};
    buildChannelLut(Channel::RGB, master);
    buildChannelLut(Channel::Red, red);
    buildChannelLut(Channel::Green, green);
    buildChannelLut(Channel::Blue, blue);
    for (int v = 0; v < 256; ++v) {
        const int m = master[v];
        rLut[v] = red[m];
        gLut[v] = green[m];
        bLut[v] = blue[m];
    }
}

// ── Encode / decode ─────────────────────────────────────────────────────────

namespace {
const char* channelKey(Channel c)
{
    switch (c) {
    case Channel::RGB:   return "rgb";
    case Channel::Red:   return "red";
    case Channel::Green: return "green";
    case Channel::Blue:  return "blue";
    case Channel::Alpha: return "alpha";
    }
    return "rgb";
}
} // namespace

QString CurvesData::encode() const
{
    QStringList parts;
    for (int i = 0; i < kChannelCount; ++i) {
        const PointList& pts = channels[i];
        QStringList nums;
        nums.reserve(pts.size() * 2);
        for (const auto& p : pts) {
            nums << QString::number(p.x());
            nums << QString::number(p.y());
        }
        parts << QStringLiteral("%1:%2")
                     .arg(QString::fromLatin1(channelKey(static_cast<Channel>(i))),
                          nums.join(QLatin1Char(',')));
    }
    return parts.join(QLatin1Char(';'));
}

CurvesData CurvesData::decode(const QString& s)
{
    CurvesData data;
    if (s.isEmpty())
        return data;

    const QStringList parts = s.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = part.left(colon);
        const QStringList nums = part.mid(colon + 1)
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
        PointList pts;
        for (int i = 0; i + 1 < nums.size(); i += 2)
            pts.emplace_back(nums[i].toInt(), nums[i + 1].toInt());

        Channel c = Channel::RGB;
        if (key == QLatin1String("rgb"))   c = Channel::RGB;
        else if (key == QLatin1String("red"))   c = Channel::Red;
        else if (key == QLatin1String("green")) c = Channel::Green;
        else if (key == QLatin1String("blue"))  c = Channel::Blue;
        else if (key == QLatin1String("alpha")) c = Channel::Alpha;
        else continue;

        if (pts.size() >= 2)
            data.points(c) = sanitize(pts);
    }
    return data;
}

QVariantMap CurvesData::toParams() const
{
    QVariantMap m;
    m[QStringLiteral("curves")] = encode();
    m[QStringLiteral("preset")] = preset;
    return m;
}

CurvesData CurvesData::fromParams(const QVariantMap& params)
{
    CurvesData data = decode(params.value(QStringLiteral("curves")).toString());
    data.preset = params.value(QStringLiteral("preset"),
                               QStringLiteral("Default")).toString();
    return data;
}

// ── Presets ──────────────────────────────────────────────────────────────────

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> kPresets = {
        { QStringLiteral("Default"),           { {0,0}, {255,255} } },
        { QStringLiteral("Linear"),            { {0,0}, {255,255} } },
        { QStringLiteral("Increase Contrast"), { {0,0}, {64,50}, {128,128}, {192,205}, {255,255} } },
        { QStringLiteral("Medium Contrast"),   { {0,0}, {64,45}, {128,128}, {192,210}, {255,255} } },
        { QStringLiteral("Strong Contrast"),   { {0,0}, {48,30}, {128,128}, {208,230}, {255,255} } },
        { QStringLiteral("Lift Shadows"),      { {0,20}, {64,85}, {128,145}, {255,255} } },
        { QStringLiteral("Crush Blacks"),      { {0,0}, {40,0}, {128,120}, {255,255} } },
        { QStringLiteral("Fade"),              { {0,35}, {128,140}, {255,245} } },
        { QStringLiteral("Brighten"),          { {0,0}, {64,85}, {128,155}, {192,220}, {255,255} } },
        { QStringLiteral("Darken"),            { {0,0}, {64,45}, {128,105}, {192,170}, {255,255} } },
        { QStringLiteral("Matte Look"),        { {0,28}, {64,75}, {128,135}, {192,195}, {255,238} } },
        { QStringLiteral("Invert"),            { {0,255}, {255,0} } },
        { QStringLiteral("Solarize"),          { {0,0}, {64,120}, {128,255}, {192,120}, {255,0} } },
    };
    return kPresets;
}

const Preset* findPreset(const QString& id)
{
    for (const auto& p : presets()) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

} // namespace curves
