#pragma once

#include "color/ColorProfile.hpp"

#include <QList>

class ColorProfileRepository {
public:
    QList<ColorProfile> builtInRgbProfiles() const;
    QList<ColorProfile> userInstalledProfiles() const;
    QList<ColorProfile> systemProfiles() const;

    ColorProfile findByName(const QString& name) const;
    ColorProfile loadFromFile(const QString& path);

private:
    QList<ColorProfile> m_userProfiles;
};
