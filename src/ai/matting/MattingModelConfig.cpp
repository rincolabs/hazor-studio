#include "ai/matting/MattingModelConfig.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

MattingModelConfig MattingModelConfig::defaultsForFamily(const QString& family)
{
    MattingModelConfig c;
    const QString f = family.toLower();

    if (f == QLatin1String("birefnet")) {
        // BiRefNet: 1024² stretched, ImageNet normalisation, logits → sigmoid.
        c.inputSize = 1024;
        c.keepAspectRatio = false;
        c.meanR = 0.485f; c.meanG = 0.456f; c.meanB = 0.406f;
        c.stdR  = 0.229f; c.stdG  = 0.224f; c.stdB  = 0.225f;
        c.applySigmoid = true;
        c.minMaxNormalize = false;
    } else if (f == QLatin1String("modnet")) {
        // MODNet: 512², keep aspect (multiple of 32), [-1,1] normalisation,
        // output is already a [0,1] alpha.
        c.inputSize = 512;
        c.keepAspectRatio = true;
        c.meanR = c.meanG = c.meanB = 0.5f;
        c.stdR  = c.stdG  = c.stdB  = 0.5f;
        c.applySigmoid = false;
        c.minMaxNormalize = false;
    } else if (f == QLatin1String("rmbg")) {
        // RMBG 1.4: 1024² stretched, /255 then mean 0.5 std 1.0, output min-max
        // normalised to [0,1].
        c.inputSize = 1024;
        c.keepAspectRatio = false;
        c.meanR = c.meanG = c.meanB = 0.5f;
        c.stdR  = c.stdG  = c.stdB  = 1.0f;
        c.applySigmoid = false;
        c.minMaxNormalize = true;
    } else {
        // Generic matting fallback.
        c.inputSize = 1024;
        c.keepAspectRatio = true;
        c.meanR = c.meanG = c.meanB = 0.5f;
        c.stdR  = c.stdG  = c.stdB  = 0.5f;
        c.applySigmoid = true;
        c.minMaxNormalize = false;
    }
    return c;
}

MattingModelConfig MattingModelConfig::load(const QString& modelDir, const QString& family)
{
    MattingModelConfig c = defaultsForFamily(family);

    const QString path = QDir(modelDir).filePath(QStringLiteral("config.json"));
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return c;

    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty())
        return c;

    auto readInt = [&](const char* k, int& v) {
        if (o.contains(QLatin1String(k))) v = o.value(QLatin1String(k)).toInt(v);
    };
    auto readBool = [&](const char* k, bool& v) {
        if (o.contains(QLatin1String(k))) v = o.value(QLatin1String(k)).toBool(v);
    };
    auto readF = [&](const char* k, float& v) {
        if (o.contains(QLatin1String(k))) v = float(o.value(QLatin1String(k)).toDouble(v));
    };
    auto readStr = [&](const char* k, QString& v) {
        if (o.contains(QLatin1String(k))) v = o.value(QLatin1String(k)).toString(v);
    };

    readInt("input_size", c.inputSize);
    readBool("keep_aspect_ratio", c.keepAspectRatio);
    readF("mean_r", c.meanR); readF("mean_g", c.meanG); readF("mean_b", c.meanB);
    readF("std_r", c.stdR);   readF("std_g", c.stdG);   readF("std_b", c.stdB);
    readBool("apply_sigmoid", c.applySigmoid);
    readBool("min_max_normalize", c.minMaxNormalize);
    readStr("input_name", c.inputName);
    readStr("output_name", c.outputName);

    if (c.inputSize <= 0)
        c.inputSize = 1024;
    return c;
}
