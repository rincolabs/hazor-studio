#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "Descriptor.hpp"
#include "Error.hpp"
#include "Io.hpp"

// The image resources section (8BIM resource blocks). See PROVENANCE.md for
// origin details.

namespace psd {

// Well-known resource ids.
namespace ids {
inline constexpr uint16_t kResolutionInfo = 1005;
inline constexpr uint16_t kLayerState = 1024;
inline constexpr uint16_t kLayerGroupInfo = 1026;
inline constexpr uint16_t kThumbnailPs4 = 1033;
inline constexpr uint16_t kThumbnail = 1036;
inline constexpr uint16_t kGlobalAngle = 1037;
inline constexpr uint16_t kIccProfile = 1039;
inline constexpr uint16_t kGlobalAltitude = 1049;
inline constexpr uint16_t kVersionInfo = 1057;
inline constexpr uint16_t kExif = 1058;
inline constexpr uint16_t kXmp = 1060;
} // namespace ids

// Fixed-point resolution info (resource 1005).
struct ResolutionInfo {
    uint32_t hResFixed = 0; // 16.16 fixed point, always pixels per inch.
    uint16_t hResUnit = 1;  // Display unit (1 = px/in, 2 = px/cm).
    uint16_t widthUnit = 1; // Ruler unit for width (1 in, 2 cm, 3 pt, 4 pica, 5 column).
    uint32_t vResFixed = 0;
    uint16_t vResUnit = 1;
    uint16_t heightUnit = 1;

    // Resolution info for `dpi` pixels per inch in both directions.
    static ResolutionInfo fromDpi(double dpi);
    double hRes() const { return static_cast<double>(hResFixed) / 65536.0; }
    double vRes() const { return static_cast<double>(vResFixed) / 65536.0; }
    static Result<ResolutionInfo> fromBytes(std::span<const uint8_t> b);
    std::vector<uint8_t> toBytes() const;

    bool operator==(const ResolutionInfo&) const = default;
};
std::ostream& operator<<(std::ostream& os, const ResolutionInfo& r);

// Version info (resource 1057).
struct VersionInfo {
    uint32_t version = 1;
    bool hasRealMergedData = false;
    UnicodeString writer;
    UnicodeString reader;
    uint32_t fileVersion = 1;

    static Result<VersionInfo> fromBytes(std::span<const uint8_t> b);
    std::vector<uint8_t> toBytes() const;

    bool operator==(const VersionInfo&) const = default;
};
std::ostream& operator<<(std::ostream& os, const VersionInfo& v);

// Typed view of a known resource.
class ResourceData {
public:
    enum class Kind {
        ResolutionInfo, // 1005
        LayerState,     // 1024: index of the target layer.
        LayerGroupInfo, // 1026: group id per layer.
        Thumbnail,      // 1036 / 1033: raw thumbnail resource (28-byte header + JFIF/raw data).
        GlobalAngle,    // 1037: global lighting angle in degrees.
        IccProfile,     // 1039: ICC profile bytes.
        GlobalAltitude, // 1049: global altitude.
        VersionInfo,    // 1057
        Exif,           // 1058: EXIF bytes.
        Xmp,            // 1060: XMP packet (UTF-8).
    };

    static ResourceData resolutionInfo(psd::ResolutionInfo v);
    static ResourceData layerState(uint16_t v);
    static ResourceData layerGroupInfo(std::vector<uint16_t> v);
    static ResourceData thumbnail(std::span<const uint8_t> v);
    static ResourceData globalAngle(int32_t v);
    static ResourceData iccProfile(std::span<const uint8_t> v);
    static ResourceData globalAltitude(int32_t v);
    static ResourceData versionInfo(psd::VersionInfo v);
    static ResourceData exif(std::span<const uint8_t> v);
    static ResourceData xmp(std::span<const uint8_t> v);

    Kind kind() const { return m_kind; }
    const psd::ResolutionInfo& asResolutionInfo() const { return std::get<psd::ResolutionInfo>(m_payload); }
    uint16_t asLayerState() const { return std::get<uint16_t>(m_payload); }
    const std::vector<uint16_t>& asLayerGroupInfo() const { return std::get<std::vector<uint16_t>>(m_payload); }
    std::span<const uint8_t> asThumbnail() const { return std::get<std::span<const uint8_t>>(m_payload); }
    int32_t asGlobalAngle() const { return std::get<int32_t>(m_payload); }
    std::span<const uint8_t> asIccProfile() const { return std::get<std::span<const uint8_t>>(m_payload); }
    int32_t asGlobalAltitude() const { return std::get<int32_t>(m_payload); }
    const psd::VersionInfo& asVersionInfo() const { return std::get<psd::VersionInfo>(m_payload); }
    std::span<const uint8_t> asExif() const { return std::get<std::span<const uint8_t>>(m_payload); }
    std::span<const uint8_t> asXmp() const { return std::get<std::span<const uint8_t>>(m_payload); }

    // std::span has no operator== (it's a non-owning view), so this compares
    // by hand rather than delegating to the variant's generated ==.
    bool operator==(const ResourceData& other) const;

private:
    using Payload = std::variant<psd::ResolutionInfo, uint16_t, std::vector<uint16_t>, std::span<const uint8_t>, int32_t, psd::VersionInfo>;
    ResourceData(Kind kind, Payload payload) : m_kind(kind), m_payload(std::move(payload)) {}
    Kind m_kind;
    Payload m_payload;
};
std::ostream& operator<<(std::ostream& os, const ResourceData& r);

// One image resource block.
struct ImageResource {
    std::array<uint8_t, 4> signature = {'8', 'B', 'I', 'M'};
    uint16_t id = 0;
    std::vector<uint8_t> name; // Pascal name bytes (usually empty).
    std::vector<uint8_t> data; // Resource data (without the even padding).

    // A new `8BIM` resource with an empty name.
    static ImageResource make(uint16_t id, std::vector<uint8_t> data);

    // Typed parse of well-known resources. nullopt for unknown ids.
    std::optional<Result<ResourceData>> parsed() const;
    // XMP packet as a string (resource 1060).
    std::optional<std::string> xmpStr() const;

    static Result<ImageResource> read(Reader& r);
    Result<void> write(std::vector<uint8_t>& out) const;

    bool operator==(const ImageResource&) const = default;
};
std::ostream& operator<<(std::ostream& os, const ImageResource& r);

Result<std::vector<ImageResource>> readResourcesSection(Reader& r);
Result<void> writeResourcesSection(std::vector<uint8_t>& out, const std::vector<ImageResource>& res);

// Builds the data for a 1057 version info resource, with the writer and
// reader names Photoshop 2026 writes, without a NUL terminator.
ImageResource versionInfoResource(bool hasRealMergedData);

} // namespace psd
