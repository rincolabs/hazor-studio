#pragma once

#include <cstdint>
#include <vector>

#include "Compression.hpp"
#include "Error.hpp"
#include "Header.hpp"
#include "Io.hpp"

// The merged (composite) image data section. See PROVENANCE.md for origin
// details.

namespace psd {

// The merged image: compression plus the encoded bytes of all channels (to
// the end of the file), kept verbatim.
struct ImageData {
    Compression compression = Compression::Raw;
    std::vector<uint8_t> data;

    // Plane layout for the merged image described by `header`.
    static PlaneLayout layout(const Header& header);

    // Encodes planar data (channels x height x rowBytes).
    static Result<ImageData> encode(Compression compression, std::span<const uint8_t> decoded, const Header& header);

    // Decodes all channels into planar big-endian samples.
    Result<std::vector<uint8_t>> decode(const Header& header) const;

    static Result<ImageData> read(Reader& r, const Header& header);

    bool operator==(const ImageData& other) const { return compression == other.compression && data == other.data; }
    bool operator!=(const ImageData& other) const { return !(*this == other); }
};

} // namespace psd
