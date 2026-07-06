#pragma once

#include "color/ColorProfile.hpp"

#include <QString>
#include <QtGlobal>
#include <qwindowdefs.h>

struct DisplayColorContext {
    ColorProfile displayProfile;
    QString displayName;
    QString platformName;

    bool isSystemProfile = false;
    bool isFallbackSRgb = true;
};

class DisplayProfileService {
public:
    DisplayColorContext currentDisplayContextForWindow(WId windowId);

    ColorProfile fallbackDisplayProfile() const;

private:
    DisplayColorContext detectFromSystem(WId windowId);
    DisplayColorContext fallbackSRgbContext() const;
};
