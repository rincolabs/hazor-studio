#include "HistogramGenerator.hpp"

#include <QImage>
#include <cmath>

HistogramSampleLevel HistogramGenerator::chooseLevel(int width, int height, const Options& opts)
{
    if (!opts.autoDownsample || opts.maxSampledPixels <= 0)
        return opts.sampleLevel;

    const long long pixels = static_cast<long long>(width) * height;
    if (pixels <= opts.maxSampledPixels)        return HistogramSampleLevel::Full;
    if (pixels <= opts.maxSampledPixels * 4LL)  return HistogramSampleLevel::Half;
    if (pixels <= opts.maxSampledPixels * 16LL) return HistogramSampleLevel::Quarter;
    return HistogramSampleLevel::Eighth;
}

HistogramData HistogramGenerator::generate(const QImage& imageIn,
                                           const QImage* maskIn,
                                           const Options& opts)
{
    HistogramData data;
    if (imageIn.isNull() || imageIn.width() <= 0 || imageIn.height() <= 0)
        return data; // valid == false

    // Normalize to a known 32-bit straight-alpha layout so byte access is
    // predictable. Premultiplied input is converted so RGB stays un-scaled.
    QImage image = imageIn;
    if (image.format() != QImage::Format_ARGB32 &&
        image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }
    const bool rgbaOrder = (image.format() == QImage::Format_RGBA8888);

    const int w = image.width();
    const int h = image.height();

    const HistogramSampleLevel level =
        (opts.sampleLevel == HistogramSampleLevel::Full)
            ? chooseLevel(w, h, opts)
            : opts.sampleLevel;
    const int stride = sampleStride(level);
    data.sampleLevel = level;

    // Optional selection mask, normalized to Grayscale8 at matching size.
    QImage mask;
    bool useMask = false;
    if (maskIn && !maskIn->isNull() && maskIn->size() == image.size()) {
        mask = *maskIn;
        if (mask.format() != QImage::Format_Grayscale8)
            mask = mask.convertToFormat(QImage::Format_Grayscale8);
        useMask = true;
    }

    int count = 0;
    for (int y = 0; y < h; y += stride) {
        const uchar* line = image.constScanLine(y);
        const uchar* maskLine = useMask ? mask.constScanLine(y) : nullptr;
        for (int x = 0; x < w; x += stride) {
            if (maskLine && maskLine[x] == 0)
                continue;

            const uchar* px = line + static_cast<size_t>(x) * 4;
            int r, g, b, a;
            if (rgbaOrder) {
                r = px[0]; g = px[1]; b = px[2]; a = px[3];
            } else {
                // ARGB32 little-endian byte order is B,G,R,A.
                b = px[0]; g = px[1]; r = px[2]; a = px[3];
            }

            if (opts.skipTransparent && a == 0)
                continue;

            ++data.red[r];
            ++data.green[g];
            ++data.blue[b];

            // Rec. 709 perceptual luminance.
            const int lum = static_cast<int>(
                std::lround(0.2126 * r + 0.7152 * g + 0.0722 * b));
            ++data.luminance[lum < 0 ? 0 : (lum > 255 ? 255 : lum)];

            ++count;
        }
    }

    data.pixelCount = count;
    const HistogramStats s = statsForBins(data.luminance);
    data.mean = s.mean;
    data.median = s.median;
    data.stdDev = s.stdDev;
    data.valid = true;
    return data;
}

HistogramStats HistogramGenerator::statsForBins(const std::array<int, 256>& bins)
{
    HistogramStats st;
    long long total = 0;
    double sum = 0.0;
    for (int i = 0; i < 256; ++i) {
        total += bins[i];
        sum += static_cast<double>(i) * bins[i];
    }
    st.total = static_cast<int>(total);
    if (total == 0)
        return st;

    const double mean = sum / static_cast<double>(total);
    st.mean = static_cast<float>(mean);

    double varAcc = 0.0;
    for (int i = 0; i < 256; ++i) {
        const double d = static_cast<double>(i) - mean;
        varAcc += d * d * bins[i];
    }
    st.stdDev = static_cast<float>(std::sqrt(varAcc / static_cast<double>(total)));

    // Median = level where the running count crosses half the population.
    const long long half = (total + 1) / 2;
    long long acc = 0;
    for (int i = 0; i < 256; ++i) {
        acc += bins[i];
        if (acc >= half) {
            st.median = static_cast<float>(i);
            break;
        }
    }
    return st;
}

HistogramStats HistogramGenerator::statsFor(const HistogramData& data, HistogramChannel ch)
{
    return statsForBins(data.bins(ch));
}
