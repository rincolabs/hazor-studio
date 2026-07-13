#include "CanvasPresets.hpp"

#include <QCoreApplication>

#include <cmath>

namespace canvaspresets {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("CanvasPresets", text);
}

} // namespace

QString documentKindLabel(DocumentKind kind)
{
    return kind == DocumentKind::Animation ? tr_("Animation") : tr_("Photo");
}

DocumentKind documentKindFromLabel(const QString& label)
{
    return label.compare(tr_("Animation"), Qt::CaseInsensitive) == 0
               ? DocumentKind::Animation
               : DocumentKind::Photo;
}

const QVector<CanvasPreset>& builtinPresets()
{
    static const QVector<CanvasPreset> presets = [] {
        auto photo = [](const QString& group, const QString& name, double w,
                        double h, const QString& unit, int dpi) {
            CanvasPreset p;
            p.group = group;
            p.name = name;
            p.kind = DocumentKind::Photo;
            p.width = w;
            p.height = h;
            p.unit = unit;
            p.resolution = dpi;
            return p;
        };
        auto anim = [](const QString& group, const QString& name, int w, int h) {
            CanvasPreset p;
            p.group = group;
            p.name = name;
            p.kind = DocumentKind::Animation;
            p.width = w;
            p.height = h;
            p.unit = QStringLiteral("px");
            p.resolution = 72;
            return p;
        };
        // Image-format entries (thumbnails, static posts) for the same platform
        // groups. Kept as Photo so they list without the "Video" tag and stay
        // out of the animation exporter.
        auto img = [](const QString& group, const QString& name, int w, int h) {
            CanvasPreset p;
            p.group = group;
            p.name = name;
            p.kind = DocumentKind::Photo;
            p.width = w;
            p.height = h;
            p.unit = QStringLiteral("px");
            p.resolution = 72;
            return p;
        };

        QVector<CanvasPreset> list;
        // ── Photo ──
        list << photo(tr_("Photo"), tr_("Landscape, 8 × 10 in"), 10.0, 8.0, QStringLiteral("in"), 300)
             << photo(tr_("Photo"), tr_("Landscape, 16 × 20 in"), 20.0, 16.0, QStringLiteral("in"), 300)
             << photo(tr_("Photo"), tr_("Portrait, 8 × 10 in"), 8.0, 10.0, QStringLiteral("in"), 300)
             << photo(tr_("Photo"), tr_("Square, 12 × 12 in"), 12.0, 12.0, QStringLiteral("in"), 300)
             << photo(tr_("Photo"), tr_("Square, 8 × 8 in"), 8.0, 8.0, QStringLiteral("in"), 300)
             << photo(tr_("Paper Sizes"), QStringLiteral("A3"), 11.7, 16.5, QStringLiteral("in"), 300)
             << photo(tr_("Paper Sizes"), QStringLiteral("A4"), 8.27, 11.69, QStringLiteral("in"), 300)
             << photo(tr_("Paper Sizes"), QStringLiteral("A5"), 5.83, 8.27, QStringLiteral("in"), 300)
             << photo(tr_("Screen"), tr_("HD, 1920 × 1080 px"), 1920.0, 1080.0, QStringLiteral("px"), 72)
             << photo(tr_("Screen"), tr_("4K UHD, 3840 × 2160 px"), 3840.0, 2160.0, QStringLiteral("px"), 72)
             << photo(tr_("Screen"), tr_("XGA, 1024 × 768 px"), 1024.0, 768.0, QStringLiteral("px"), 72)
             // ── Animation ── (also reused by Export Animation As)
             << anim(QStringLiteral("Instagram"), tr_("Square Post — 1080 × 1080"), 1080, 1080)
             << anim(QStringLiteral("Instagram"), tr_("Portrait Post — 1080 × 1350"), 1080, 1350)
             << anim(QStringLiteral("Instagram"), tr_("Story / Reels — 1080 × 1920"), 1080, 1920)
             << anim(QStringLiteral("Instagram"), tr_("Landscape — 1080 × 566"), 1080, 566)
             << anim(QStringLiteral("TikTok"), tr_("Vertical Video — 1080 × 1920"), 1080, 1920)
             << anim(QStringLiteral("YouTube"), tr_("Full HD — 1920 × 1080"), 1920, 1080)
             << anim(QStringLiteral("YouTube"), tr_("Quad HD — 2560 × 1440"), 2560, 1440)
             << anim(QStringLiteral("YouTube"), tr_("4K — 3840 × 2160"), 3840, 2160)
             << anim(QStringLiteral("YouTube"), tr_("Shorts — 1080 × 1920"), 1080, 1920)
             << anim(QStringLiteral("X / Twitter"), tr_("Landscape — 1280 × 720"), 1280, 720)
             << anim(QStringLiteral("X / Twitter"), tr_("Square — 1080 × 1080"), 1080, 1080)
             << anim(QStringLiteral("Facebook"), tr_("Feed — 1080 × 1350"), 1080, 1350)
             << anim(QStringLiteral("Facebook"), tr_("Landscape — 1280 × 720"), 1280, 720)
             << anim(QStringLiteral("Facebook"), tr_("Stories / Reels — 1080 × 1920"), 1080, 1920)
             // ── Image posts & thumbnails ──
             << img(QStringLiteral("Instagram"), tr_("Square Post — 1080 × 1080"), 1080, 1080)
             << img(QStringLiteral("Instagram"), tr_("Portrait Post — 1080 × 1350"), 1080, 1350)
             << img(QStringLiteral("Instagram"), tr_("Landscape Post — 1080 × 566"), 1080, 566)
             << img(QStringLiteral("Instagram"), tr_("Story — 1080 × 1920"), 1080, 1920)
             << img(QStringLiteral("TikTok"), tr_("Photo Post — 1080 × 1920"), 1080, 1920)
             << img(QStringLiteral("YouTube"), tr_("Thumbnail — 1280 × 720"), 1280, 720)
             << img(QStringLiteral("YouTube"), tr_("Channel Banner — 2560 × 1440"), 2560, 1440)
             << img(QStringLiteral("X / Twitter"), tr_("Post — 1600 × 900"), 1600, 900)
             << img(QStringLiteral("X / Twitter"), tr_("Header — 1500 × 500"), 1500, 500)
             << img(QStringLiteral("Facebook"), tr_("Post — 1200 × 630"), 1200, 630)
             << img(QStringLiteral("Facebook"), tr_("Cover — 851 × 315"), 851, 315)
             << img(QStringLiteral("Facebook"), tr_("Story — 1080 × 1920"), 1080, 1920);
        return list;
    }();
    return presets;
}

QSize presetPixelSize(const CanvasPreset& preset)
{
    const double dpi = preset.resolution > 0 ? preset.resolution : 300.0;
    auto toPixels = [&](double value, const QString& unit) -> int {
        if (unit == QLatin1String("px"))
            return static_cast<int>(std::lround(value));
        double inches = value;
        if (unit == QLatin1String("cm"))
            inches = value / 2.54;
        else if (unit == QLatin1String("mm"))
            inches = value / 25.4;
        return static_cast<int>(std::lround(inches * dpi));
    };
    return QSize(toPixels(preset.width, preset.unit),
                 toPixels(preset.height, preset.unit));
}

QVariantMap toVariantMap(const CanvasPreset& preset)
{
    return {
        {QStringLiteral("group"), preset.group},
        {QStringLiteral("name"), preset.name},
        {QStringLiteral("kind"), static_cast<int>(preset.kind)},
        {QStringLiteral("width"), preset.width},
        {QStringLiteral("height"), preset.height},
        {QStringLiteral("unit"), preset.unit},
        {QStringLiteral("resolution"), preset.resolution},
        {QStringLiteral("builtin"), preset.builtin},
    };
}

CanvasPreset fromVariantMap(const QVariantMap& map)
{
    CanvasPreset p;
    p.group = map.value(QStringLiteral("group")).toString();
    p.name = map.value(QStringLiteral("name")).toString();
    p.kind = static_cast<DocumentKind>(
        map.value(QStringLiteral("kind"), static_cast<int>(DocumentKind::Photo)).toInt());
    p.width = map.value(QStringLiteral("width")).toDouble();
    p.height = map.value(QStringLiteral("height")).toDouble();
    p.unit = map.value(QStringLiteral("unit"), QStringLiteral("px")).toString();
    p.resolution = map.value(QStringLiteral("resolution"), 300).toInt();
    p.builtin = map.value(QStringLiteral("builtin"), false).toBool();
    return p;
}

} // namespace canvaspresets
