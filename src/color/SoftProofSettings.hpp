#pragma once

#include "color/ColorProfile.hpp"

struct SoftProofSettings {
    bool enabled = false;

    ColorProfile proofProfile;

    RenderingIntent intent = RenderingIntent::RelativeColorimetric;
    BlackPointCompensation blackPointCompensation = BlackPointCompensation::Enabled;

    bool simulatePaperColor = false;
    bool simulateBlackInk = false;
    bool preserveRgbNumbers = false;

    QString presetName;
};
