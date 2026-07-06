#pragma once

#include "HistogramTypes.hpp"

class QImage;

// Reads pixels, builds the 256-bin histograms and derives statistics.
// Stateless / reentrant — safe to run on a worker thread on a detached QImage.
class HistogramGenerator {
public:
    struct Options {
        HistogramSource      source        = HistogramSource::EntireImage;
        HistogramSampleLevel sampleLevel   = HistogramSampleLevel::Full;
        bool                 autoDownsample = true;
        // Pixel budget above which auto-downsample kicks in.
        int                  maxSampledPixels = 2'000'000;
        // Skip fully transparent pixels so empty canvas areas don't pile up at 0.
        bool                 skipTransparent = true;
    };

    // mask: optional Format_Grayscale8 selection mask (same size as image);
    //       nullptr counts every pixel. Pixels with mask value 0 are excluded.
    static HistogramData generate(const QImage& image,
                                  const QImage* mask,
                                  const Options& opts);

    // Per-channel statistics derived purely from a 256-bin histogram (O(256)).
    static HistogramStats statsFor(const HistogramData& data, HistogramChannel ch);

    // Auto-pick a sample level for the given dimensions and pixel budget.
    static HistogramSampleLevel chooseLevel(int width, int height, const Options& opts);

private:
    static HistogramStats statsForBins(const std::array<int, 256>& bins);
};
