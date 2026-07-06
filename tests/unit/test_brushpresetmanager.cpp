#define BOOST_TEST_MODULE BrushPresetManagerTest
#include <boost/test/included/unit_test.hpp>

#include "brush/BrushPresetManager.hpp"
#include "brush/BrushPreset.hpp"
#include "brush/BrushTypes.hpp"
#include "brush/DynamicsConfig.hpp"

#include <QJsonObject>
#include <QJsonArray>

// ── Helpers ─────────────────────────────────────────────────────

static BrushPreset makePreset(const QString& name, float size = 20,
                              BrushType type = BrushType::Round,
                              float hardness = 0.8f)
{
    BrushPreset p;
    p.name = name;
    p.settings.size = size;
    p.settings.type = type;
    p.settings.hardness = hardness;
    p.settings.color = Qt::black;
    return p;
}

// ── Suite ───────────────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(brushpresetmanager)

// ── CRUD ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(starts_empty)
{
    BrushPresetManager m;
    BOOST_CHECK_EQUAL(m.presets().size(), 0);
    BOOST_CHECK(m.presetNames().isEmpty());
}

BOOST_AUTO_TEST_CASE(add_preset_increases_count)
{
    BrushPresetManager m;
    m.addPreset(makePreset("Test"));
    BOOST_CHECK_EQUAL(m.presets().size(), 1);
    BOOST_CHECK_EQUAL(m.presetNames().size(), 1);
}

BOOST_AUTO_TEST_CASE(add_preset_replaces_by_name)
{
    BrushPresetManager m;
    m.addPreset(makePreset("Test", 10));
    m.addPreset(makePreset("Test", 99));
    BOOST_CHECK_EQUAL(m.presets().size(), 1);
    auto* p = m.findPreset("Test");
    BOOST_REQUIRE(p);
    BOOST_CHECK_CLOSE(p->settings.size, 99, 0.001f);
}

BOOST_AUTO_TEST_CASE(add_multiple_presets)
{
    BrushPresetManager m;
    m.addPreset(makePreset("A"));
    m.addPreset(makePreset("B"));
    m.addPreset(makePreset("C"));
    BOOST_CHECK_EQUAL(m.presets().size(), 3);
}

BOOST_AUTO_TEST_CASE(remove_preset_decreases_count)
{
    BrushPresetManager m;
    m.addPreset(makePreset("Test"));
    m.removePreset("Test");
    BOOST_CHECK_EQUAL(m.presets().size(), 0);
}

BOOST_AUTO_TEST_CASE(remove_preset_removes_correct_one)
{
    BrushPresetManager m;
    m.addPreset(makePreset("A"));
    m.addPreset(makePreset("B"));
    m.addPreset(makePreset("C"));
    m.removePreset("B");
    BOOST_CHECK_EQUAL(m.presets().size(), 2);
    BOOST_CHECK(m.findPreset("A"));
    BOOST_CHECK(!m.findPreset("B"));
    BOOST_CHECK(m.findPreset("C"));
}

BOOST_AUTO_TEST_CASE(remove_nonexistent_noop)
{
    BrushPresetManager m;
    m.addPreset(makePreset("A"));
    m.removePreset("NoSuch");
    BOOST_CHECK_EQUAL(m.presets().size(), 1);
}

BOOST_AUTO_TEST_CASE(find_preset_existing)
{
    BrushPresetManager m;
    m.addPreset(makePreset("Found"));
    auto* p = m.findPreset("Found");
    BOOST_REQUIRE(p);
    BOOST_CHECK_EQUAL(p->name.toStdString(), "Found");
}

BOOST_AUTO_TEST_CASE(find_preset_nonexistent)
{
    BrushPresetManager m;
    m.addPreset(makePreset("A"));
    BOOST_CHECK(!m.findPreset("NoSuch"));
}

BOOST_AUTO_TEST_CASE(preset_names_returns_list)
{
    BrushPresetManager m;
    m.addPreset(makePreset("B"));
    m.addPreset(makePreset("A"));
    m.addPreset(makePreset("C"));
    auto names = m.presetNames();
    BOOST_CHECK_EQUAL(names.size(), 3);
    BOOST_CHECK(names.contains("A"));
    BOOST_CHECK(names.contains("B"));
    BOOST_CHECK(names.contains("C"));
}

BOOST_AUTO_TEST_CASE(presets_const_ref_stable)
{
    BrushPresetManager m;
    m.addPreset(makePreset("X"));
    const auto& ref = m.presets();
    BOOST_CHECK_EQUAL(ref.size(), 1);
    BOOST_CHECK_EQUAL(ref[0].name.toStdString(), "X");
}

// ── Serialization ───────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(to_json_contains_all_fields)
{
    auto p = makePreset("SerTest", 42, BrushType::Square, 0.75f);
    p.settings.opacity = 0.9f;
    p.settings.flow = 0.8f;
    p.settings.color = QColor(100, 150, 200);
    p.settings.mode = BrushMode::Erase;
    p.settings.tipSource = BrushTipSource::Image;
    p.settings.tipImagePath = "/test/tip.png";
    p.settings.blendMode = BrushBlendMode::Multiply;
    p.settings.smoothingMode = SmoothingMode::CubicSpline;
    p.settings.smoothingRadius = 15.0f;
    p.settings.spacing = 0.5f;
    p.settings.airbrush = true;
    p.settings.wetEdges = true;

    auto json = p.toJson();
    BOOST_CHECK_EQUAL(json["name"].toString().toStdString(), "SerTest");
    BOOST_CHECK_CLOSE(json["size"].toDouble(), 42.0, 0.001);
    BOOST_CHECK_CLOSE(json["hardness"].toDouble(), 0.75, 0.001);
    BOOST_CHECK_CLOSE(json["opacity"].toDouble(), 0.9, 0.001);
    BOOST_CHECK_CLOSE(json["flow"].toDouble(), 0.8, 0.001);
    BOOST_CHECK_EQUAL(json["color"].toString().toStdString(), QColor(100,150,200).name().toStdString());
    BOOST_CHECK_EQUAL(json["type"].toInt(), static_cast<int>(BrushType::Square));
    BOOST_CHECK_EQUAL(json["mode"].toInt(), static_cast<int>(BrushMode::Erase));
    BOOST_CHECK_EQUAL(json["tipSource"].toInt(), static_cast<int>(BrushTipSource::Image));
    BOOST_CHECK_EQUAL(json["tipImagePath"].toString().toStdString(), "/test/tip.png");
    BOOST_CHECK_EQUAL(json["blendMode"].toInt(), static_cast<int>(BrushBlendMode::Multiply));
    BOOST_CHECK_EQUAL(json["smoothingMode"].toInt(), static_cast<int>(SmoothingMode::CubicSpline));
    BOOST_CHECK_CLOSE(json["smoothingRadius"].toDouble(), 15.0, 0.001);
    BOOST_CHECK_CLOSE(json["spacing"].toDouble(), 0.5, 0.001);
    BOOST_CHECK_EQUAL(json["airbrush"].toBool(), true);
    BOOST_CHECK_EQUAL(json["wetEdges"].toBool(), true);
    BOOST_CHECK(json.contains("shapeDynamics"));
    BOOST_CHECK(json.contains("scatterDynamics"));
    BOOST_CHECK(json.contains("colorDynamics"));
    BOOST_CHECK(json.contains("textureConfig"));
    BOOST_CHECK(json.contains("dualBrushConfig"));
}

BOOST_AUTO_TEST_CASE(from_json_empty_returns_defaults)
{
    auto p = BrushPreset::fromJson(QJsonObject());
    BOOST_CHECK(p.name.isEmpty());
    BOOST_CHECK_CLOSE(p.settings.size, 20.0f, 0.001f);
    BOOST_CHECK_CLOSE(p.settings.hardness, 0.8f, 0.001f);
    BOOST_CHECK_CLOSE(p.settings.opacity, 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(p.settings.flow, 1.0f, 0.001f);
    BOOST_CHECK(p.settings.color == Qt::black);
    BOOST_CHECK(p.settings.type == BrushType::Round);
    BOOST_CHECK(p.settings.mode == BrushMode::Paint);
    BOOST_CHECK(p.settings.tipSource == BrushTipSource::Circle);
    BOOST_CHECK(p.settings.blendMode == BrushBlendMode::Normal);
    BOOST_CHECK(p.settings.smoothingMode == SmoothingMode::Basic);
    BOOST_CHECK_CLOSE(p.settings.smoothingRadius, 10.0f, 0.001f);
    BOOST_CHECK_CLOSE(p.settings.spacing, 0.1f, 0.001f);
    BOOST_CHECK(!p.settings.airbrush);
    BOOST_CHECK(!p.settings.wetEdges);
    BOOST_CHECK(!p.settings.shapeDynamics.enabled);
    BOOST_CHECK(!p.settings.scatterDynamics.enabled);
    BOOST_CHECK(!p.settings.colorDynamics.enabled);
    BOOST_CHECK(!p.settings.textureConfig.enabled);
    BOOST_CHECK(!p.settings.dualBrushConfig.enabled);
}

BOOST_AUTO_TEST_CASE(roundtrip_preserves_all_scalar_fields)
{
    BrushPreset original;
    original.name = "Roundtrip";
    original.settings.size = 37;
    original.settings.hardness = 0.5f;
    original.settings.opacity = 0.3f;
    original.settings.flow = 0.7f;
    original.settings.color = QColor(255, 128, 64);
    original.settings.type = BrushType::Square;
    original.settings.mode = BrushMode::Erase;
    original.settings.tipSource = BrushTipSource::Image;
    original.settings.tipImagePath = "/path/to/tip.png";
    original.settings.blendMode = BrushBlendMode::Screen;
    original.settings.smoothingMode = SmoothingMode::PulledString;
    original.settings.smoothingRadius = 8;
    original.settings.spacing = 0.3f;
    original.settings.airbrush = true;
    original.settings.wetEdges = true;

    auto json = original.toJson();
    auto restored = BrushPreset::fromJson(json);

    BOOST_CHECK_EQUAL(restored.name.toStdString(), original.name.toStdString());
    BOOST_CHECK_CLOSE(restored.settings.size, original.settings.size, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.hardness, original.settings.hardness, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.opacity, original.settings.opacity, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.flow, original.settings.flow, 0.001f);
    BOOST_CHECK(restored.settings.color == original.settings.color);
    BOOST_CHECK(restored.settings.type == original.settings.type);
    BOOST_CHECK(restored.settings.mode == original.settings.mode);
    BOOST_CHECK(restored.settings.tipSource == original.settings.tipSource);
    BOOST_CHECK_EQUAL(restored.settings.tipImagePath.toStdString(), original.settings.tipImagePath.toStdString());
    BOOST_CHECK(restored.settings.blendMode == original.settings.blendMode);
    BOOST_CHECK(restored.settings.smoothingMode == original.settings.smoothingMode);
    BOOST_CHECK_CLOSE(restored.settings.smoothingRadius, original.settings.smoothingRadius, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.spacing, original.settings.spacing, 0.001f);
    BOOST_CHECK_EQUAL(restored.settings.airbrush, original.settings.airbrush);
    BOOST_CHECK_EQUAL(restored.settings.wetEdges, original.settings.wetEdges);
}

BOOST_AUTO_TEST_CASE(roundtrip_preserves_shape_dynamics)
{
    BrushPreset original;
    original.name = "ShapeDyn";
    auto& sd = original.settings.shapeDynamics;
    sd.enabled = true;
    sd.sizeJitter = {0.5f, DynamicsSource::Pressure};
    sd.sizeMinRatio = 0.2f;
    sd.angleJitter = {0.3f, DynamicsSource::Random};
    sd.roundnessJitter = {0.1f, DynamicsSource::Tilt};
    sd.roundnessMin = 0.5f;

    auto restored = BrushPreset::fromJson(original.toJson());
    BOOST_CHECK(restored.settings.shapeDynamics.enabled);
    BOOST_CHECK_CLOSE(restored.settings.shapeDynamics.sizeJitter.amount, 0.5f, 0.001f);
    BOOST_CHECK(restored.settings.shapeDynamics.sizeJitter.source == DynamicsSource::Pressure);
    BOOST_CHECK_CLOSE(restored.settings.shapeDynamics.sizeMinRatio, 0.2f, 0.001f);
    BOOST_CHECK(restored.settings.shapeDynamics.angleJitter.source == DynamicsSource::Random);
    BOOST_CHECK(restored.settings.shapeDynamics.roundnessJitter.source == DynamicsSource::Tilt);
    BOOST_CHECK_CLOSE(restored.settings.shapeDynamics.roundnessMin, 0.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(roundtrip_preserves_scatter_dynamics)
{
    BrushPreset original;
    original.name = "ScatterDyn";
    auto& sd = original.settings.scatterDynamics;
    sd.enabled = true;
    sd.amount = 50;
    sd.bothAxes = true;
    sd.count = 5;
    sd.countJitter = {0.7f, DynamicsSource::Fade};

    auto restored = BrushPreset::fromJson(original.toJson());
    BOOST_CHECK(restored.settings.scatterDynamics.enabled);
    BOOST_CHECK_CLOSE(restored.settings.scatterDynamics.amount, 50, 0.001f);
    BOOST_CHECK(restored.settings.scatterDynamics.bothAxes);
    BOOST_CHECK_EQUAL(restored.settings.scatterDynamics.count, 5);
    BOOST_CHECK(restored.settings.scatterDynamics.countJitter.source == DynamicsSource::Fade);
}

BOOST_AUTO_TEST_CASE(roundtrip_preserves_color_dynamics)
{
    BrushPreset original;
    original.name = "ColorDyn";
    auto& cd = original.settings.colorDynamics;
    cd.enabled = true;
    cd.hueJitter = {15, DynamicsSource::Velocity};
    cd.saturationJitter = {20, DynamicsSource::Direction};
    cd.brightnessJitter = {10, DynamicsSource::Random};
    cd.purityJitter = {5, DynamicsSource::Pressure};

    auto restored = BrushPreset::fromJson(original.toJson());
    BOOST_CHECK(restored.settings.colorDynamics.enabled);
    BOOST_CHECK_CLOSE(restored.settings.colorDynamics.hueJitter.amount, 15, 0.001f);
    BOOST_CHECK(restored.settings.colorDynamics.saturationJitter.source == DynamicsSource::Direction);
    BOOST_CHECK_CLOSE(restored.settings.colorDynamics.purityJitter.amount, 5, 0.001f);
}

BOOST_AUTO_TEST_CASE(roundtrip_preserves_texture_config)
{
    BrushPreset original;
    original.name = "TexCfg";
    auto& tc = original.settings.textureConfig;
    tc.enabled = true;
    tc.scale = 3;
    tc.brightness = 0.7f;
    tc.contrast = 0.4f;
    tc.invert = true;
    tc.mode = TextureConfig::Mode::RandomOffset;
    tc.depth = 0.9f;

    auto restored = BrushPreset::fromJson(original.toJson());
    BOOST_CHECK(restored.settings.textureConfig.enabled);
    BOOST_CHECK_CLOSE(restored.settings.textureConfig.scale, 3, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.textureConfig.contrast, 0.4f, 0.001f);
    BOOST_CHECK(restored.settings.textureConfig.invert);
    BOOST_CHECK(restored.settings.textureConfig.mode == TextureConfig::Mode::RandomOffset);
    BOOST_CHECK_CLOSE(restored.settings.textureConfig.depth, 0.9f, 0.001f);
}

BOOST_AUTO_TEST_CASE(roundtrip_preserves_dual_brush_config)
{
    BrushPreset original;
    original.name = "DualBrush";
    auto& db = original.settings.dualBrushConfig;
    db.enabled = true;
    db.tipType = 1;
    db.size = 50;
    db.spacing = 3;
    db.scatter = 8;
    db.count = 4;
    db.hardness = 0.5f;

    auto restored = BrushPreset::fromJson(original.toJson());
    BOOST_CHECK(restored.settings.dualBrushConfig.enabled);
    BOOST_CHECK_EQUAL(restored.settings.dualBrushConfig.tipType, 1);
    BOOST_CHECK_CLOSE(restored.settings.dualBrushConfig.size, 50, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.dualBrushConfig.spacing, 3, 0.001f);
    BOOST_CHECK_CLOSE(restored.settings.dualBrushConfig.scatter, 8, 0.001f);
    BOOST_CHECK_EQUAL(restored.settings.dualBrushConfig.count, 4);
    BOOST_CHECK_CLOSE(restored.settings.dualBrushConfig.hardness, 0.5f, 0.001f);
}

BOOST_AUTO_TEST_SUITE_END()
