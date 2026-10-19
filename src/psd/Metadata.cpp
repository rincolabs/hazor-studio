#include "Metadata.hpp"

namespace psd {

namespace {
// Preallocation cap shared in spirit with descriptor.rs's vec_for_count.
constexpr size_t kMaxPrealloc = 4096;
} // namespace

MetadataItem MetadataItem::make(std::array<uint8_t, 4> key, std::vector<uint8_t> data) {
    if (data.size() % 2 == 1) data.push_back(0);
    MetadataItem it;
    it.signature = {'8', 'B', 'I', 'M'};
    it.key = key;
    it.copy = false;
    it.reserved = {0, 0, 0};
    it.data = std::move(data);
    return it;
}

std::ostream& operator<<(std::ostream& os, const MetadataItem& it) {
    return os << "MetadataItem{key=\"" << utf8BytesToStringLossy(it.key) << "\", copy=" << it.copy << ", n=" << it.data.size() << "}";
}

Result<std::vector<MetadataItem>> parseShmd(std::span<const uint8_t> data) {
    Reader r(data);
    PSD_TRY_ASSIGN(auto count, r.u32());
    PSD_TRY(r.checkCount(count, 16));
    std::vector<MetadataItem> out;
    out.reserve(std::min<size_t>(count, kMaxPrealloc));
    for (uint32_t i = 0; i < count; ++i) {
        MetadataItem it;
        PSD_TRY_ASSIGN(it.signature, r.array<4>());
        PSD_TRY_ASSIGN(it.key, r.array<4>());
        PSD_TRY_ASSIGN(auto copy, r.u8());
        it.copy = copy != 0;
        PSD_TRY_ASSIGN(it.reserved, r.array<3>());
        PSD_TRY_ASSIGN(auto len, r.u32());
        PSD_TRY_ASSIGN(auto itemData, r.bytesU64(len));
        it.data = std::vector<uint8_t>(itemData.begin(), itemData.end());
        out.push_back(std::move(it));
    }
    if (!r.isEmpty()) return PsdError::invalid("trailing bytes after metadata items");
    return out;
}

std::vector<uint8_t> writeShmd(const std::vector<MetadataItem>& items) {
    std::vector<uint8_t> out;
    putU32(out, static_cast<uint32_t>(items.size()));
    for (const auto& it : items) {
        put(out, it.signature);
        put(out, it.key);
        putU8(out, it.copy ? 1 : 0);
        put(out, it.reserved);
        putU32(out, static_cast<uint32_t>(it.data.size()));
        put(out, it.data);
    }
    return out;
}

} // namespace psd
