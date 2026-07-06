#include "color/ColorProfileRepository.hpp"

#include <QFile>

QList<ColorProfile> ColorProfileRepository::builtInRgbProfiles() const
{
    QList<ColorProfile> profiles;
    const QList<ColorProfile> candidates = {
        ColorProfile::sRgb(),
        ColorProfile::linearSRgb(),
        ColorProfile::displayP3(),
        ColorProfile::adobeRgb1998()
    };

    for (const ColorProfile& profile : candidates) {
        if (profile.isValid())
            profiles.push_back(profile);
    }
    return profiles;
}

QList<ColorProfile> ColorProfileRepository::userInstalledProfiles() const
{
    return m_userProfiles;
}

QList<ColorProfile> ColorProfileRepository::systemProfiles() const
{
    return {};
}

ColorProfile ColorProfileRepository::findByName(const QString& name) const
{
    const auto matchesName = [&name](const ColorProfile& profile) {
        return profile.displayName().compare(name, Qt::CaseInsensitive) == 0;
    };

    for (const ColorProfile& profile : builtInRgbProfiles()) {
        if (matchesName(profile))
            return profile;
    }
    for (const ColorProfile& profile : m_userProfiles) {
        if (matchesName(profile))
            return profile;
    }
    return ColorProfile::invalid();
}

ColorProfile ColorProfileRepository::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return ColorProfile::invalid();

    ColorProfile profile = ColorProfile::fromIccBytes(file.readAll());
    if (profile.isValid())
        m_userProfiles.push_back(profile);
    return profile;
}
