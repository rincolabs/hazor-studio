#include "Resources.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace psd {

ResolutionInfo ResolutionInfo::fromDpi(double dpi) {
    double scaled = std::round(dpi * 65536.0);
    scaled = std::clamp(scaled, 0.0, static_cast<double>(UINT32_MAX));
    uint32_t f = static_cast<uint32_t>(scaled);
    return ResolutionInfo{f, 1, 1, f, 1, 1};
}

Result<ResolutionInfo> ResolutionInfo::fromBytes(std::span<const uint8_t> b) {
    Reader r(b);
    ResolutionInfo ri;
    PSD_TRY_ASSIGN(ri.hResFixed, r.u32());
    PSD_TRY_ASSIGN(ri.hResUnit, r.u16());
    PSD_TRY_ASSIGN(ri.widthUnit, r.u16());
    PSD_TRY_ASSIGN(ri.vResFixed, r.u32());
    PSD_TRY_ASSIGN(ri.vResUnit, r.u16());
    PSD_TRY_ASSIGN(ri.heightUnit, r.u16());
    return ri;
}

std::vector<uint8_t> ResolutionInfo::toBytes() const {
    std::vector<uint8_t> v;
    v.reserve(16);
    putU32(v, hResFixed);
    putU16(v, hResUnit);
    putU16(v, widthUnit);
    putU32(v, vResFixed);
    putU16(v, vResUnit);
    putU16(v, heightUnit);
    return v;
}

std::ostream& operator<<(std::ostream& os, const ResolutionInfo& r) {
    return os << "ResolutionInfo{hRes=" << r.hRes() << ", vRes=" << r.vRes() << "}";
}

Result<VersionInfo> VersionInfo::fromBytes(std::span<const uint8_t> b) {
    Reader r(b);
    VersionInfo v;
    PSD_TRY_ASSIGN(v.version, r.u32());
    PSD_TRY_ASSIGN(auto hasReal, r.u8());
    v.hasRealMergedData = hasReal != 0;
    PSD_TRY_ASSIGN(v.writer, UnicodeString::read(r));
    PSD_TRY_ASSIGN(v.reader, UnicodeString::read(r));
    PSD_TRY_ASSIGN(v.fileVersion, r.u32());
    return v;
}

std::vector<uint8_t> VersionInfo::toBytes() const {
    std::vector<uint8_t> v;
    putU32(v, version);
    putU8(v, hasRealMergedData ? 1 : 0);
    writer.write(v);
    reader.write(v);
    putU32(v, fileVersion);
    return v;
}

std::ostream& operator<<(std::ostream& os, const VersionInfo& v) {
    return os << "VersionInfo{version=" << v.version << ", hasRealMergedData=" << v.hasRealMergedData << ", writer=" << v.writer
              << ", reader=" << v.reader << ", fileVersion=" << v.fileVersion << "}";
}

ResourceData ResourceData::resolutionInfo(psd::ResolutionInfo v) { return ResourceData(Kind::ResolutionInfo, std::move(v)); }
ResourceData ResourceData::layerState(uint16_t v) { return ResourceData(Kind::LayerState, v); }
ResourceData ResourceData::layerGroupInfo(std::vector<uint16_t> v) { return ResourceData(Kind::LayerGroupInfo, std::move(v)); }
ResourceData ResourceData::thumbnail(std::span<const uint8_t> v) { return ResourceData(Kind::Thumbnail, v); }
ResourceData ResourceData::globalAngle(int32_t v) { return ResourceData(Kind::GlobalAngle, v); }
ResourceData ResourceData::iccProfile(std::span<const uint8_t> v) { return ResourceData(Kind::IccProfile, v); }
ResourceData ResourceData::globalAltitude(int32_t v) { return ResourceData(Kind::GlobalAltitude, v); }
ResourceData ResourceData::versionInfo(psd::VersionInfo v) { return ResourceData(Kind::VersionInfo, std::move(v)); }
ResourceData ResourceData::exif(std::span<const uint8_t> v) { return ResourceData(Kind::Exif, v); }
ResourceData ResourceData::xmp(std::span<const uint8_t> v) { return ResourceData(Kind::Xmp, v); }

bool ResourceData::operator==(const ResourceData& other) const {
    if (m_kind != other.m_kind) return false;
    switch (m_kind) {
    case Kind::ResolutionInfo: return asResolutionInfo() == other.asResolutionInfo();
    case Kind::LayerState: return asLayerState() == other.asLayerState();
    case Kind::LayerGroupInfo: return asLayerGroupInfo() == other.asLayerGroupInfo();
    case Kind::Thumbnail: return std::ranges::equal(asThumbnail(), other.asThumbnail());
    case Kind::GlobalAngle: return asGlobalAngle() == other.asGlobalAngle();
    case Kind::IccProfile: return std::ranges::equal(asIccProfile(), other.asIccProfile());
    case Kind::GlobalAltitude: return asGlobalAltitude() == other.asGlobalAltitude();
    case Kind::VersionInfo: return asVersionInfo() == other.asVersionInfo();
    case Kind::Exif: return std::ranges::equal(asExif(), other.asExif());
    case Kind::Xmp: return std::ranges::equal(asXmp(), other.asXmp());
    }
    return false;
}

std::ostream& operator<<(std::ostream& os, const ResourceData& r) {
    switch (r.kind()) {
    case ResourceData::Kind::ResolutionInfo: return os << "ResolutionInfo(" << r.asResolutionInfo() << ")";
    case ResourceData::Kind::LayerState: return os << "LayerState(" << r.asLayerState() << ")";
    case ResourceData::Kind::LayerGroupInfo: return os << "LayerGroupInfo(n=" << r.asLayerGroupInfo().size() << ")";
    case ResourceData::Kind::Thumbnail: return os << "Thumbnail(n=" << r.asThumbnail().size() << ")";
    case ResourceData::Kind::GlobalAngle: return os << "GlobalAngle(" << r.asGlobalAngle() << ")";
    case ResourceData::Kind::IccProfile: return os << "IccProfile(n=" << r.asIccProfile().size() << ")";
    case ResourceData::Kind::GlobalAltitude: return os << "GlobalAltitude(" << r.asGlobalAltitude() << ")";
    case ResourceData::Kind::VersionInfo: return os << "VersionInfo(" << r.asVersionInfo() << ")";
    case ResourceData::Kind::Exif: return os << "Exif(n=" << r.asExif().size() << ")";
    case ResourceData::Kind::Xmp: return os << "Xmp(n=" << r.asXmp().size() << ")";
    }
    return os << "?";
}

ImageResource ImageResource::make(uint16_t id, std::vector<uint8_t> data) {
    ImageResource r;
    r.signature = {'8', 'B', 'I', 'M'};
    r.id = id;
    r.data = std::move(data);
    return r;
}

std::optional<Result<ResourceData>> ImageResource::parsed() const {
    using R = Result<ResourceData>;
    std::span<const uint8_t> d(data);
    switch (id) {
    case ids::kResolutionInfo: {
        auto v = ResolutionInfo::fromBytes(d);
        if (!v) return R(v.error());
        return R(ResourceData::resolutionInfo(v.value()));
    }
    case ids::kLayerState: {
        Reader r(d);
        auto v = r.u16();
        if (!v) return R(v.error());
        return R(ResourceData::layerState(v.value()));
    }
    case ids::kLayerGroupInfo: {
        if (d.size() % 2 != 0) return R(PsdError::invalid("layer group info has odd length"));
        std::vector<uint16_t> groups;
        groups.reserve(d.size() / 2);
        for (size_t i = 0; i < d.size(); i += 2) {
            groups.push_back(static_cast<uint16_t>((d[i] << 8) | d[i + 1]));
        }
        return R(ResourceData::layerGroupInfo(std::move(groups)));
    }
    case ids::kThumbnail:
    case ids::kThumbnailPs4:
        return R(ResourceData::thumbnail(d));
    case ids::kGlobalAngle: {
        Reader r(d);
        auto v = r.i32();
        if (!v) return R(v.error());
        return R(ResourceData::globalAngle(v.value()));
    }
    case ids::kIccProfile:
        return R(ResourceData::iccProfile(d));
    case ids::kGlobalAltitude: {
        Reader r(d);
        auto v = r.i32();
        if (!v) return R(v.error());
        return R(ResourceData::globalAltitude(v.value()));
    }
    case ids::kVersionInfo: {
        auto v = VersionInfo::fromBytes(d);
        if (!v) return R(v.error());
        return R(ResourceData::versionInfo(v.value()));
    }
    case ids::kExif:
        return R(ResourceData::exif(d));
    case ids::kXmp:
        return R(ResourceData::xmp(d));
    default:
        return std::nullopt;
    }
}

std::optional<std::string> ImageResource::xmpStr() const {
    if (id != ids::kXmp) return std::nullopt;
    if (!isValidUtf8(data)) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

Result<ImageResource> ImageResource::read(Reader& r) {
    PSD_TRY_ASSIGN(auto signature, r.array<4>());
    auto isKnown = [&](const char* s) { return std::memcmp(signature.data(), s, 4) == 0; };
    if (!(isKnown("8BIM") || isKnown("MeSa") || isKnown("AgHg") || isKnown("PHUT") || isKnown("DCSR"))) {
        return PsdError::invalidSignature("8BIM resource", signature);
    }
    ImageResource res;
    res.signature = signature;
    PSD_TRY_ASSIGN(res.id, r.u16());
    PSD_TRY_ASSIGN(res.name, readPascal(r, 2));
    PSD_TRY_ASSIGN(auto len, r.u32());
    PSD_TRY_ASSIGN(auto dataSpan, r.bytesU64(len));
    res.data = std::vector<uint8_t>(dataSpan.begin(), dataSpan.end());
    if (len % 2 == 1) {
        PSD_TRY(r.skip(1));
    }
    return res;
}

Result<void> ImageResource::write(std::vector<uint8_t>& out) const {
    put(out, signature);
    putU16(out, id);
    writePascal(out, name, 2);
    PSD_TRY(putLen(out, static_cast<uint64_t>(data.size()), false));
    put(out, data);
    if (data.size() % 2 == 1) putU8(out, 0);
    return {};
}

std::ostream& operator<<(std::ostream& os, const ImageResource& r) { return os << "ImageResource{id=" << r.id << ", n=" << r.data.size() << "}"; }

Result<std::vector<ImageResource>> readResourcesSection(Reader& r) {
    PSD_TRY_ASSIGN(auto len, r.u32());
    PSD_TRY_ASSIGN(auto s, r.sub(len));
    std::vector<ImageResource> v;
    while (!s.isEmpty()) {
        PSD_TRY_ASSIGN(auto res, ImageResource::read(s));
        v.push_back(std::move(res));
    }
    return v;
}

Result<void> writeResourcesSection(std::vector<uint8_t>& out, const std::vector<ImageResource>& res) {
    size_t at = beginLen(out, false);
    for (const auto& r : res) {
        PSD_TRY(r.write(out));
    }
    return endLen(out, at, false);
}

ImageResource versionInfoResource(bool hasRealMergedData) {
    VersionInfo v;
    v.version = 1;
    v.hasRealMergedData = hasRealMergedData;
    v.writer = UnicodeString::fromUtf8("Adobe Photoshop");
    v.reader = UnicodeString::fromUtf8("Adobe Photoshop 2026");
    v.fileVersion = 1;
    return ImageResource::make(ids::kVersionInfo, v.toBytes());
}

} // namespace psd
