#include "ai/inpaint/StableDiffusionLatentUtils.hpp"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <random>

int64_t StableDiffusionTensor::elementCount() const
{
    if (shape.empty())
        return 0;
    int64_t n = 1;
    for (int64_t dim : shape) {
        if (dim <= 0)
            return 0;
        n *= dim;
    }
    return n;
}

namespace {

StableDiffusionTensor binaryOp(const StableDiffusionTensor& a,
                               const StableDiffusionTensor& b,
                               float bScale)
{
    if (a.shape != b.shape || !a.isValid() || !b.isValid())
        return {};
    StableDiffusionTensor out;
    out.shape = a.shape;
    out.data.resize(a.data.size());
    for (size_t i = 0; i < a.data.size(); ++i)
        out.data[i] = a.data[i] + b.data[i] * bScale;
    return out;
}

int byteFromSignedUnit(float v)
{
    return std::clamp(static_cast<int>(std::lround((v * 0.5f + 0.5f) * 255.0f)), 0, 255);
}

} // namespace

namespace StableDiffusionLatentUtils {

StableDiffusionTensor randomNormal(const std::vector<int64_t>& shape, quint64 seed)
{
    StableDiffusionTensor out;
    out.shape = shape;
    const int64_t count = out.elementCount();
    if (count <= 0)
        return {};
    out.data.resize(static_cast<size_t>(count));
    std::mt19937_64 rng(seed == 0 ? 1 : seed);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    for (float& v : out.data)
        v = dist(rng);
    return out;
}

StableDiffusionTensor zeros(const std::vector<int64_t>& shape)
{
    StableDiffusionTensor out;
    out.shape = shape;
    const int64_t count = out.elementCount();
    if (count <= 0)
        return {};
    out.data.assign(static_cast<size_t>(count), 0.0f);
    return out;
}

StableDiffusionTensor add(const StableDiffusionTensor& a, const StableDiffusionTensor& b)
{
    return binaryOp(a, b, 1.0f);
}

StableDiffusionTensor subtract(const StableDiffusionTensor& a, const StableDiffusionTensor& b)
{
    return binaryOp(a, b, -1.0f);
}

StableDiffusionTensor multiply(const StableDiffusionTensor& a, float scalar)
{
    if (!a.isValid())
        return {};
    StableDiffusionTensor out;
    out.shape = a.shape;
    out.data.resize(a.data.size());
    for (size_t i = 0; i < a.data.size(); ++i)
        out.data[i] = a.data[i] * scalar;
    return out;
}

StableDiffusionTensor addScaled(const StableDiffusionTensor& base,
                                const StableDiffusionTensor& delta,
                                float scale)
{
    return binaryOp(base, delta, scale);
}

StableDiffusionTensor concatChannels(const StableDiffusionTensor& a,
                                     const StableDiffusionTensor& b,
                                     const StableDiffusionTensor& c)
{
    if (!a.isValid() || !b.isValid() || !c.isValid())
        return {};
    if (a.shape.size() != 4 || b.shape.size() != 4 || c.shape.size() != 4)
        return {};
    if (a.shape[0] != b.shape[0] || a.shape[0] != c.shape[0]
        || a.shape[2] != b.shape[2] || a.shape[2] != c.shape[2]
        || a.shape[3] != b.shape[3] || a.shape[3] != c.shape[3]) {
        return {};
    }

    const int64_t n = a.shape[0];
    const int64_t ca = a.shape[1];
    const int64_t cb = b.shape[1];
    const int64_t cc = c.shape[1];
    const int64_t h = a.shape[2];
    const int64_t w = a.shape[3];
    StableDiffusionTensor out;
    out.shape = { n, ca + cb + cc, h, w };
    out.data.resize(static_cast<size_t>(out.elementCount()));

    auto copyTensor = [&](const StableDiffusionTensor& src, int64_t channelOffset) {
        const int64_t srcChannels = src.shape[1];
        const int64_t plane = h * w;
        for (int64_t batch = 0; batch < n; ++batch) {
            for (int64_t ch = 0; ch < srcChannels; ++ch) {
                const size_t srcBase = static_cast<size_t>((batch * srcChannels + ch) * plane);
                const size_t dstBase = static_cast<size_t>((batch * out.shape[1] + channelOffset + ch) * plane);
                std::copy_n(src.data.begin() + srcBase, static_cast<size_t>(plane),
                            out.data.begin() + dstBase);
            }
        }
    };
    copyTensor(a, 0);
    copyTensor(b, ca);
    copyTensor(c, ca + cb);
    return out;
}

StableDiffusionTensor maskedImage(const StableDiffusionTensor& image,
                                  const StableDiffusionTensor& mask)
{
    if (!image.isValid() || !mask.isValid() || image.shape.size() != 4 || mask.shape.size() != 4)
        return {};
    if (image.shape[0] != mask.shape[0] || mask.shape[1] != 1
        || image.shape[2] != mask.shape[2] || image.shape[3] != mask.shape[3]) {
        return {};
    }

    const int64_t channels = image.shape[1];
    const int64_t h = image.shape[2];
    const int64_t w = image.shape[3];
    const int64_t plane = h * w;
    StableDiffusionTensor out;
    out.shape = image.shape;
    out.data.resize(image.data.size());
    for (int64_t ch = 0; ch < channels; ++ch) {
        const size_t channelBase = static_cast<size_t>(ch * plane);
        for (int64_t i = 0; i < plane; ++i) {
            const float keep = 1.0f - std::clamp(mask.data[static_cast<size_t>(i)], 0.0f, 1.0f);
            out.data[channelBase + static_cast<size_t>(i)] = image.data[channelBase + static_cast<size_t>(i)] * keep;
        }
    }
    return out;
}

StableDiffusionTensor resizeMaskToLatents(const StableDiffusionTensor& mask,
                                          const QSize& latentSize)
{
    if (!mask.isValid() || mask.shape.size() != 4 || mask.shape[1] != 1)
        return {};
    const int srcH = static_cast<int>(mask.shape[2]);
    const int srcW = static_cast<int>(mask.shape[3]);
    const int dstW = latentSize.width();
    const int dstH = latentSize.height();
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return {};

    StableDiffusionTensor out;
    out.shape = { 1, 1, dstH, dstW };
    out.data.resize(static_cast<size_t>(out.elementCount()));
    for (int y = 0; y < dstH; ++y) {
        const int sy = std::clamp((y * srcH) / dstH, 0, srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            const int sx = std::clamp((x * srcW) / dstW, 0, srcW - 1);
            out.data[static_cast<size_t>(y) * dstW + x] =
                mask.data[static_cast<size_t>(sy) * srcW + sx];
        }
    }
    return out;
}

StableDiffusionTensor imageToNchw(const QImage& image, const QSize& size)
{
    const QImage scaled = image.convertToFormat(QImage::Format_RGBA8888)
        .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull())
        return {};
    const int w = scaled.width();
    const int h = scaled.height();
    StableDiffusionTensor out;
    out.shape = { 1, 3, h, w };
    out.data.resize(static_cast<size_t>(out.elementCount()));
    const size_t plane = static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            out.data[idx] = qRed(row[x]) / 127.5f - 1.0f;
            out.data[plane + idx] = qGreen(row[x]) / 127.5f - 1.0f;
            out.data[plane * 2 + idx] = qBlue(row[x]) / 127.5f - 1.0f;
        }
    }
    return out;
}

StableDiffusionTensor maskToNchw(const QImage& mask, const QSize& size)
{
    const QImage scaled = mask.convertToFormat(QImage::Format_Grayscale8)
        .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull())
        return {};
    const int w = scaled.width();
    const int h = scaled.height();
    StableDiffusionTensor out;
    out.shape = { 1, 1, h, w };
    out.data.resize(static_cast<size_t>(out.elementCount()));
    for (int y = 0; y < h; ++y) {
        const uchar* row = scaled.constScanLine(y);
        for (int x = 0; x < w; ++x)
            out.data[static_cast<size_t>(y) * w + x] = row[x] / 255.0f;
    }
    return out;
}

QImage nchwToImage(const StableDiffusionTensor& tensor, bool signedUnitRange)
{
    if (!tensor.isValid() || tensor.shape.size() != 4 || tensor.shape[1] < 3)
        return {};
    const int h = static_cast<int>(tensor.shape[2]);
    const int w = static_cast<int>(tensor.shape[3]);
    const size_t plane = static_cast<size_t>(w) * h;
    QImage out(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            auto toByte = [signedUnitRange](float v) {
                if (signedUnitRange)
                    return byteFromSignedUnit(v);
                return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
            };
            row[x] = qRgba(toByte(tensor.data[idx]),
                           toByte(tensor.data[plane + idx]),
                           toByte(tensor.data[plane * 2 + idx]),
                           255);
        }
    }
    return out;
}

} // namespace StableDiffusionLatentUtils
