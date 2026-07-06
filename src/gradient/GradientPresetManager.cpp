#include "GradientPresetManager.hpp"

#include "core/AppPaths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace {
GradientDefinition makeGradient(const QString& name,
                                std::initializer_list<GradientColorStop> colors,
                                std::initializer_list<GradientOpacityStop> opacities = {
                                    {1.0, 0.0, 0.5},
                                    {1.0, 1.0, 0.5},
                                })
{
    GradientDefinition definition;
    definition.name = name;
    definition.kind = GradientKind::Linear;
    definition.colorStops = QVector<GradientColorStop>(colors);
    definition.opacityStops = QVector<GradientOpacityStop>(opacities);
    definition.smoothness = 1.0;
    definition.dither = true;
    definition.transparency = true;
    definition.interpolation = GradientInterpolationMethod::Perceptual;
    definition.normalize();
    return definition;
}
}

GradientPresetManager::GradientPresetManager(QObject* parent)
    : QObject(parent)
{
    resetToDefaults();
}

GradientDefinition GradientPresetManager::currentGradient() const
{
    return m_current;
}

void GradientPresetManager::setCurrentGradient(const GradientDefinition& definition)
{
    m_current = definition;
    m_current.normalize();
    emit currentGradientChanged(m_current);
}

void GradientPresetManager::setSessionColors(const QColor& foreground, const QColor& background)
{
    m_foreground = foreground;
    m_background = background;
}

bool GradientPresetManager::loadPresets()
{
    const QString path = presetFilePath();
    if (!QFile::exists(path)) {
        const QString legacyPath = QDir(QFileInfo(path).absolutePath())
            .filePath(QStringLiteral("gradient-presets.json"));
        if (QFile::exists(legacyPath)) {
            const bool loaded = loadFromFile(legacyPath);
            if (loaded)
                savePresets();
            return loaded;
        }
        resetToDefaults();
        return savePresets();
    }
    return loadFromFile(path);
}

bool GradientPresetManager::savePresets() const
{
    return saveToFile(presetFilePath());
}

bool GradientPresetManager::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    QVector<GradientDefinition> loaded;
    for (const auto& value : root.value(QStringLiteral("presets")).toArray()) {
        GradientDefinition definition = GradientDefinition::fromJson(value.toObject());
        if (definition.isValid())
            loaded.append(definition);
    }

    if (loaded.isEmpty())
        loaded = defaultPresets(m_foreground, m_background);

    m_presets = loaded;
    setCurrentGradient(m_presets.first());
    emit presetsChanged();
    return true;
}

bool GradientPresetManager::saveToFile(const QString& path) const
{
    QFileInfo info(path);
    if (!info.dir().exists())
        info.dir().mkpath(QStringLiteral("."));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    QJsonArray array;
    for (const auto& preset : m_presets)
        array.append(preset.toJson());
    root.insert(QStringLiteral("presets"), array);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void GradientPresetManager::resetToDefaults()
{
    m_presets = defaultPresets(m_foreground, m_background);
    if (!m_presets.isEmpty())
        m_current = m_presets.first();
    emit presetsChanged();
    emit currentGradientChanged(m_current);
}

void GradientPresetManager::addPreset(const GradientDefinition& definition)
{
    GradientDefinition normalized = definition;
    normalized.normalize();
    if (normalized.name.trimmed().isEmpty())
        normalized.name = tr("Custom Gradient");
    m_presets.append(normalized);
    setCurrentGradient(normalized);
    savePresets();
    emit presetsChanged();
}

void GradientPresetManager::removePreset(int index)
{
    if (index < 0 || index >= m_presets.size())
        return;
    m_presets.removeAt(index);
    if (m_presets.isEmpty())
        m_presets = defaultPresets(m_foreground, m_background);
    setCurrentGradient(m_presets.first());
    savePresets();
    emit presetsChanged();
}

QVector<GradientDefinition> GradientPresetManager::defaultPresets(
    const QColor& foreground,
    const QColor& background)
{
    return {
        makeGradient(QStringLiteral("Black to White"),
                     {{Qt::black, 0.0, 0.5}, {Qt::white, 1.0, 0.5}}),
        makeGradient(QStringLiteral("White to Black"),
                     {{Qt::white, 0.0, 0.5}, {Qt::black, 1.0, 0.5}}),
        makeGradient(QStringLiteral("Transparent to Black"),
                     {{Qt::black, 0.0, 0.5}, {Qt::black, 1.0, 0.5}},
                     {{0.0, 0.0, 0.5}, {1.0, 1.0, 0.5}}),
        makeGradient(QStringLiteral("Transparent to White"),
                     {{Qt::white, 0.0, 0.5}, {Qt::white, 1.0, 0.5}},
                     {{0.0, 0.0, 0.5}, {1.0, 1.0, 0.5}}),
        makeGradient(QStringLiteral("Foreground to Background"),
                     {{foreground, 0.0, 0.5}, {background, 1.0, 0.5}}),
        makeGradient(QStringLiteral("Foreground to Transparent"),
                     {{foreground, 0.0, 0.5}, {foreground, 1.0, 0.5}},
                     {{1.0, 0.0, 0.5}, {0.0, 1.0, 0.5}}),
        makeGradient(QStringLiteral("Blue to Cyan"),
                     {{QColor(20, 78, 220), 0.0, 0.5}, {QColor(48, 225, 255), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Orange to Purple"),
                     {{QColor(255, 136, 24), 0.0, 0.5}, {QColor(126, 49, 210), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Rainbow"),
                     {{QColor(228, 48, 48), 0.0, 0.5},
                      {QColor(244, 170, 36), 0.18, 0.5},
                      {QColor(250, 236, 70), 0.34, 0.5},
                      {QColor(70, 188, 82), 0.50, 0.5},
                      {QColor(40, 135, 230), 0.67, 0.5},
                      {QColor(78, 58, 196), 0.84, 0.5},
                      {QColor(178, 72, 202), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Metal"),
                     {{QColor(42, 45, 49), 0.0, 0.5},
                      {QColor(230, 235, 240), 0.35, 0.35},
                      {QColor(92, 98, 108), 0.52, 0.45},
                      {QColor(245, 248, 250), 0.72, 0.55},
                      {QColor(54, 58, 65), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Chrome"),
                     {{QColor(24, 28, 34), 0.0, 0.5},
                      {QColor(245, 250, 255), 0.22, 0.5},
                      {QColor(88, 104, 122), 0.42, 0.5},
                      {QColor(255, 255, 255), 0.58, 0.5},
                      {QColor(42, 48, 56), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Gold"),
                     {{QColor(84, 50, 14), 0.0, 0.5},
                      {QColor(247, 188, 58), 0.32, 0.5},
                      {QColor(255, 245, 182), 0.55, 0.5},
                      {QColor(181, 102, 20), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Red/Green"),
                     {{QColor(220, 42, 54), 0.0, 0.5}, {QColor(44, 184, 94), 1.0, 0.5}}),
        makeGradient(QStringLiteral("Noise-like (future)"),
                     {{QColor(25, 25, 28), 0.0, 0.5},
                      {QColor(220, 220, 220), 0.22, 0.5},
                      {QColor(76, 76, 82), 0.38, 0.5},
                      {QColor(235, 235, 238), 0.61, 0.5},
                      {QColor(40, 40, 46), 1.0, 0.5}})
    };
}

QString GradientPresetManager::presetFilePath() const
{
    return AppPaths::dataFile(QStringLiteral("gradient-presets.hsgp"));
}
