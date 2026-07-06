#include "HueSaturationData.hpp"

#include <algorithm>
#include <cmath>

namespace huesaturation {

const std::array<float, kBandCount> kBandCenters = {
    0.0f,    // Reds
    60.0f,   // Yellows
    120.0f,  // Greens
    180.0f,  // Cyans
    240.0f,  // Blues
    300.0f   // Magentas
};

namespace {

inline float clampf(float v, float lo, float hi)
{
    return std::min(std::max(v, lo), hi);
}

inline float wrap360(float h)
{
    h = std::fmod(h, 360.0f);
    if (h < 0.0f)
        h += 360.0f;
    return h;
}

// Smallest absolute angular distance between two hues, in [0, 180].
inline float hueDistance(float h, float center)
{
    float d = std::fabs(wrap360(h) - center);
    if (d > 180.0f)
        d = 360.0f - d;
    return d;
}

// Brings absolute hue `h` into a continuous degree frame within ±180 of `mid`,
// so the four ordered band edges (which live in that frame) can be compared
// without special-casing the 0/360 wrap.
inline float toCenterFrame(float h, float mid)
{
    float hh = wrap360(h) - mid;          // (-mid, 360-mid)
    hh = std::fmod(hh + 180.0f, 360.0f);
    if (hh < 0.0f)
        hh += 360.0f;
    return mid + (hh - 180.0f);           // mid + [-180, 180)
}

// RGB (0..1) → HSL. Hue in degrees [0,360), s/l in [0,1].
void rgbToHsl(float r, float g, float b, float& h, float& s, float& l)
{
    const float mx = std::max({ r, g, b });
    const float mn = std::min({ r, g, b });
    l = 0.5f * (mx + mn);
    if (mx <= mn) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    const float d = mx - mn;
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r)
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;
    h *= 60.0f;
}

inline float hue2rgb(float p, float q, float t)
{
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

// HSL → RGB (0..1). Hue in degrees.
void hslToRgb(float h, float s, float l, float& r, float& g, float& b)
{
    if (s <= 0.0f) {
        r = g = b = l;
        return;
    }
    const float hn = wrap360(h) / 360.0f;
    const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    r = hue2rgb(p, q, hn + 1.0f / 3.0f);
    g = hue2rgb(p, q, hn);
    b = hue2rgb(p, q, hn - 1.0f / 3.0f);
}

// Saturation slider response: multiplicative so a perfectly neutral pixel
// (s == 0) stays neutral (a positive slider intensifies existing chroma rather
// than inventing colour). adj normalised to [-1,1].
inline float applySaturation(float s, float adj)
{
    const float a = clampf(adj / 100.0f, -1.0f, 1.0f);
    return clampf(s * (1.0f + a), 0.0f, 1.0f);
}

// Lightness slider response: additive toward the extremes — −100 drives to
// black, +100 to white, 0 leaves it unchanged. adj normalised to [-1,1].
inline float applyLightness(float l, float adj)
{
    const float a = clampf(adj / 100.0f, -1.0f, 1.0f);
    return a >= 0.0f ? l + a * (1.0f - l) : l * (1.0f + a);
}

const char* rangeKey(Range r)
{
    switch (r) {
    case Range::Master:   return "m";
    case Range::Reds:     return "re";
    case Range::Yellows:  return "ye";
    case Range::Greens:   return "gr";
    case Range::Cyans:    return "cy";
    case Range::Blues:    return "bl";
    case Range::Magentas: return "ma";
    }
    return "m";
}

Range rangeFromKey(const QString& key, bool& ok)
{
    ok = true;
    if (key == QLatin1String("m"))  return Range::Master;
    if (key == QLatin1String("re")) return Range::Reds;
    if (key == QLatin1String("ye")) return Range::Yellows;
    if (key == QLatin1String("gr")) return Range::Greens;
    if (key == QLatin1String("cy")) return Range::Cyans;
    if (key == QLatin1String("bl")) return Range::Blues;
    if (key == QLatin1String("ma")) return Range::Magentas;
    ok = false;
    return Range::Master;
}

} // namespace

// ── HueRange geometry ────────────────────────────────────────────────────────

namespace {
// Average falloff-shoulder width of a band, with a sane floor so eyedropper
// growth never produces a hard edge.
inline float shoulderWidth(const HueRange& b)
{
    const float left = b.innerStart - b.outerStart;
    const float right = b.outerEnd - b.innerEnd;
    return std::max(10.0f, 0.5f * (left + right));
}
// Re-derive centre/wrap from the (continuous) inner edges after an edit, and
// re-assert the ordering invariant outerStart ≤ innerStart ≤ innerEnd ≤ outerEnd.
void normalizeBand(HueRange& b)
{
    b.innerStart = std::min(b.innerStart, b.innerEnd);
    b.outerStart = std::min(b.outerStart, b.innerStart);
    b.outerEnd = std::max(b.outerEnd, b.innerEnd);
    b.centerHue = wrap360(0.5f * (b.innerStart + b.innerEnd));
    b.supportsWrap = b.outerStart < 0.0f || b.outerEnd > 360.0f;
}
} // namespace

float HueRange::weight(float h) const
{
    if (!enabled)
        return 0.0f;
    const float mid = 0.5f * (innerStart + innerEnd);
    const float hh = toCenterFrame(h, mid);
    if (hh <= outerStart || hh >= outerEnd)
        return 0.0f;
    if (hh < innerStart) {
        const float denom = innerStart - outerStart;
        return denom > 1e-4f ? clampf((hh - outerStart) / denom, 0.0f, 1.0f) : 1.0f;
    }
    if (hh > innerEnd) {
        const float denom = outerEnd - innerEnd;
        return denom > 1e-4f ? clampf((outerEnd - hh) / denom, 0.0f, 1.0f) : 1.0f;
    }
    return 1.0f;
}

void HueRange::recenter(float hueDeg)
{
    const float halfInner = 0.5f * (innerEnd - innerStart);
    const float leftFall = innerStart - outerStart;
    const float rightFall = outerEnd - innerEnd;
    const float c = wrap360(hueDeg);
    innerStart = c - halfInner;
    innerEnd = c + halfInner;
    outerStart = innerStart - leftFall;
    outerEnd = innerEnd + rightFall;
    normalizeBand(*this);
}

void HueRange::expandToInclude(float hueDeg)
{
    const float mid = 0.5f * (innerStart + innerEnd);
    const float hh = toCenterFrame(hueDeg, mid);
    const float shoulder = shoulderWidth(*this);
    // Grow the core to reach the new hue and keep a falloff shoulder beyond it.
    // Clamp the total span so the band never wraps onto itself (≤ ~300°).
    if (hh < innerStart) {
        innerStart = std::max(hh, mid - 150.0f);
        outerStart = std::min(outerStart, innerStart - shoulder);
    } else if (hh > innerEnd) {
        innerEnd = std::min(hh, mid + 150.0f);
        outerEnd = std::max(outerEnd, innerEnd + shoulder);
    }
    sampledColors.push_back(static_cast<int>(wrap360(hueDeg)));
    normalizeBand(*this);
}

void HueRange::contractToExclude(float hueDeg)
{
    const float mid = 0.5f * (innerStart + innerEnd);
    const float hh = toCenterFrame(hueDeg, mid);
    if (hh <= outerStart || hh >= outerEnd)
        return; // already outside the band
    const float shoulder = shoulderWidth(*this);
    // Single-segment limitation: we cannot split a band into two pieces, so we
    // pull the *nearer* outer edge in to `hh` (making the clicked hue the new
    // zero-weight boundary) and preserve the larger remaining side.
    const float distLeft = hh - outerStart;
    const float distRight = outerEnd - hh;
    if (distLeft <= distRight) {
        outerStart = hh;
        innerStart = std::max(innerStart, outerStart + 0.5f * shoulder);
        innerStart = std::min(innerStart, innerEnd);
    } else {
        outerEnd = hh;
        innerEnd = std::min(innerEnd, outerEnd - 0.5f * shoulder);
        innerEnd = std::max(innerEnd, innerStart);
    }
    sampledColors.push_back(static_cast<int>(wrap360(hueDeg)));
    normalizeBand(*this);
}

std::array<HueRange, kBandCount> defaultBandRanges()
{
    std::array<HueRange, kBandCount> b{};
    for (int i = 0; i < kBandCount; ++i) {
        const float c = kBandCenters[i];
        HueRange r;
        r.enabled = true;
        r.centerHue = c;
        r.innerStart = c - kDefaultCoreHalf;
        r.innerEnd = c + kDefaultCoreHalf;
        r.outerStart = c - (kDefaultCoreHalf + kDefaultFalloff);
        r.outerEnd = c + (kDefaultCoreHalf + kDefaultFalloff);
        r.supportsWrap = r.outerStart < 0.0f || r.outerEnd > 360.0f;
        b[i] = r;
    }
    return b;
}

Range HueSaturationData::nearestBand(float hueDeg) const
{
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < kBandCount; ++i) {
        const float d = hueDistance(hueDeg, wrap360(bands[i].centerHue));
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return static_cast<Range>(best + 1);
}

bool HueSaturationData::isIdentity() const
{
    if (colorize)
        return false; // a tint is never a no-op
    for (const auto& v : ranges)
        if (!v.isZero())
            return false;
    return true;
}

void HueSaturationData::resetRange(Range r)
{
    range(r) = HslValues{};
    // Restore this band's geometry to its professional default too, so "Reset
    // Range" fully undoes any eyedropper/handle refinement of the range.
    if (r != Range::Master) {
        const int i = static_cast<int>(r) - 1;
        bands[i] = defaultBandRanges()[i];
    }
}

void HueSaturationData::reset()
{
    presetName = QStringLiteral("Default");
    activeRange = Range::Master;
    colorize = false;
    for (auto& v : ranges)
        v = HslValues{};
    bands = defaultBandRanges();
}

void HueSaturationData::applyPixel(float& r, float& g, float& b) const
{
    const HslValues& master = range(Range::Master);

    if (colorize) {
        // Global tint: keep the pixel's luminance, recolor at the target hue.
        const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
        const float targetHue = wrap360(static_cast<float>(master.hue));
        const float cs = clampf(static_cast<float>(master.saturation), 0.0f, 100.0f) / 100.0f;
        const float l = applyLightness(luma, static_cast<float>(master.lightness));
        hslToRgb(targetHue, cs, l, r, g, b);
        return;
    }

    float h, s, l;
    rgbToHsl(r, g, b, h, s, l);

    float hueShift = static_cast<float>(master.hue);
    float satAdj = static_cast<float>(master.saturation);
    float lightAdj = static_cast<float>(master.lightness);
    for (int i = 0; i < kBandCount; ++i) {
        const HslValues& bandHsl = ranges[i + 1]; // 0 is Master
        if (bandHsl.isZero() || !bands[i].enabled)
            continue;
        const float w = bands[i].weight(h);
        if (w <= 0.0f)
            continue;
        hueShift += w * static_cast<float>(bandHsl.hue);
        satAdj += w * static_cast<float>(bandHsl.saturation);
        lightAdj += w * static_cast<float>(bandHsl.lightness);
    }

    h = wrap360(h + hueShift);
    s = applySaturation(s, satAdj);
    l = applyLightness(l, lightAdj);
    hslToRgb(h, s, l, r, g, b);
}

QString HueSaturationData::encode() const
{
    QStringList parts;
    for (int i = 0; i < kRangeCount; ++i) {
        const HslValues& v = ranges[i];
        parts << QStringLiteral("%1:%2,%3,%4")
                     .arg(QString::fromLatin1(rangeKey(static_cast<Range>(i))))
                     .arg(v.hue).arg(v.saturation).arg(v.lightness);
    }
    return parts.join(QLatin1Char(';'));
}

HueSaturationData HueSaturationData::decode(const QString& s)
{
    HueSaturationData data;
    if (s.isEmpty())
        return data;

    const QStringList parts = s.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        bool ok = false;
        const Range r = rangeFromKey(part.left(colon), ok);
        if (!ok)
            continue;
        const QStringList nums = part.mid(colon + 1)
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (nums.size() < 3)
            continue;
        HslValues v;
        v.hue        = std::clamp(nums[0].toInt(), -180, 180);
        v.saturation = std::clamp(nums[1].toInt(), -100, 100);
        v.lightness  = std::clamp(nums[2].toInt(), -100, 100);
        data.range(r) = v;
    }
    return data;
}

// ── Band-geometry serialization ──────────────────────────────────────────────
// Stored as one extra scalar string so it rides the same scalar-only adjustment
// serializer as `huesaturation`. Per band: "key:enabled,center,outerStart,
// innerStart,innerEnd,outerEnd,wrap", bands joined by ';'. Sampled-colour
// history rides a separate "hsSampled" string ("key:h1,h2;..") so the geometry
// string stays fixed-width and easy to parse. Files without these keys load the
// professional defaults (legacy projects + pre-refinement Hue/Saturation).
namespace {

QString encodeBands(const std::array<HueRange, kBandCount>& bands)
{
    QStringList parts;
    for (int i = 0; i < kBandCount; ++i) {
        const HueRange& b = bands[i];
        parts << QStringLiteral("%1:%2,%3,%4,%5,%6,%7,%8")
                     .arg(QString::fromLatin1(rangeKey(static_cast<Range>(i + 1))))
                     .arg(b.enabled ? 1 : 0)
                     .arg(b.centerHue, 0, 'f', 2)
                     .arg(b.outerStart, 0, 'f', 2)
                     .arg(b.innerStart, 0, 'f', 2)
                     .arg(b.innerEnd, 0, 'f', 2)
                     .arg(b.outerEnd, 0, 'f', 2)
                     .arg(b.supportsWrap ? 1 : 0);
    }
    return parts.join(QLatin1Char(';'));
}

void decodeBands(const QString& s, std::array<HueRange, kBandCount>& bands)
{
    if (s.isEmpty())
        return;
    for (const QString& part : s.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        bool ok = false;
        const Range r = rangeFromKey(part.left(colon), ok);
        if (!ok || r == Range::Master)
            continue;
        const QStringList n = part.mid(colon + 1)
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (n.size() < 7)
            continue;
        HueRange b;
        b.enabled = n[0].toInt() != 0;
        b.centerHue = n[1].toFloat();
        b.outerStart = n[2].toFloat();
        b.innerStart = n[3].toFloat();
        b.innerEnd = n[4].toFloat();
        b.outerEnd = n[5].toFloat();
        b.supportsWrap = n[6].toInt() != 0;
        bands[static_cast<int>(r) - 1] = b;
    }
}

QString encodeSampled(const std::array<HueRange, kBandCount>& bands)
{
    QStringList parts;
    for (int i = 0; i < kBandCount; ++i) {
        const std::vector<int>& s = bands[i].sampledColors;
        if (s.empty())
            continue;
        QStringList nums;
        for (int v : s)
            nums << QString::number(v);
        parts << QStringLiteral("%1:%2")
                     .arg(QString::fromLatin1(rangeKey(static_cast<Range>(i + 1))))
                     .arg(nums.join(QLatin1Char(',')));
    }
    return parts.join(QLatin1Char(';'));
}

void decodeSampled(const QString& s, std::array<HueRange, kBandCount>& bands)
{
    if (s.isEmpty())
        return;
    for (const QString& part : s.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        bool ok = false;
        const Range r = rangeFromKey(part.left(colon), ok);
        if (!ok || r == Range::Master)
            continue;
        std::vector<int>& dst = bands[static_cast<int>(r) - 1].sampledColors;
        dst.clear();
        for (const QString& num : part.mid(colon + 1)
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts))
            dst.push_back(num.toInt());
    }
}

} // namespace

QVariantMap HueSaturationData::toParams() const
{
    QVariantMap m;
    m[QStringLiteral("huesaturation")] = encode();
    m[QStringLiteral("colorize")] = colorize;
    m[QStringLiteral("activeRange")] = static_cast<int>(activeRange);
    m[QStringLiteral("presetName")] = presetName;
    m[QStringLiteral("hsRanges")] = encodeBands(bands);
    const QString sampled = encodeSampled(bands);
    if (!sampled.isEmpty())
        m[QStringLiteral("hsSampled")] = sampled;
    return m;
}

HueSaturationData HueSaturationData::fromParams(const QVariantMap& params)
{
    HueSaturationData data = decode(params.value(QStringLiteral("huesaturation")).toString());
    data.colorize = params.value(QStringLiteral("colorize"), false).toBool();
    int ar = params.value(QStringLiteral("activeRange"), 0).toInt();
    ar = std::clamp(ar, 0, kRangeCount - 1);
    data.activeRange = static_cast<Range>(ar);
    data.presetName = params.value(QStringLiteral("presetName"),
                                   QStringLiteral("Default")).toString();
    // bands already hold defaultBandRanges() (decode() leaves them); override
    // only the entries present in the saved string.
    decodeBands(params.value(QStringLiteral("hsRanges")).toString(), data.bands);
    decodeSampled(params.value(QStringLiteral("hsSampled")).toString(), data.bands);
    return data;
}

// ── Presets ──────────────────────────────────────────────────────────────────

namespace {

HueSaturationData makeMaster(int h, int s, int l, bool colorize = false)
{
    HueSaturationData d;
    d.range(Range::Master) = HslValues{ h, s, l };
    d.colorize = colorize;
    return d;
}

} // namespace

const std::vector<PresetInfo>& presets()
{
    static const std::vector<PresetInfo> kPresets = [] {
        std::vector<PresetInfo> v;

        v.push_back({ QStringLiteral("Default"), HueSaturationData{} });
        v.push_back({ QStringLiteral("Desaturate"), makeMaster(0, -100, 0) });
        v.push_back({ QStringLiteral("Increase Saturation"), makeMaster(0, 45, 0) });
        v.push_back({ QStringLiteral("Decrease Saturation"), makeMaster(0, -45, 0) });

        {   // Warm Boost — nudge hues toward red/orange and lift warm bands.
            HueSaturationData d = makeMaster(6, 10, 0);
            d.range(Range::Reds)    = HslValues{ 0, 14, 0 };
            d.range(Range::Yellows) = HslValues{ -4, 16, 4 };
            v.push_back({ QStringLiteral("Warm Boost"), d });
        }
        {   // Cool Boost — nudge toward blue and lift cool bands.
            HueSaturationData d = makeMaster(-6, 10, 0);
            d.range(Range::Cyans) = HslValues{ 0, 14, 0 };
            d.range(Range::Blues) = HslValues{ 4, 18, -2 };
            v.push_back({ QStringLiteral("Cool Boost"), d });
        }

        v.push_back({ QStringLiteral("High Contrast Color"), makeMaster(0, 40, -8) });
        v.push_back({ QStringLiteral("Soft Color"), makeMaster(0, -35, 12) });

        // Colorize presets (Master hue is the target tint).
        v.push_back({ QStringLiteral("Sepia Colorize"),
                      makeMaster(35, 30, 0, /*colorize=*/true) });
        v.push_back({ QStringLiteral("Cyanotype Colorize"),
                      makeMaster(-150, 40, -5, /*colorize=*/true) }); // ≈210° cyan-blue

        return v;
    }();
    return kPresets;
}

HueSaturationData presetData(const QString& name)
{
    for (const auto& p : presets()) {
        if (p.name == name) {
            HueSaturationData d = p.data;
            d.presetName = name;
            return d;
        }
    }
    return HueSaturationData{}; // Default / Custom / unknown
}

bool isPreset(const QString& name)
{
    for (const auto& p : presets())
        if (p.name == name)
            return true;
    return false;
}

} // namespace huesaturation
