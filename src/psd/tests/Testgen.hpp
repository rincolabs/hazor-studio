#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "psd/Blend.hpp"
#include "psd/Compression.hpp"
#include "psd/Descriptor.hpp"
#include "psd/File.hpp"
#include "psd/Header.hpp"

// A deterministic synthetic PSD/PSB generator for tests. Test-support only
// (not part of the production library). See PROVENANCE.md for origin
// details.

namespace psd::testgen {

// A named generated file.
struct Case {
    std::string name;
    PsdFile file;
};

// Deterministic pattern of planar samples for a w x h plane.
std::vector<uint8_t> patternPlane(size_t w, size_t h, uint16_t depth, uint32_t seed);

// Number of channels used for a color mode in generated files.
uint16_t modeChannels(ColorMode mode);
// Valid depths for a color mode.
std::vector<uint16_t> modeDepths(ColorMode mode);
// All color modes covered. A function (not a plain extern global) to avoid
// a static-initialization-order fiasco: the ColorMode::* values it lists are
// themselves statics defined in another translation unit (Header.cpp), so a
// plain global copy-initialized from them here could run before those do.
// This is instead a function-local static, lazily built on first call
// (always after all static init has completed).
const std::array<ColorMode, 8>& modes();

// A flattened file (no layer section) with a patterned merged image.
PsdFile mergedOnly(Version version, ColorMode mode, uint16_t depth, Compression compression, uint32_t w, uint32_t h);

// A sample versioned descriptor as used by lfx2/SoCo-style blocks.
VersionedDescriptor sampleDescriptor();

// A layered file exercising masks, nested groups, blend modes, names, empty
// layers, negative offsets and many tagged blocks.
PsdFile layered(Version version, ColorMode mode, uint16_t depth, Compression compression);

// A tiny layered RGB file (useful for truncation sweeps).
PsdFile small(Version version, Compression compression);

// Every generated case.
std::vector<Case> allCases();

} // namespace psd::testgen
