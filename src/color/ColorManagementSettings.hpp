#pragma once

#include "color/ColorProfile.hpp"

struct ColorManagementSettings {
    ColorProfile defaultRgbWorkingSpace = ColorProfile::sRgb();

    MissingProfilePolicy missingProfilePolicy = MissingProfilePolicy::AssumeSRgb;
    ProfileMismatchPolicy profileMismatchPolicy = ProfileMismatchPolicy::PreserveEmbeddedProfile;

    RenderingIntent defaultRenderingIntent = RenderingIntent::RelativeColorimetric;
    BlackPointCompensation blackPointCompensation = BlackPointCompensation::Enabled;

    bool askWhenOpeningProfileMismatch = true;
    bool askWhenOpeningMissingProfile = true;

    bool convertNewDocumentsToWorkingSpace = false;

    bool enableDisplayColorManagement = true;
    bool useSystemMonitorProfile = true;

    ColorProfile fallbackDisplayProfile = ColorProfile::sRgb();

    bool enableSoftProof = false;
};
