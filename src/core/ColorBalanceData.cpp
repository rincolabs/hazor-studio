#include "ColorBalanceData.hpp"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace colorbalance {

namespace {

// Tonal weighting curves over the normalized input value t ∈ [0, 1]. Smooth,
// overlapping Gaussians centred on black / mid-grey / white give shadows,
// midtones and highlights bands that transition without banding and leave the
// other ranges mostly untouched — the standard Color Balance feel.
constexpr double kSigma = 0.25;          // band half-width
// Slider 100 at a band's peak maps to ~85 levels of channel shift; echoes the
// strength of standard transfer tables without going full-range.
constexpr double kStrength = 0.85;

inline double gaussian(double t, double center)
{
    const double d = t - center;
    return std::exp(-(d * d) / (2.0 * kSigma * kSigma));
}

inline unsigned char clamp255(double v)
{
    return static_cast<unsigned char>(
        std::clamp(static_cast<int>(std::lround(v)), 0, 255));
}

const char* toneKey(Tone t)
{
    switch (t) {
    case Tone::Shadows:    return "sh";
    case Tone::Midtones:   return "mi";
    case Tone::Highlights: return "hi";
    }
    return "mi";
}

} // namespace

void ColorBalanceData::setValue(Tone t, Axis a, int v)
{
    values[static_cast<int>(t)][static_cast<int>(a)] = std::clamp(v, -100, 100);
}

bool ColorBalanceData::isIdentity() const
{
    for (const auto& tone : values)
        for (int v : tone)
            if (v != 0)
                return false;
    return true;
}

void ColorBalanceData::resetTone(Tone t)
{
    values[static_cast<int>(t)].fill(0);
}

void ColorBalanceData::reset()
{
    for (auto& tone : values)
        tone.fill(0);
    preserveLuminosity = true;
}

void ColorBalanceData::buildTransferLuts(std::array<unsigned char, 256>& rLut,
                                         std::array<unsigned char, 256>& gLut,
                                         std::array<unsigned char, 256>& bLut) const
{
    const int crS = value(Tone::Shadows, Axis::CyanRed);
    const int crM = value(Tone::Midtones, Axis::CyanRed);
    const int crH = value(Tone::Highlights, Axis::CyanRed);
    const int mgS = value(Tone::Shadows, Axis::MagentaGreen);
    const int mgM = value(Tone::Midtones, Axis::MagentaGreen);
    const int mgH = value(Tone::Highlights, Axis::MagentaGreen);
    const int ybS = value(Tone::Shadows, Axis::YellowBlue);
    const int ybM = value(Tone::Midtones, Axis::YellowBlue);
    const int ybH = value(Tone::Highlights, Axis::YellowBlue);

    for (int i = 0; i < 256; ++i) {
        const double t = i / 255.0;
        const double ws = gaussian(t, 0.0);
        const double wm = gaussian(t, 0.5);
        const double wh = gaussian(t, 1.0);

        const double rShift = (crS * ws + crM * wm + crH * wh) * kStrength;
        const double gShift = (mgS * ws + mgM * wm + mgH * wh) * kStrength;
        const double bShift = (ybS * ws + ybM * wm + ybH * wh) * kStrength;

        rLut[i] = clamp255(i + rShift);
        gLut[i] = clamp255(i + gShift);
        bLut[i] = clamp255(i + bShift);
    }
}

QString ColorBalanceData::encode() const
{
    QStringList parts;
    for (int ti = 0; ti < kToneCount; ++ti) {
        const auto& tone = values[ti];
        parts << QStringLiteral("%1:%2,%3,%4")
                     .arg(QString::fromLatin1(toneKey(static_cast<Tone>(ti))))
                     .arg(tone[0]).arg(tone[1]).arg(tone[2]);
    }
    return parts.join(QLatin1Char(';'));
}

ColorBalanceData ColorBalanceData::decode(const QString& s)
{
    ColorBalanceData data;
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
        if (nums.size() < kAxisCount)
            continue;

        Tone tone;
        if (key == QLatin1String("sh"))      tone = Tone::Shadows;
        else if (key == QLatin1String("mi")) tone = Tone::Midtones;
        else if (key == QLatin1String("hi")) tone = Tone::Highlights;
        else continue;

        for (int a = 0; a < kAxisCount; ++a)
            data.setValue(tone, static_cast<Axis>(a), nums[a].toInt());
    }
    return data;
}

QVariantMap ColorBalanceData::toParams() const
{
    QVariantMap m;
    m[QStringLiteral("colorbalance")] = encode();
    m[QStringLiteral("preserveLuminosity")] = preserveLuminosity;
    return m;
}

ColorBalanceData ColorBalanceData::fromParams(const QVariantMap& params)
{
    ColorBalanceData data = decode(params.value(QStringLiteral("colorbalance")).toString());
    data.preserveLuminosity =
        params.value(QStringLiteral("preserveLuminosity"), true).toBool();
    return data;
}

} // namespace colorbalance
