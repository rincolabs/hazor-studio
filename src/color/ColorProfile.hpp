#pragma once

#include <QByteArray>
#include <QColorSpace>
#include <QString>

enum class ColorProfileKind {
    Unknown,
    SRgb,
    DisplayP3,
    AdobeRgb1998,
    ProPhotoRgb,
    LinearSRgb,
    CustomIcc
};

enum class ColorProfileSource {
    EmbeddedInFile,
    AssignedByUser,
    AssumedByPolicy,
    ConvertedByUser,
    GeneratedDefault,
    Missing
};

enum class MissingProfilePolicy {
    AskUser,
    AssumeSRgb,
    AssignWorkingSpace,
    LeaveUntagged
};

enum class ProfileMismatchPolicy {
    AskUser,
    PreserveEmbeddedProfile,
    ConvertToWorkingSpace,
    AssignWorkingSpace
};

enum class RenderingIntent {
    Perceptual,
    RelativeColorimetric,
    Saturation,
    AbsoluteColorimetric
};

enum class BlackPointCompensation {
    Disabled,
    Enabled
};

struct ColorConversionOptions {
    RenderingIntent intent = RenderingIntent::RelativeColorimetric;
    BlackPointCompensation blackPointCompensation = BlackPointCompensation::Enabled;

    bool dither = true;
    bool preserveAlpha = true;
    bool clampOutOfGamut = true;
};

class ColorProfile {
public:
    static ColorProfile invalid();
    static ColorProfile sRgb();
    static ColorProfile displayP3();
    static ColorProfile adobeRgb1998();
    static ColorProfile linearSRgb();

    static ColorProfile fromIccBytes(const QByteArray& iccBytes);
    static ColorProfile fromQColorSpace(const QColorSpace& colorSpace);
    static ColorProfile fromKind(ColorProfileKind kind);

    bool isValid() const;
    bool isSRgb() const;
    bool isWideGamut() const;
    bool isLinear() const;
    bool isCustom() const;

    QString displayName() const;
    QString description() const;

    ColorProfileKind kind() const;
    ColorProfileSource source() const;
    void setSource(ColorProfileSource source);

    QByteArray iccBytes() const;
    QColorSpace toQColorSpace() const;

    bool equivalentTo(const ColorProfile& other) const;

    // Stable, cheap identity hash for cache keys (display/composite caches).
    // Returns 0 for an invalid profile; never 0 for a valid one. Two profiles
    // that are byte-for-byte the same ICC (or the same named space) share a
    // fingerprint; it is a cache discriminator, not a colorimetric equality.
    quint64 fingerprint() const;

private:
    static ColorProfile fromNamedColorSpace(ColorProfileKind kind,
                                            ColorProfileSource source,
                                            const QColorSpace& colorSpace,
                                            const QString& displayName,
                                            const QString& description);

    ColorProfileKind m_kind = ColorProfileKind::Unknown;
    ColorProfileSource m_source = ColorProfileSource::Missing;

    QString m_displayName;
    QString m_description;

    QByteArray m_iccBytes;
    QColorSpace m_qColorSpace;
};

QString colorProfileKindToString(ColorProfileKind kind);
QString colorProfileSourceToString(ColorProfileSource source);
ColorProfileKind colorProfileKindFromString(const QString& value);
ColorProfileSource colorProfileSourceFromString(const QString& value);
