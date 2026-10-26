#pragma once

#include <cstdint>
#include <ostream>
#include <utility>

#include "Error.hpp"
#include "Io.hpp"

// The layer bounding-box `Rect` type, split into its own header because both
// FilterEffects.hpp (and so Tagged.hpp's section-structure checks) and
// Layer.hpp need it — avoiding a Layer <-> Tagged header cycle. See
// PROVENANCE.md for origin details.

namespace psd {

// A rectangle as stored in PSD files (top, left, bottom, right).
struct Rect {
    int32_t top = 0;    // Top edge (inclusive).
    int32_t left = 0;    // Left edge (inclusive).
    int32_t bottom = 0; // Bottom edge (exclusive).
    int32_t right = 0;  // Right edge (exclusive).

    // Rectangle from origin and size.
    static Rect fromXywh(int32_t left, int32_t top, uint32_t width, uint32_t height);
    // Width (may be negative for malformed data).
    int64_t width() const { return int64_t(right) - int64_t(left); }
    // Height (may be negative for malformed data).
    int64_t height() const { return int64_t(bottom) - int64_t(top); }
    // `true` when width or height is zero or negative.
    bool isEmpty() const { return width() <= 0 || height() <= 0; }
    // Validated (width, height) as size_t.
    Result<std::pair<size_t, size_t>> size() const;

    static Result<Rect> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const Rect&) const = default;
};
std::ostream& operator<<(std::ostream& os, const Rect& r);

} // namespace psd
