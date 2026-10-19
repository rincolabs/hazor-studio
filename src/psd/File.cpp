#include "File.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace psd {

std::ostream& operator<<(std::ostream& os, LayerInfoPlacement::Kind kind) {
    return os << (kind == LayerInfoPlacement::Kind::Section ? "Section" : "GlobalBlock");
}

std::ostream& operator<<(std::ostream& os, const LayerInfoPlacement& p) {
    if (p.kind() == LayerInfoPlacement::Kind::Section) return os << "Section";
    return os << "GlobalBlock{index=" << p.globalBlockInfo().index << "}";
}

namespace {
std::optional<uint16_t> be16At(std::span<const uint8_t> data, size_t at) {
    if (at + 2 > data.size()) return std::nullopt;
    return static_cast<uint16_t>((data[at] << 8) | data[at + 1]);
}
} // namespace

std::optional<uint16_t> GlobalLayerMask::overlayColorSpace() const { return be16At(data, 0); }

std::optional<std::array<uint16_t, 4>> GlobalLayerMask::colorComponents() const {
    auto a = be16At(data, 2), b = be16At(data, 4), c = be16At(data, 6), d = be16At(data, 8);
    if (!a || !b || !c || !d) return std::nullopt;
    return std::array<uint16_t, 4>{*a, *b, *c, *d};
}

std::optional<uint16_t> GlobalLayerMask::opacity() const { return be16At(data, 10); }

std::optional<uint8_t> GlobalLayerMask::kind() const {
    if (data.size() <= 12) return std::nullopt;
    return data[12];
}

std::ostream& operator<<(std::ostream& os, const GlobalLayerMask& m) { return os << "GlobalLayerMask(n=" << m.data.size() << ")"; }

namespace {
constexpr std::array<uint8_t, 4> kLr16Key = {'L', 'r', '1', '6'};
constexpr std::array<uint8_t, 4> kLr32Key = {'L', 'r', '3', '2'};
constexpr std::array<uint8_t, 4> kLayrKey = {'L', 'a', 'y', 'r'};
} // namespace

Result<PsdFile> PsdFile::fromBytes(std::span<const uint8_t> data) {
    auto result = fromBytesAndMerged(data, [](const PsdFile&) { return false; });
    if (!result) return std::move(result).error();
    return std::move(result.value().first);
}

Result<std::pair<PsdFile, std::optional<Result<std::vector<uint8_t>>>>> PsdFile::fromBytesAndMerged(std::span<const uint8_t> data,
                                                                                                      std::function<bool(const PsdFile&)> want) {
    return fromBytesAndMergedRecovering(data, std::move(want), [](const PsdFile&) { return false; });
}

Result<std::pair<PsdFile, std::optional<Result<std::vector<uint8_t>>>>>
PsdFile::fromBytesAndMergedRecovering(std::span<const uint8_t> data, std::function<bool(const PsdFile&)> want, std::function<bool(const PsdFile&)> recover) {
    using PairResult = Result<std::pair<PsdFile, std::optional<Result<std::vector<uint8_t>>>>>;
    Reader r(data);
    PSD_TRY_ASSIGN(auto header, Header::read(r));
    bool psb = isPsb(header.version);
    PSD_TRY_ASSIGN(auto cmdLen, r.u32());
    PSD_TRY_ASSIGN(auto cmdSpan, r.bytesU64(cmdLen));
    std::vector<uint8_t> colorModeData(cmdSpan.begin(), cmdSpan.end());
    PSD_TRY_ASSIGN(auto resources, readResourcesSection(r));

    PSD_TRY_ASSIGN(auto lmLen, r.lenField(psb));
    PSD_TRY_ASSIGN(auto s, r.sub(lmLen));
    std::optional<LayerInfo> layerInfo;
    std::optional<GlobalLayerMask> globalLayerMask;
    std::vector<TaggedBlock> globalBlocks;
    std::vector<uint8_t> layerMaskTrailing;
    LayerInfoPlacement layerInfoPlacement = LayerInfoPlacement::section();

    if (!s.isEmpty()) {
        PSD_TRY_ASSIGN(auto liLen, s.lenField(psb));
        if (liLen > 0) {
            PSD_TRY_ASSIGN(auto liData, s.bytesU64(liLen));
            PSD_TRY_ASSIGN(layerInfo, LayerInfo::readBody(liData, header.version));
        }
        if (s.remaining() >= 4) {
            PSD_TRY_ASSIGN(auto n, s.u32());
            PSD_TRY_ASSIGN(auto gm, s.bytesU64(n));
            globalLayerMask = GlobalLayerMask{std::vector<uint8_t>(gm.begin(), gm.end())};

            std::optional<std::array<uint8_t, 4>> targetKey;
            if (header.depth == 16) targetKey = kLr16Key;
            else if (header.depth == 32) targetKey = kLr32Key;
            bool candidateSeen = targetKey.has_value() ? false : (layerInfo.has_value() && !layerInfo->layers.empty());

            auto consume = [&](size_t index, std::array<uint8_t, 4> signature, std::array<uint8_t, 4> key, std::span<const uint8_t> blockData,
                                const std::optional<std::vector<uint8_t>>& padding) -> bool {
                bool isCandidate = targetKey.has_value() ? key == *targetKey : (key == kLayrKey || key == kLr16Key || key == kLr32Key);
                if (candidateSeen || !isCandidate) return false;
                candidateSeen = true;
                auto info = LayerInfo::readBody(blockData, header.version);
                if (!info) {
                    if (targetKey.has_value()) layerInfo = std::nullopt;
                    return false;
                }
                layerInfo = info.value();
                LayerInfoPlacement::GlobalBlockInfo gbi;
                gbi.index = index;
                gbi.signature = signature;
                gbi.key = key;
                gbi.padding = padding;
                layerInfoPlacement = LayerInfoPlacement::globalBlock(gbi);
                return true;
            };
            PSD_TRY_ASSIGN(auto blocksAndTrailing, readBlocksWith(s, header.version, 4, consume));
            globalBlocks = std::move(blocksAndTrailing.first);
            layerMaskTrailing = std::move(blocksAndTrailing.second);
        } else {
            auto rest = s.peekRest();
            layerMaskTrailing = std::vector<uint8_t>(rest.begin(), rest.end());
        }
    }

    PsdFile file;
    file.header = header;
    file.colorModeData = std::move(colorModeData);
    file.resources = std::move(resources);
    file.layerInfo = std::move(layerInfo);
    file.layerInfoPlacement = layerInfoPlacement;
    file.globalLayerMask = std::move(globalLayerMask);
    file.globalBlocks = std::move(globalBlocks);
    file.layerMaskTrailing = std::move(layerMaskTrailing);
    file.imageData = ImageData{Compression::Raw, {}};

    Reader encoded = r;
    std::optional<Result<std::vector<uint8_t>>> merged;
    Result<void> mergedStatus{};
    if (want(file)) {
        auto outer = file.readMergedDecoded(r);
        if (!outer) {
            mergedStatus = outer.error();
        } else {
            merged = std::move(outer.value());
        }
    } else {
        auto image = ImageData::read(r, file.header);
        if (!image) {
            mergedStatus = image.error();
        } else {
            file.imageData = std::move(image.value());
            merged = std::nullopt;
        }
    }
    if (!mergedStatus) {
        PsdError error = mergedStatus.error();
        if (error.kind() == PsdError::Kind::LimitExceeded || !recover(file)) {
            return PairResult(error);
        }
        auto compRaw = encoded.u16();
        if (!compRaw) return PairResult(compRaw.error());
        Compression compression = Compression::fromU16(compRaw.value());
        auto source = encoded.peekRest();
        std::vector<uint8_t> dataOut(source.begin(), source.end());
        file.imageData = ImageData{compression, std::move(dataOut)};
        merged = Result<std::vector<uint8_t>>(error);
    }
    return std::make_pair(std::move(file), std::move(merged));
}

Result<Result<std::vector<uint8_t>>> PsdFile::readMergedDecoded(Reader& r) {
    using Inner = Result<std::vector<uint8_t>>;
    auto layout = ImageData::layout(header);
    auto compPeek = r;
    PSD_TRY_ASSIGN(auto compRaw, compPeek.u16());
    Compression compression = Compression::fromU16(compRaw);
    bool zip = compression == Compression::Zip || compression == Compression::ZipPrediction;
    PSD_TRY_ASSIGN(auto total, layout.totalBytes());
    if (!zip || total > kMaxDecodedBytes) {
        PSD_TRY_ASSIGN(imageData, ImageData::read(r, header));
        return decodeMerged();
    }
    PSD_TRY(r.u16());
    auto data = r.peekRest();
    auto decoded = decodePlanes(compression, data, layout);
    if (!decoded) {
        PSD_TRY(validatePlanes(compression, data, layout));
    }
    imageData = ImageData{compression, std::vector<uint8_t>(data.begin(), data.end())};
    PSD_TRY(r.skip(data.size()));
    return Inner(decoded);
}

Result<std::vector<uint8_t>> PsdFile::toBytes() const {
    Version v = header.version;
    bool psb = isPsb(v);
    size_t hint = (layerInfo.has_value() ? layerInfo->channelDataLen() : 0) + imageData.data.size();
    std::vector<uint8_t> out;
    out.reserve(hint);
    header.write(out);
    PSD_TRY(putLen(out, colorModeData.size(), false));
    put(out, colorModeData);
    PSD_TRY(writeResourcesSection(out, resources));

    bool inBlock = layerInfoPlacement.kind() == LayerInfoPlacement::Kind::GlobalBlock;
    bool sectionEmpty = !layerInfo.has_value() && !globalLayerMask.has_value() && globalBlocks.empty() && layerMaskTrailing.empty();
    size_t at = beginLen(out, psb);
    if (!sectionEmpty) {
        size_t li = beginLen(out, psb);
        if (layerInfo.has_value() && !inBlock) {
            PSD_TRY(layerInfo->writeBody(out, v));
        }
        PSD_TRY(endLen(out, li, psb));
        bool needMaskField = globalLayerMask.has_value() || !globalBlocks.empty() || (inBlock && layerInfo.has_value());
        if (needMaskField) {
            std::span<const uint8_t> gm = globalLayerMask.has_value() ? std::span<const uint8_t>(globalLayerMask->data) : std::span<const uint8_t>();
            PSD_TRY(putLen(out, gm.size(), false));
            put(out, gm);
        }
        if (layerInfo.has_value() && layerInfoPlacement.kind() == LayerInfoPlacement::Kind::GlobalBlock) {
            const auto& gbi = layerInfoPlacement.globalBlockInfo();
            size_t splitAt = std::min(gbi.index, globalBlocks.size());
            std::vector<TaggedBlock> before(globalBlocks.begin(), globalBlocks.begin() + static_cast<ptrdiff_t>(splitAt));
            std::vector<TaggedBlock> after(globalBlocks.begin() + static_cast<ptrdiff_t>(splitAt), globalBlocks.end());
            PSD_TRY(writeBlocks(out, before, v, 4));
            put(out, gbi.signature);
            put(out, gbi.key);
            bool longLen = usesLongLength(v, gbi.key);
            size_t lenAt = beginLen(out, longLen);
            size_t start = out.size();
            PSD_TRY(layerInfo->writeBody(out, v));
            size_t len = out.size() - start;
            PSD_TRY(endLen(out, lenAt, longLen));
            if (gbi.padding.has_value()) {
                put(out, *gbi.padding);
            } else {
                out.insert(out.end(), (4 - len % 4) % 4, 0u);
            }
            PSD_TRY(writeBlocks(out, after, v, 4));
        } else {
            PSD_TRY(writeBlocks(out, globalBlocks, v, 4));
        }
        put(out, layerMaskTrailing);
    }
    PSD_TRY(endLen(out, at, psb));

    putU16(out, imageData.compression.asU16());
    put(out, imageData.data);
    return out;
}

const std::vector<LayerRecord>& PsdFile::layers() const {
    static const std::vector<LayerRecord> kEmpty;
    return layerInfo.has_value() ? layerInfo->layers : kEmpty;
}

std::vector<LayerRecord>& PsdFile::layersMut() {
    if (!layerInfo.has_value()) layerInfo = LayerInfo{};
    return layerInfo->layers;
}

const ImageResource* PsdFile::resource(uint16_t id) const {
    for (auto& r : resources) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

std::optional<std::pair<const TaggedBlock*, PsdError>> PsdFile::unreadableLayerBlock() const {
    if (layerInfo.has_value()) return std::nullopt;
    const TaggedBlock* block = nullptr;
    for (auto& b : globalBlocks) {
        if (b.key == kLayrKey || b.key == kLr16Key || b.key == kLr32Key) {
            block = &b;
            break;
        }
    }
    if (!block) return std::nullopt;
    auto info = LayerInfo::readBody(block->data, header.version);
    if (info) return std::nullopt;
    return std::make_pair(block, info.error());
}

const TaggedBlock* PsdFile::globalBlock(std::array<uint8_t, 4> key) const {
    for (auto& b : globalBlocks) {
        if (b.key == key) return &b;
    }
    return nullptr;
}

std::optional<std::span<const uint8_t>> PsdFile::iccProfile() const {
    const ImageResource* r = resource(ids::kIccProfile);
    if (!r) return std::nullopt;
    return std::span<const uint8_t>(r->data);
}

std::optional<ResolutionInfo> PsdFile::resolution() const {
    const ImageResource* r = resource(ids::kResolutionInfo);
    if (!r) return std::nullopt;
    auto parsed = r->parsed();
    if (!parsed.has_value() || !bool(*parsed)) return std::nullopt;
    if (parsed->value().kind() != ResourceData::Kind::ResolutionInfo) return std::nullopt;
    return parsed->value().asResolutionInfo();
}

std::optional<bool> PsdFile::hasRealMergedData() const {
    const ImageResource* r = resource(ids::kVersionInfo);
    if (!r) return std::nullopt;
    auto parsed = r->parsed();
    if (!parsed.has_value() || !bool(*parsed)) return std::nullopt;
    if (parsed->value().kind() != ResourceData::Kind::VersionInfo) return std::nullopt;
    return parsed->value().asVersionInfo().hasRealMergedData;
}

Result<std::vector<uint8_t>> PsdFile::decodeMerged() const { return imageData.decode(header); }

bool PsdFile::namesMergedTransparency(uint16_t extra) const {
    static const std::string kName = "Transparency";
    std::vector<std::string> names;
    const ImageResource* r1045 = resource(1045);
    const ImageResource* r1006 = resource(1006);
    if (r1045) {
        size_t at = 0;
        const auto& d = r1045->data;
        while (at + 4 <= d.size()) {
            uint32_t len = (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | uint32_t(d[at + 3]);
            at += 4;
            size_t end = at + static_cast<size_t>(len) * 2;
            if (end > d.size() || end < at) break;
            std::vector<uint16_t> units;
            units.reserve(len);
            for (size_t i = at; i < end; i += 2) units.push_back(static_cast<uint16_t>((d[i] << 8) | d[i + 1]));
            std::string s = utf16ToUtf8Lossy(units);
            while (!s.empty() && s.back() == '\0') s.pop_back();
            names.push_back(s);
            at = end;
        }
    } else if (r1006) {
        size_t at = 0;
        const auto& d = r1006->data;
        while (at < d.size()) {
            uint8_t len = d[at];
            if (at + 1 + len > d.size()) break;
            names.push_back(utf8BytesToStringLossy(std::span<const uint8_t>(d.data() + at + 1, len)));
            at += 1 + len;
        }
    }
    return names.size() == static_cast<size_t>(extra) && !names.empty() && names.front() == kName;
}

bool PsdFile::mergedHasAlpha() const {
    auto color = header.colorMode.colorChannels();
    if (!color.has_value()) return false;
    if (header.channels <= static_cast<uint16_t>(*color)) return false;
    bool marked = std::any_of(globalBlocks.begin(), globalBlocks.end(), [](const TaggedBlock& b) {
        return b.key == std::array<uint8_t, 4>{'M', 't', 'r', 'n'} || b.key == std::array<uint8_t, 4>{'M', 't', '1', '6'} ||
               b.key == std::array<uint8_t, 4>{'M', 't', '3', '2'};
    });
    if (layerInfo.has_value()) {
        bool inBlock = layerInfoPlacement.kind() == LayerInfoPlacement::Kind::GlobalBlock;
        return layerInfo->mergedAlpha || marked || (inBlock && namesMergedTransparency(static_cast<uint16_t>(header.channels - *color)));
    }
    return true;
}

Result<void> PsdFile::validate() const {
    PSD_TRY(header.validate());
    if (header.version == Version::Psd && (header.width > 30000 || header.height > 30000)) {
        return PsdError::limitExceeded("PSD dimensions exceed 30000; use PSB");
    }
    if (header.colorMode == ColorMode::Indexed && colorModeData.size() != 768) {
        return PsdError::invalid("indexed color mode requires a 768-byte palette");
    }
    return {};
}

Result<PsdFile> PsdFile::open(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return PsdError::invalid("could not open file: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromBytes(data);
}

Result<void> PsdFile::save(const std::string& path) const {
    PSD_TRY_ASSIGN(auto bytes, toBytes());
    std::ofstream out(path, std::ios::binary);
    if (!out) return PsdError::invalid("could not open file for writing: " + path);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) return PsdError::invalid("write failed: " + path);
    return {};
}

std::ostream& operator<<(std::ostream& os, const PsdFile& f) { return os << "PsdFile{header=" << f.header << ", layers=" << f.layers().size() << "}"; }

} // namespace psd
