#pragma once

#include <array>
#include <cstdint>

// ── Channel display modes ────────────────────────────────────────────────────
enum class HistogramChannel {
    Colors,      // RGB channels blended additively (cyan/magenta/yellow/white overlaps)
    RGB,         // combined luminance, neutral white fill
    Red,
    Green,
    Blue,
    Luminosity   // perceptual luminance (Rec. 709)
};

// ── Pixel source (architecture-ready; UI initially exposes Entire Image) ─────
enum class HistogramSource {
    EntireImage,
    VisibleLayers,
    ActiveLayer,
    SelectionOnly,
    ViewportOnly
};

// ── Downsample level for large images ────────────────────────────────────────
enum class HistogramSampleLevel {
    Full,     // 1:1
    Half,     // 1/2 per axis  (1/4 of pixels)
    Quarter,  // 1/4 per axis  (1/16 of pixels)
    Eighth    // 1/8 per axis  (1/64 of pixels)
};

// Stride (pixel step on each axis) for a given sample level.
inline int sampleStride(HistogramSampleLevel level)
{
    switch (level) {
        case HistogramSampleLevel::Full:    return 1;
        case HistogramSampleLevel::Half:    return 2;
        case HistogramSampleLevel::Quarter: return 4;
        case HistogramSampleLevel::Eighth:  return 8;
    }
    return 1;
}

// Per-channel statistics derived from a 256-bin histogram.
struct HistogramStats {
    float mean = 0.0f;
    float median = 0.0f;
    float stdDev = 0.0f;
    int   total = 0;
};

// ── Raw histogram payload ────────────────────────────────────────────────────
struct HistogramData {
    std::array<int, 256> red{};
    std::array<int, 256> green{};
    std::array<int, 256> blue{};
    std::array<int, 256> luminance{};

    int pixelCount = 0;

    // Statistics for the luminance channel (spec-default).
    float mean = 0.0f;
    float median = 0.0f;
    float stdDev = 0.0f;

    HistogramSampleLevel sampleLevel = HistogramSampleLevel::Full;
    bool valid = false;

    void clear()
    {
        red.fill(0);
        green.fill(0);
        blue.fill(0);
        luminance.fill(0);
        pixelCount = 0;
        mean = median = stdDev = 0.0f;
        sampleLevel = HistogramSampleLevel::Full;
        valid = false;
    }

    const std::array<int, 256>& bins(HistogramChannel ch) const
    {
        switch (ch) {
            case HistogramChannel::Red:   return red;
            case HistogramChannel::Green: return green;
            case HistogramChannel::Blue:  return blue;
            case HistogramChannel::RGB:
            case HistogramChannel::Luminosity:
            case HistogramChannel::Colors:
            default:                      return luminance;
        }
    }
};
