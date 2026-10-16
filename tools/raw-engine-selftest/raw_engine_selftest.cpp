// Standalone, isolated smoke test for src/raw/ (RawEngine/RawSession/
// RawDevelopParams), outside the tests/ CMake suite (AGENTS.md calls that
// suite outdated and says agents must not drive it on their own initiative).
// Not wired into any build target: compiled/linked directly against the
// already-built object files and libraries under build/ — see
// tmp/raw_engine_selftest_build.sh. Never touches QApplication/QWidget, so
// this never "launches the application" in the sense AGENTS.md rule 1 means.
//
// Usage: tmp/raw_engine_selftest_build.sh && ./tmp/raw_engine_selftest [path/to/file.CR2]
// Defaults to tmp/IMG_5382.CR2 when no path is given.
//
// Keep this file around (per explicit request) until a real test lands in
// tests/ for RAW; it is not part of the Hazor target and ./scripts/build.sh
// never touches it.

#include "raw/RawDevelopParams.hpp"
#include "raw/RawEngine.hpp"
#include "raw/RawImageBuffer.hpp"
#include "raw/RawMetadata.hpp"
#include "raw/RawSession.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cstdio>
#include <string>

// Local stand-in for RawDevelopService::convertToRgba8888() (Fase 4.2) —
// not linked here to avoid pulling in its QObject/AUTOMOC machinery for a
// test that never touches the async service, just RawSession's renders.
static QImage toRgba8888(const RawImageBuffer& buffer)
{
    if (buffer.isEmpty())
        return {};
    QImage image(buffer.width, buffer.height, QImage::Format_RGBA8888);
    if (buffer.format == RawPixelFormat::RGB8) {
        for (int y = 0; y < buffer.height; ++y) {
            uchar* row = image.scanLine(y);
            const std::uint8_t* src = &buffer.pixelsU8[std::size_t(y) * buffer.width * 3];
            for (int x = 0; x < buffer.width; ++x) {
                row[x * 4 + 0] = src[x * 3 + 0];
                row[x * 4 + 1] = src[x * 3 + 1];
                row[x * 4 + 2] = src[x * 3 + 2];
                row[x * 4 + 3] = 255;
            }
        }
        return image;
    }
    const float toByte = 255.0f / 65535.0f;
    for (int y = 0; y < buffer.height; ++y) {
        uchar* row = image.scanLine(y);
        const float* src = &buffer.pixelsFloat[std::size_t(y) * buffer.width * 3];
        for (int x = 0; x < buffer.width; ++x) {
            for (int c = 0; c < 3; ++c)
                row[x * 4 + c] = static_cast<uchar>(
                    std::clamp(src[x * 3 + c] * toByte + 0.5f, 0.0f, 255.0f));
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}

static void printMetadata(const RawMetadata& m)
{
    std::printf("  camera:        %s %s\n", m.cameraMake.c_str(), m.cameraModel.c_str());
    std::printf("  lens:          %s\n", m.lens.c_str());
    std::printf("  iso:           %d\n", m.isoSpeed);
    std::printf("  f-number:      f/%.1f\n", m.fNumber);
    std::printf("  focal length:  %.1f mm\n", m.focalLengthMm);
    std::printf("  shutter:       %.4f s\n", m.shutterSpeedSeconds);
    std::printf("  exposure comp: %.2f EV\n", m.exposureCompensationEv);
    std::printf("  orientation:   %d deg%s\n", m.orientationDegrees,
                m.orientationMirrored ? " (mirrored)" : "");
    std::printf("  dimensions:    %dx%d\n", m.width, m.height);
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered, so progress is visible under `timeout`
    QCoreApplication app(argc, argv);

    const QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("tmp/IMG_5382.CR2");
    const QString resourceDir = QStringLiteral("3rdparty/rawengine/rtdata");
    const QString userDir = QDir::tempPath() + QStringLiteral("/hazor-raw-selftest-user");

    int failures = 0;
    auto check = [&](bool cond, const char* what) {
        std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
        if (!cond) ++failures;
    };

    std::printf("== RawEngine::initialize() ==\n");
    std::printf("  resourceDir: %s\n", resourceDir.toStdString().c_str());
    std::printf("  userDir:     %s\n", userDir.toStdString().c_str());
    RawEngine::initialize(resourceDir.toStdString(), userDir.toStdString());
    check(RawEngine::isAvailable(), "RawEngine::isAvailable() after initialize()");

    std::printf("\n== RawEngine::isSupportedExtension() ==\n");
    check(RawEngine::isSupportedExtension("cr2"), "\"cr2\" is a supported extension");
    check(!RawEngine::isSupportedExtension("png"), "\"png\" is not a supported extension");

    std::printf("\n== RawEngine::openFile(%s) ==\n", path.toStdString().c_str());
    check(QFileInfo::exists(path), "input file exists");
    std::string errorMessage;
    std::unique_ptr<RawSession> session = RawEngine::openFile(path.toStdString(), &errorMessage);
    if (!session) {
        std::printf("[FAIL] RawEngine::openFile(): %s\n", errorMessage.c_str());
        std::printf("\n%d failure(s).\n", failures + 1);
        return 1;
    }
    std::printf("[OK] RawEngine::openFile() returned a session\n");

    std::printf("\n== RawSession::metadata() ==\n");
    const RawMetadata meta = session->metadata();
    printMetadata(meta);
    check(meta.width > 0 && meta.height > 0, "metadata dimensions are non-zero");

    std::printf("\n== RawEngine::matchedCameraProfile() ==\n");
    const MatchedCameraProfile matched = RawEngine::matchedCameraProfile(meta);
    const char* kindName = matched.kind == MatchedProfileKind::Dcp ? "DCP"
        : matched.kind == MatchedProfileKind::Icc ? "ICC" : "CameraMatrix";
    std::printf("  matched profile: %s (kind=%s)\n", matched.matchedCameraName.c_str(), kindName);

    std::printf("\n== RawDevelopParams::cameraDefaults() + PP3 round-trip ==\n");
    RawDevelopParams params = RawDevelopParams::cameraDefaults();
    check(params.isValid(), "cameraDefaults() is valid");
    const std::string pp3 = params.toPP3String();
    std::printf("  PP3 length: %zu bytes\n", pp3.size());
    check(!pp3.empty(), "toPP3String() is non-empty");

    RawDevelopParams reparsed = RawDevelopParams::fromPP3String(pp3);
    const std::string pp3b = reparsed.toPP3String();
    check(pp3 == pp3b, "PP3 string round-trip (serialize -> parse -> serialize) is byte-identical");

    const QString pp3FilePath = QDir::tempPath() + QStringLiteral("/hazor-raw-selftest.pp3");
    check(params.savePP3File(pp3FilePath.toStdString()), "savePP3File()");
    RawDevelopParams fromFile;
    check(RawDevelopParams::loadPP3File(pp3FilePath.toStdString(), fromFile), "loadPP3File()");
    check(fromFile.toPP3String() == pp3, "PP3 file round-trip matches the string round-trip");

    std::printf("\n== RawSession::renderPreview(512) ==\n");
    const RawImageBuffer preview = session->renderPreview(512, params);
    check(!preview.isEmpty(), "renderPreview() returned a non-empty buffer");
    check(preview.format == RawPixelFormat::RGB8, "renderPreview() is RGB8");
    std::printf("  size: %dx%d\n", preview.width, preview.height);
    if (!preview.isEmpty()) {
        const QImage previewImage = toRgba8888(preview);
        const QString outPath = QStringLiteral("tmp/raw_engine_selftest_preview.png");
        check(previewImage.save(outPath), "saved preview PNG (view it to confirm it's a real photo)");
        std::printf("  -> %s\n", outPath.toStdString().c_str());
    }

    std::printf("\n== RawSession::renderFull() ==\n");
    const RawImageBuffer full = session->renderFull(params);
    check(!full.isEmpty(), "renderFull() returned a non-empty buffer");
    check(full.format == RawPixelFormat::RGBFloat, "renderFull() is RGBFloat");
    std::printf("  size: %dx%d\n", full.width, full.height);
    check(full.width == meta.width || full.height == meta.height
              || full.width == meta.height || full.height == meta.width,
          "renderFull() dimensions are plausible against sensor metadata");
    if (!full.isEmpty()) {
        const QImage fullImage = toRgba8888(full);
        const QString outPath = QStringLiteral("tmp/raw_engine_selftest_full.png");
        check(fullImage.save(outPath), "saved full-res PNG (view it to confirm it's a real photo)");
        std::printf("  -> %s\n", outPath.toStdString().c_str());
    }

    std::printf("\n%d failure(s).\n", failures);
    return failures == 0 ? 0 : 1;
}
