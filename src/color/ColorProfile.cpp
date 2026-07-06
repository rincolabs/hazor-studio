#include "color/ColorProfile.hpp"

#include <QHash>

namespace {

QColorSpace namedColorSpace(QColorSpace::NamedColorSpace named)
{
    return QColorSpace(named);
}

QString colorSpaceDescription(const QColorSpace& colorSpace)
{
    const QString description = colorSpace.description().trimmed();
    return description.isEmpty() ? QStringLiteral("ICC color profile") : description;
}

bool sameIccProfile(const QColorSpace& a, const QColorSpace& b)
{
    if (!a.isValid() || !b.isValid())
        return false;

    const QByteArray aIcc = a.iccProfile();
    const QByteArray bIcc = b.iccProfile();
    return !aIcc.isEmpty() && aIcc == bIcc;
}

ColorProfileKind detectKnownKind(const QColorSpace& colorSpace)
{
    if (!colorSpace.isValid())
        return ColorProfileKind::Unknown;

    if (colorSpace == namedColorSpace(QColorSpace::SRgb))
        return ColorProfileKind::SRgb;
    if (colorSpace == namedColorSpace(QColorSpace::SRgbLinear))
        return ColorProfileKind::LinearSRgb;
    if (colorSpace == namedColorSpace(QColorSpace::DisplayP3))
        return ColorProfileKind::DisplayP3;
    if (colorSpace == namedColorSpace(QColorSpace::AdobeRgb))
        return ColorProfileKind::AdobeRgb1998;

    return ColorProfileKind::CustomIcc;
}

QString defaultNameForKind(ColorProfileKind kind)
{
    switch (kind) {
    case ColorProfileKind::SRgb:
        return QStringLiteral("sRGB IEC61966-2.1");
    case ColorProfileKind::DisplayP3:
        return QStringLiteral("Display P3");
    case ColorProfileKind::AdobeRgb1998:
        return QStringLiteral("Adobe RGB (1998)");
    case ColorProfileKind::ProPhotoRgb:
        return QStringLiteral("ProPhoto RGB");
    case ColorProfileKind::LinearSRgb:
        return QStringLiteral("Linear sRGB");
    case ColorProfileKind::CustomIcc:
        return QStringLiteral("Custom ICC Profile");
    case ColorProfileKind::Unknown:
        break;
    }
    return QStringLiteral("Untagged");
}

} // namespace

ColorProfile ColorProfile::invalid()
{
    ColorProfile profile;
    profile.m_kind = ColorProfileKind::Unknown;
    profile.m_source = ColorProfileSource::Missing;
    profile.m_displayName = defaultNameForKind(ColorProfileKind::Unknown);
    return profile;
}

ColorProfile ColorProfile::sRgb()
{
    return fromNamedColorSpace(ColorProfileKind::SRgb,
                               ColorProfileSource::GeneratedDefault,
                               namedColorSpace(QColorSpace::SRgb),
                               defaultNameForKind(ColorProfileKind::SRgb),
                               QStringLiteral("Standard RGB working space."));
}

ColorProfile ColorProfile::displayP3()
{
    return fromNamedColorSpace(ColorProfileKind::DisplayP3,
                               ColorProfileSource::GeneratedDefault,
                               namedColorSpace(QColorSpace::DisplayP3),
                               defaultNameForKind(ColorProfileKind::DisplayP3),
                               QStringLiteral("Wide-gamut RGB display profile."));
}

ColorProfile ColorProfile::adobeRgb1998()
{
    return fromNamedColorSpace(ColorProfileKind::AdobeRgb1998,
                               ColorProfileSource::GeneratedDefault,
                               namedColorSpace(QColorSpace::AdobeRgb),
                               defaultNameForKind(ColorProfileKind::AdobeRgb1998),
                               QStringLiteral("Adobe RGB (1998) working space."));
}

ColorProfile ColorProfile::linearSRgb()
{
    return fromNamedColorSpace(ColorProfileKind::LinearSRgb,
                               ColorProfileSource::GeneratedDefault,
                               namedColorSpace(QColorSpace::SRgbLinear),
                               defaultNameForKind(ColorProfileKind::LinearSRgb),
                               QStringLiteral("Linear-light sRGB profile."));
}

ColorProfile ColorProfile::fromIccBytes(const QByteArray& iccBytes)
{
    if (iccBytes.isEmpty())
        return invalid();

    QColorSpace colorSpace = QColorSpace::fromIccProfile(iccBytes);
    if (!colorSpace.isValid())
        return invalid();

    ColorProfile profile = fromQColorSpace(colorSpace);
    profile.m_iccBytes = iccBytes;
    profile.m_source = ColorProfileSource::EmbeddedInFile;
    return profile;
}

ColorProfile ColorProfile::fromQColorSpace(const QColorSpace& colorSpace)
{
    if (!colorSpace.isValid())
        return invalid();

    const ColorProfileKind kind = detectKnownKind(colorSpace);
    ColorProfile profile;
    profile.m_kind = kind;
    profile.m_source = ColorProfileSource::EmbeddedInFile;
    profile.m_qColorSpace = colorSpace;
    profile.m_iccBytes = colorSpace.iccProfile();
    profile.m_displayName = kind == ColorProfileKind::CustomIcc
        ? colorSpaceDescription(colorSpace)
        : defaultNameForKind(kind);
    profile.m_description = colorSpaceDescription(colorSpace);
    return profile;
}

ColorProfile ColorProfile::fromKind(ColorProfileKind kind)
{
    switch (kind) {
    case ColorProfileKind::SRgb:
        return sRgb();
    case ColorProfileKind::DisplayP3:
        return displayP3();
    case ColorProfileKind::AdobeRgb1998:
        return adobeRgb1998();
    case ColorProfileKind::LinearSRgb:
        return linearSRgb();
    case ColorProfileKind::ProPhotoRgb:
    case ColorProfileKind::CustomIcc:
    case ColorProfileKind::Unknown:
        break;
    }
    return invalid();
}

bool ColorProfile::isValid() const
{
    return m_qColorSpace.isValid();
}

bool ColorProfile::isSRgb() const
{
    return m_kind == ColorProfileKind::SRgb;
}

bool ColorProfile::isWideGamut() const
{
    return m_kind == ColorProfileKind::DisplayP3
        || m_kind == ColorProfileKind::AdobeRgb1998
        || m_kind == ColorProfileKind::ProPhotoRgb
        || m_kind == ColorProfileKind::CustomIcc;
}

bool ColorProfile::isLinear() const
{
    return m_kind == ColorProfileKind::LinearSRgb;
}

bool ColorProfile::isCustom() const
{
    return m_kind == ColorProfileKind::CustomIcc;
}

QString ColorProfile::displayName() const
{
    return m_displayName.isEmpty() ? defaultNameForKind(m_kind) : m_displayName;
}

QString ColorProfile::description() const
{
    return m_description;
}

ColorProfileKind ColorProfile::kind() const
{
    return m_kind;
}

ColorProfileSource ColorProfile::source() const
{
    return m_source;
}

void ColorProfile::setSource(ColorProfileSource source)
{
    m_source = source;
}

QByteArray ColorProfile::iccBytes() const
{
    if (!m_iccBytes.isEmpty())
        return m_iccBytes;
    return m_qColorSpace.isValid() ? m_qColorSpace.iccProfile() : QByteArray();
}

QColorSpace ColorProfile::toQColorSpace() const
{
    return m_qColorSpace;
}

bool ColorProfile::equivalentTo(const ColorProfile& other) const
{
    if (!isValid() || !other.isValid())
        return !isValid() && !other.isValid();

    if (m_qColorSpace == other.m_qColorSpace)
        return true;

    if (sameIccProfile(m_qColorSpace, other.m_qColorSpace))
        return true;

    const QByteArray lhs = iccBytes();
    const QByteArray rhs = other.iccBytes();
    return !lhs.isEmpty() && lhs == rhs;
}

quint64 ColorProfile::fingerprint() const
{
    if (!isValid())
        return 0;

    // Prefer the embedded ICC bytes when present (custom profiles); otherwise
    // fold the named kind + display name so the built-in spaces stay distinct.
    quint64 h = 1469598103934665603ull; // FNV-1a offset basis
    auto mix = [&h](quint64 value) {
        h ^= value;
        h *= 1099511628211ull;
    };

    mix(static_cast<quint64>(m_kind) + 1);
    const QByteArray icc = iccBytes();
    if (!icc.isEmpty())
        mix(static_cast<quint64>(qHash(icc)));
    else
        mix(static_cast<quint64>(qHash(m_displayName)));

    return h ? h : 1; // valid profiles must never fingerprint to 0
}

ColorProfile ColorProfile::fromNamedColorSpace(ColorProfileKind kind,
                                               ColorProfileSource source,
                                               const QColorSpace& colorSpace,
                                               const QString& displayName,
                                               const QString& description)
{
    if (!colorSpace.isValid())
        return invalid();

    ColorProfile profile;
    profile.m_kind = kind;
    profile.m_source = source;
    profile.m_qColorSpace = colorSpace;
    profile.m_iccBytes = colorSpace.iccProfile();
    profile.m_displayName = displayName;
    profile.m_description = description;
    return profile;
}

QString colorProfileKindToString(ColorProfileKind kind)
{
    switch (kind) {
    case ColorProfileKind::Unknown: return QStringLiteral("Unknown");
    case ColorProfileKind::SRgb: return QStringLiteral("SRgb");
    case ColorProfileKind::DisplayP3: return QStringLiteral("DisplayP3");
    case ColorProfileKind::AdobeRgb1998: return QStringLiteral("AdobeRgb1998");
    case ColorProfileKind::ProPhotoRgb: return QStringLiteral("ProPhotoRgb");
    case ColorProfileKind::LinearSRgb: return QStringLiteral("LinearSRgb");
    case ColorProfileKind::CustomIcc: return QStringLiteral("CustomIcc");
    }
    return QStringLiteral("Unknown");
}

QString colorProfileSourceToString(ColorProfileSource source)
{
    switch (source) {
    case ColorProfileSource::EmbeddedInFile: return QStringLiteral("EmbeddedInFile");
    case ColorProfileSource::AssignedByUser: return QStringLiteral("AssignedByUser");
    case ColorProfileSource::AssumedByPolicy: return QStringLiteral("AssumedByPolicy");
    case ColorProfileSource::ConvertedByUser: return QStringLiteral("ConvertedByUser");
    case ColorProfileSource::GeneratedDefault: return QStringLiteral("GeneratedDefault");
    case ColorProfileSource::Missing: return QStringLiteral("Missing");
    }
    return QStringLiteral("Missing");
}

ColorProfileKind colorProfileKindFromString(const QString& value)
{
    if (value == QLatin1String("SRgb")) return ColorProfileKind::SRgb;
    if (value == QLatin1String("DisplayP3")) return ColorProfileKind::DisplayP3;
    if (value == QLatin1String("AdobeRgb1998")) return ColorProfileKind::AdobeRgb1998;
    if (value == QLatin1String("ProPhotoRgb")) return ColorProfileKind::ProPhotoRgb;
    if (value == QLatin1String("LinearSRgb")) return ColorProfileKind::LinearSRgb;
    if (value == QLatin1String("CustomIcc")) return ColorProfileKind::CustomIcc;
    return ColorProfileKind::Unknown;
}

ColorProfileSource colorProfileSourceFromString(const QString& value)
{
    if (value == QLatin1String("EmbeddedInFile")) return ColorProfileSource::EmbeddedInFile;
    if (value == QLatin1String("AssignedByUser")) return ColorProfileSource::AssignedByUser;
    if (value == QLatin1String("AssumedByPolicy")) return ColorProfileSource::AssumedByPolicy;
    if (value == QLatin1String("ConvertedByUser")) return ColorProfileSource::ConvertedByUser;
    if (value == QLatin1String("GeneratedDefault")) return ColorProfileSource::GeneratedDefault;
    return ColorProfileSource::Missing;
}
