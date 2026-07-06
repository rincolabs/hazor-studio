#include "ai/inpaint/AiDirectInpaintModel.hpp"

#include "ai/onnx/OnnxSessionHandle.hpp"

#include <QDebug>
#include <QDir>
#include <QObject>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct FloatStats {
    float min = 0.0f;
    float max = 0.0f;
    double mean = 0.0;
    int nonFinite = 0;
};

FloatStats statsFor(const std::vector<float>& data)
{
    FloatStats stats;
    if (data.empty())
        return stats;

    stats.min = std::numeric_limits<float>::max();
    stats.max = std::numeric_limits<float>::lowest();
    double sum = 0.0;
    int count = 0;
    for (float v : data) {
        if (!std::isfinite(v)) {
            ++stats.nonFinite;
            continue;
        }
        stats.min = std::min(stats.min, v);
        stats.max = std::max(stats.max, v);
        sum += v;
        ++count;
    }
    if (count > 0)
        stats.mean = sum / count;
    else
        stats.min = stats.max = 0.0f;
    return stats;
}

int nonZeroCount(const std::vector<float>& data)
{
    int count = 0;
    for (float v : data)
        if (v > 0.0f)
            ++count;
    return count;
}

QString shapeText(const std::vector<int64_t>& shape)
{
    QStringList parts;
    for (int64_t dim : shape)
        parts << QString::number(dim);
    return parts.join(QLatin1Char('x'));
}

QImage imageFromNchw(const std::vector<float>& data, int w, int h,
                     bool byteRange, bool signedUnitRange)
{
    if (data.size() < static_cast<size_t>(3) * w * h)
        return {};
    QImage out(w, h, QImage::Format_RGBA8888);
    const size_t plane = static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            auto toByte = [byteRange, signedUnitRange](float v) {
                if (byteRange)
                    return std::clamp(static_cast<int>(std::lround(v)), 0, 255);
                if (signedUnitRange)
                    return std::clamp(static_cast<int>(std::lround((v * 0.5f + 0.5f) * 255.0f)), 0, 255);
                return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
            };
            row[x] = qRgba(toByte(data[idx]),
                           toByte(data[plane + idx]),
                           toByte(data[plane * 2 + idx]),
                           255);
        }
    }
    return out;
}

QImage maskFromData(const std::vector<float>& data, int w, int h)
{
    if (data.size() < static_cast<size_t>(w) * h)
        return {};
    QImage out(w, h, QImage::Format_Grayscale8);
    const FloatStats stats = statsFor(data);
    const bool byteRange = stats.max > 2.0f;
    for (int y = 0; y < h; ++y) {
        uchar* row = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const float v = byteRange ? data[idx] : data[idx] * 255.0f;
            row[x] = static_cast<uchar>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
        }
    }
    return out;
}

} // namespace

AiDirectInpaintModel::AiDirectInpaintModel(std::shared_ptr<AiInferenceSession> session,
                                           AiModelDescriptor descriptor)
    : m_session(std::move(session))
    , m_descriptor(std::move(descriptor))
{
}

QString AiDirectInpaintModel::providerUsed() const
{
    return m_session ? m_session->providerUsed() : QString();
}

QString AiDirectInpaintModel::inputName(const QString& key, const QString& fallback) const
{
    const QVariantMap names = m_descriptor.input.value(QStringLiteral("inputNames")).toMap();
    const QString v = names.value(key).toString();
    return v.isEmpty() ? fallback : v;
}

QString AiDirectInpaintModel::outputName(const QString& key, const QString& fallback) const
{
    const QVariantMap outputs = m_descriptor.input.value(QStringLiteral("outputNames")).toMap();
    const QString v = outputs.value(key).toString();
    return v.isEmpty() ? fallback : v;
}

QImage AiDirectInpaintModel::run(const QImage& image, const QImage& mask,
                                 const AiRemoveOptions&,
                                 QString* error,
                                 std::atomic_bool* cancel,
                                 const QString& debugDir)
{
    if (cancel && cancel->load())
        return {};
    if (!m_session || !m_session->hasHandle(QStringLiteral("model"))) {
        if (error) *error = QObject::tr("Inpainting model session is not available.");
        return {};
    }

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    const QImage mask8 = mask.convertToFormat(QImage::Format_Grayscale8);
    if (rgba.isNull() || mask8.isNull() || rgba.size() != mask8.size()) {
        if (error) *error = QObject::tr("Invalid inpainting input.");
        return {};
    }

    const QString imageRange = m_descriptor.input.value(QStringLiteral("imageRange")).toString().trimmed();
    const bool inputIsByteRange = imageRange == QLatin1String("0..255")
        || imageRange == QLatin1String("uint8");
    const bool inputIsSignedUnitRange = imageRange == QLatin1String("-1..1");
    const bool maskImage = m_descriptor.input.value(QStringLiteral("maskImage"), false).toBool();
    const bool invertMask = m_descriptor.input.value(QStringLiteral("invertMask"), false).toBool();
    const int maskedFill = std::clamp(m_descriptor.input.value(QStringLiteral("maskedFill"), 0).toInt(), 0, 255);
    const int w = rgba.width();
    const int h = rgba.height();
    std::vector<float> imageData(static_cast<size_t>(3) * w * h);
    std::vector<float> maskData(static_cast<size_t>(w) * h);

    auto imageValue = [inputIsByteRange, inputIsSignedUnitRange](int channel) {
        if (inputIsByteRange)
            return static_cast<float>(channel);
        const float unit = channel / 255.0f;
        if (inputIsSignedUnitRange)
            return unit * 2.0f - 1.0f;
        return unit;
    };
    const float fillValue = imageValue(maskedFill);

    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
        const uchar* maskRow = mask8.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            float maskValue = maskRow[x] / 255.0f;
            if (invertMask)
                maskValue = 1.0f - maskValue;
            maskData[idx] = maskValue;

            const float invMask = 1.0f - maskValue;
            auto maybeMasked = [maskImage, maskValue, invMask, fillValue](float value) {
                return maskImage ? value * invMask + fillValue * maskValue : value;
            };
            imageData[idx] = maybeMasked(imageValue(qRed(row[x])));
            imageData[static_cast<size_t>(w) * h + idx] = maybeMasked(imageValue(qGreen(row[x])));
            imageData[static_cast<size_t>(2) * w * h + idx] = maybeMasked(imageValue(qBlue(row[x])));
        }
    }

    const FloatStats imageStats = statsFor(imageData);
    const FloatStats maskStats = statsFor(maskData);
    qInfo().noquote() << "[AI][INPAINT] Direct input"
                      << "model=" << m_descriptor.id
                      << "size=" << QSize(w, h)
                      << "image[min/max/mean]=" << imageStats.min << imageStats.max << imageStats.mean
                      << "mask[min/max/mean/nonzero]=" << maskStats.min << maskStats.max
                      << maskStats.mean << nonZeroCount(maskData)
                      << "imageRange=" << (imageRange.isEmpty() ? QStringLiteral("0..1") : imageRange)
                      << "maskImage=" << maskImage
                      << "invertMask=" << invertMask
                      << "maskedFill=" << maskedFill
                      << "inputNames=" << inputName(QStringLiteral("image"), QStringLiteral("image"))
                      << inputName(QStringLiteral("mask"), QStringLiteral("mask"));

    if (!debugDir.isEmpty()) {
        QDir dir(debugDir);
        dir.mkpath(QStringLiteral("."));
        imageFromNchw(imageData, w, h, inputIsByteRange, inputIsSignedUnitRange)
            .save(dir.filePath(QStringLiteral("03b-actual-model-input.png")));
        maskFromData(maskData, w, h)
            .save(dir.filePath(QStringLiteral("04b-actual-model-mask.png")));
    }

    std::vector<OnnxTensorData> inputs;
    inputs.push_back({inputName(QStringLiteral("image"), QStringLiteral("image")),
                      {1, 3, h, w}, std::move(imageData)});
    inputs.push_back({inputName(QStringLiteral("mask"), QStringLiteral("mask")),
                      {1, 1, h, w}, std::move(maskData)});

    std::vector<OnnxTensorData> outputs;
    const QString outName = outputName(QStringLiteral("image"), QStringLiteral("output"));
    auto handle = m_session->handle(QStringLiteral("model"));
    if (!handle->run(inputs, QStringList{outName}, outputs, error))
        return {};
    if (cancel && cancel->load())
        return {};
    if (outputs.empty() || outputs.front().data.empty()) {
        if (error) *error = QObject::tr("Inpainting model produced no image.");
        return {};
    }

    const OnnxTensorData& out = outputs.front();
    const FloatStats outputStats = statsFor(out.data);
    qInfo().noquote() << "[AI][INPAINT] Direct output"
                      << "model=" << m_descriptor.id
                      << "shape=" << shapeText(out.shape)
                      << "data=" << out.data.size()
                      << "min/max/mean/nonFinite=" << outputStats.min << outputStats.max
                      << outputStats.mean << outputStats.nonFinite
                      << "outputName=" << outName;
    if (out.shape.size() < 4) {
        if (error) *error = QObject::tr("Inpainting model output has an unsupported shape.");
        return {};
    }
    const int oh = static_cast<int>(out.shape[out.shape.size() - 2]);
    const int ow = static_cast<int>(out.shape[out.shape.size() - 1]);
    const int channels = static_cast<int>(out.shape[out.shape.size() - 3]);
    if (ow <= 0 || oh <= 0 || channels < 3
        || out.data.size() < static_cast<size_t>(channels) * ow * oh) {
        if (error) *error = QObject::tr("Inpainting model output is incomplete.");
        return {};
    }

    const bool outputIsByteRange = outputStats.max > 2.0f;
    const bool outputIsSignedUnitRange = outputStats.min < -0.01f && outputStats.max <= 1.01f;
    qInfo().noquote() << "[AI][INPAINT] Direct output decode"
                      << "model=" << m_descriptor.id
                      << "range=" << (outputIsByteRange ? QStringLiteral("0..255")
                                      : outputIsSignedUnitRange ? QStringLiteral("-1..1")
                                                                : QStringLiteral("0..1"));

    QImage result(ow, oh, QImage::Format_RGBA8888);
    const size_t plane = static_cast<size_t>(ow) * oh;
    for (int y = 0; y < oh; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < ow; ++x) {
            const size_t idx = static_cast<size_t>(y) * ow + x;
            auto toByte = [outputIsByteRange, outputIsSignedUnitRange](float v) {
                if (outputIsByteRange)
                    return std::clamp(static_cast<int>(std::lround(v)), 0, 255);
                if (outputIsSignedUnitRange)
                    return std::clamp(static_cast<int>(std::lround((v * 0.5f + 0.5f) * 255.0f)), 0, 255);
                return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
            };
            row[x] = qRgba(toByte(out.data[idx]),
                           toByte(out.data[plane + idx]),
                           toByte(out.data[plane * 2 + idx]),
                           255);
        }
    }
    return result;
}
