#include "ai/sam/SamModelConfig.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString stripQuotes(QString v)
{
    v = v.trimmed();
    if (v.size() >= 2
        && ((v.startsWith('"') && v.endsWith('"'))
            || (v.startsWith('\'') && v.endsWith('\'')))) {
        v = v.mid(1, v.size() - 2);
    }
    return v;
}

void assign(SamModelConfig& cfg, const QString& key, const QString& value)
{
    const QString k = key.trimmed().toLower();
    const QString v = stripQuotes(value);
    if (k == QLatin1String("type"))                cfg.type = v;
    else if (k == QLatin1String("name"))           cfg.name = v;
    else if (k == QLatin1String("display_name"))   cfg.displayName = v;
    else if (k == QLatin1String("encoder_model_path")) cfg.encoderModelPath = v;
    else if (k == QLatin1String("decoder_model_path")) cfg.decoderModelPath = v;
    else if (k == QLatin1String("input_size"))     cfg.inputSize = v.toInt() > 0 ? v.toInt() : cfg.inputSize;
    else if (k == QLatin1String("max_width"))      cfg.maxWidth = v.toInt();
    else if (k == QLatin1String("max_height"))     cfg.maxHeight = v.toInt();
}

} // namespace

SamModelConfig SamModelConfig::parseYaml(const QString& text)
{
    SamModelConfig cfg;
    const QStringList lines = text.split('\n');
    for (QString line : lines) {
        // Drop comments and blanks.
        const int hash = line.indexOf('#');
        if (hash >= 0)
            line = line.left(hash);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        assign(cfg, line.left(colon), line.mid(colon + 1));
    }
    return cfg;
}

SamModelConfig SamModelConfig::parseJson(const QString& text)
{
    SamModelConfig cfg;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
        return cfg;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QJsonValue val = it.value();
        assign(cfg, it.key(), val.isString() ? val.toString()
                                             : QString::number(val.toDouble()));
    }
    return cfg;
}

SamModelConfig SamModelConfig::parseFile(const QString& configPath)
{
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return SamModelConfig{};
    const QString text = QString::fromUtf8(f.readAll());
    if (configPath.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        return parseJson(text);
    return parseYaml(text);
}

QString SamModelConfig::findConfigInDir(const QString& dir)
{
    static const char* kNames[] = { "config.yaml", "config.yml", "config.json" };
    QDir d(dir);
    for (const char* name : kNames) {
        const QString path = d.filePath(QString::fromLatin1(name));
        if (QFile::exists(path))
            return path;
    }
    return QString();
}
