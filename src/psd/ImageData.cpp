#include "ImageData.hpp"

namespace psd {

PlaneLayout ImageData::layout(const Header& header) {
    PlaneLayout l;
    l.planes = static_cast<size_t>(header.channels);
    l.width = static_cast<size_t>(header.width);
    l.height = static_cast<size_t>(header.height);
    l.depth = header.depth;
    l.version = header.version;
    return l;
}

Result<ImageData> ImageData::encode(Compression compression, std::span<const uint8_t> decoded, const Header& header) {
    PSD_TRY_ASSIGN(auto data, encodePlanes(compression, decoded, layout(header)));
    return ImageData{compression, std::move(data)};
}

Result<std::vector<uint8_t>> ImageData::decode(const Header& header) const { return decodePlanes(compression, data, layout(header)); }

Result<ImageData> ImageData::read(Reader& r, const Header& header) {
    PSD_TRY_ASSIGN(auto compressionRaw, r.u16());
    Compression compression = Compression::fromU16(compressionRaw);
    auto rest = r.peekRest();
    PSD_TRY(validatePlanes(compression, rest, layout(header)));
    std::vector<uint8_t> data(rest.begin(), rest.end());
    PSD_TRY(r.skip(data.size()));
    return ImageData{compression, std::move(data)};
}

} // namespace psd
