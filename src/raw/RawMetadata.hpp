#pragma once

#include <string>

// Camera/exposure metadata read from a RAW file. Plain data, no rtengine
// types: the one field that needs interpretation (EXIF orientation, an
// upstream string like "Rotate 90 CW") is pre-decoded into degrees + mirror
// flags here so nothing outside src/raw/ ever has to know that encoding.
struct RawMetadata {
    std::string cameraMake;
    std::string cameraModel;
    std::string lens;

    int isoSpeed = 0;
    double fNumber = 0.0;              // aperture (f-stop)
    double focalLengthMm = 0.0;
    double shutterSpeedSeconds = 0.0;
    double exposureCompensationEv = 0.0;

    // Orientation as the engine will apply it (RawImageSource::defTransform
    // combines the RAW file's own embedded rotation with ProcParams::coarse;
    // this reports the file's own rotation before any user-requested coarse
    // transform on top of it).
    int orientationDegrees = 0;        // one of 0, 90, 180, 270 (clockwise)
    bool orientationMirrored = false;

    int width = 0;                     // sensor/raw width before crop
    int height = 0;

    std::string cameraName() const { return cameraMake + " " + cameraModel; }
};
