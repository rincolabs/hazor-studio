#include "color/ColorManagementService.hpp"

#include "color/QtColorEngine.hpp"
#include "core/Document.hpp"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QSettings>
#include <cmath>
#include <utility>

namespace {

constexpr auto kColorGroup = "colorManagement";

ColorProfile profileFromKindSetting(int value)
{
    ColorProfile profile = ColorProfile::fromKind(static_cast<ColorProfileKind>(value));
    return profile.isValid() ? profile : ColorProfile::sRgb();
}

} // namespace

ColorManagementService::ColorManagementService(std::unique_ptr<ColorConversionEngine> engine,
                                               ColorManagementSettings settings)
    : m_engine(std::move(engine))
    , m_settings(std::move(settings))
{
}

ColorManagementService& ColorManagementService::instance()
{
    static ColorManagementService service(
        std::make_unique<QtColorEngine>(),
        ColorManagementSettings{});
    static const bool settingsLoaded = [] {
        service.reloadSettings();
        return true;
    }();
    Q_UNUSED(settingsLoaded);
    return service;
}

const ColorManagementSettings& ColorManagementService::settings() const
{
    return m_settings;
}

void ColorManagementService::setSettings(const ColorManagementSettings& settings)
{
    m_settings = settings;
}

void ColorManagementService::reloadSettings()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kColorGroup));

    m_settings.defaultRgbWorkingSpace = profileFromKindSetting(
        settings.value(QStringLiteral("defaultRgbWorkingSpace"),
                       static_cast<int>(ColorProfileKind::SRgb)).toInt());
    m_settings.missingProfilePolicy = static_cast<MissingProfilePolicy>(
        settings.value(QStringLiteral("missingProfilePolicy"),
                       static_cast<int>(MissingProfilePolicy::AssumeSRgb)).toInt());
    m_settings.profileMismatchPolicy = static_cast<ProfileMismatchPolicy>(
        settings.value(QStringLiteral("profileMismatchPolicy"),
                       static_cast<int>(ProfileMismatchPolicy::PreserveEmbeddedProfile)).toInt());
    m_settings.defaultRenderingIntent = static_cast<RenderingIntent>(
        settings.value(QStringLiteral("defaultRenderingIntent"),
                       static_cast<int>(RenderingIntent::RelativeColorimetric)).toInt());
    m_settings.blackPointCompensation = settings.value(
        QStringLiteral("blackPointCompensation"), true).toBool()
        ? BlackPointCompensation::Enabled
        : BlackPointCompensation::Disabled;
    m_settings.convertNewDocumentsToWorkingSpace = settings.value(
        QStringLiteral("convertNewDocumentsToWorkingSpace"), false).toBool();
    m_settings.enableDisplayColorManagement = settings.value(
        QStringLiteral("enableDisplayColorManagement"), true).toBool();
    m_settings.useSystemMonitorProfile = settings.value(
        QStringLiteral("useSystemMonitorProfile"), true).toBool();
    m_settings.fallbackDisplayProfile = profileFromKindSetting(
        settings.value(QStringLiteral("fallbackDisplayProfile"),
                       static_cast<int>(ColorProfileKind::SRgb)).toInt());
    m_settings.enableSoftProof = settings.value(
        QStringLiteral("enableSoftProof"), false).toBool();

    settings.endGroup();
}

ColorConversionOptions ColorManagementService::defaultConversionOptions() const
{
    ColorConversionOptions options;
    options.intent = m_settings.defaultRenderingIntent;
    options.blackPointCompensation = m_settings.blackPointCompensation;
    return options;
}

OpenColorPolicyContext ColorManagementService::defaultOpenContext(const QString& filePath) const
{
    OpenColorPolicyContext context;
    context.filePath = filePath;
    context.workingSpace = m_settings.defaultRgbWorkingSpace;
    context.missingProfilePolicy = m_settings.missingProfilePolicy;
    context.mismatchPolicy = m_settings.profileMismatchPolicy;
    return context;
}

ColorManagedOpenResult ColorManagementService::prepareImageForOpen(
    const DocumentImage& loadedImage,
    const OpenColorPolicyContext& context)
{
    ColorManagedOpenResult result;
    result.image = loadedImage;
    result.originalEmbeddedProfile = loadedImage.colorProfile;

    const ColorProfile workingSpace = context.workingSpace.isValid()
        ? context.workingSpace
        : ColorProfile::sRgb();

    if (!result.image.colorProfile.isValid()) {
        result.profileWasMissing = true;

        switch (context.missingProfilePolicy) {
        case MissingProfilePolicy::AssumeSRgb:
            result.image.colorProfile = ColorProfile::sRgb();
            result.image.colorProfile.setSource(ColorProfileSource::AssumedByPolicy);
            result.profileWasAssumed = true;
            result.warningMessage = QStringLiteral(
                "The image has no embedded color profile. sRGB was assumed.");
            break;
        case MissingProfilePolicy::AssignWorkingSpace:
            result.image.colorProfile = workingSpace;
            result.image.colorProfile.setSource(ColorProfileSource::AssumedByPolicy);
            result.profileWasAssumed = true;
            result.warningMessage = QStringLiteral(
                "The image has no embedded color profile. The current working RGB profile was assigned.");
            break;
        case MissingProfilePolicy::LeaveUntagged:
            result.finalDocumentProfile = ColorProfile::invalid();
            result.warningMessage = QStringLiteral(
                "The image is untagged and will not be color-managed.");
            break;
        case MissingProfilePolicy::AskUser:
            result.warningMessage = QStringLiteral(
                "The image has no embedded color profile.");
            break;
        }
    }

    if (result.image.colorProfile.isValid()
        && workingSpace.isValid()
        && !result.image.colorProfile.equivalentTo(workingSpace)) {

        switch (context.mismatchPolicy) {
        case ProfileMismatchPolicy::PreserveEmbeddedProfile:
            result.finalDocumentProfile = result.image.colorProfile;
            result.profileWasPreserved = true;
            break;
        case ProfileMismatchPolicy::ConvertToWorkingSpace:
            result.image = convertPixels(result.image,
                                         result.image.colorProfile,
                                         workingSpace,
                                         defaultConversionOptions());
            result.image.colorProfile = workingSpace;
            result.image.colorProfile.setSource(ColorProfileSource::ConvertedByUser);
            result.finalDocumentProfile = workingSpace;
            result.profileWasConverted = true;
            break;
        case ProfileMismatchPolicy::AssignWorkingSpace:
            result.image.colorProfile = workingSpace;
            result.image.colorProfile.setSource(ColorProfileSource::AssignedByUser);
            result.finalDocumentProfile = workingSpace;
            break;
        case ProfileMismatchPolicy::AskUser:
            result.warningMessage = QStringLiteral(
                "The embedded color profile does not match the working RGB profile.");
            break;
        }
    }

    if (!result.finalDocumentProfile.isValid())
        result.finalDocumentProfile = result.image.colorProfile;

    result.image.iccProfile = result.image.colorProfile.iccBytes();
    return result;
}

DocumentImage ColorManagementService::convertPixels(const DocumentImage& pixels,
                                                    const ColorProfile& source,
                                                    const ColorProfile& destination,
                                                    const ColorConversionOptions& options)
{
    if (!source.isValid() || !destination.isValid())
        return pixels;

    if (source.equivalentTo(destination)) {
        DocumentImage copy = pixels;
        copy.colorProfile = destination;
        copy.iccProfile = destination.iccBytes();
        return copy;
    }

    QImage image = convertDocumentImageToQImage(pixels);
    if (image.isNull() || !m_engine || !m_engine->canConvert(source, destination))
        return pixels;

    QImage converted = m_engine->convertImage(image, source, destination, options);
    if (converted.isNull())
        return pixels;

    DocumentImage result = convertQImageToDocumentImageValue(
        converted, pixels.sourcePath, pixels.sourceFormat);
    result.colorProfile = destination;
    result.colorProfile.setSource(ColorProfileSource::ConvertedByUser);
    result.iccProfile = destination.iccBytes();
    result.metadata = pixels.metadata;
    return result;
}

QColor ColorManagementService::convertColor(const QColor& color,
                                            const ColorProfile& source,
                                            const ColorProfile& destination,
                                            const ColorConversionOptions& options)
{
    if (!color.isValid()
        || !source.isValid()
        || !destination.isValid()
        || source.equivalentTo(destination)
        || !m_engine
        || !m_engine->canConvert(source, destination)) {
        return color;
    }
    return m_engine->convertColor(color, source, destination, options);
}

void ColorManagementService::assignProfile(Document* document, const ColorProfile& newProfile)
{
    if (!document)
        return;

    document->setColorProfile(newProfile);
    document->setProfileSource(newProfile.source());
    document->invalidateDisplayCache();
    document->markColorStateDirty();
}

DisplayRenderResult ColorManagementService::convertForDisplay(
    const DocumentImage& documentComposite,
    const ColorProfile& documentProfile,
    const DisplayColorContext& displayContext)
{
    const ColorProfile source = documentProfile.isValid()
        ? documentProfile
        : ColorProfile::sRgb();
    const ColorProfile destination = displayContext.displayProfile.isValid()
        ? displayContext.displayProfile
        : ColorProfile::sRgb();

    DisplayRenderResult result;
    result.sourceProfile = source;
    result.displayProfile = destination;
    result.pixels = convertPixels(documentComposite, source, destination, defaultConversionOptions());
    return result;
}

ProofRenderResult ColorManagementService::convertForSoftProof(
    const DocumentImage& documentComposite,
    const ColorProfile& documentProfile,
    const SoftProofSettings& proofSettings,
    const DisplayColorContext& displayContext)
{
    ProofRenderResult result;
    result.documentProfile = documentProfile;
    result.proofProfile = proofSettings.proofProfile;
    result.displayProfile = displayContext.displayProfile;

    if (!proofSettings.enabled || !proofSettings.proofProfile.isValid()) {
        const DisplayRenderResult display = convertForDisplay(
            documentComposite, documentProfile, displayContext);
        result.pixels = display.pixels;
        result.displayProfile = display.displayProfile;
        return result;
    }

    ColorConversionOptions proofOptions = defaultConversionOptions();
    proofOptions.intent = proofSettings.intent;
    proofOptions.blackPointCompensation = proofSettings.blackPointCompensation;

    DocumentImage proofPixels = documentComposite;
    if (!proofSettings.preserveRgbNumbers) {
        proofPixels = convertPixels(documentComposite,
                                    documentProfile,
                                    proofSettings.proofProfile,
                                    proofOptions);
    } else {
        proofPixels.colorProfile = proofSettings.proofProfile;
        proofPixels.iccProfile = proofSettings.proofProfile.iccBytes();
    }

    const DisplayRenderResult display = convertForDisplay(
        proofPixels, proofSettings.proofProfile, displayContext);
    result.pixels = display.pixels;
    result.displayProfile = display.displayProfile;
    return result;
}

quint64 colorDisplayToken(const ColorProfile& docProfile,
                          const SoftProofSettings& proof,
                          const DisplayColorContext& display,
                          bool enableDisplayColorManagement)
{
    return qHashMulti(0,
        docProfile.fingerprint(),
        display.displayProfile.fingerprint(),
        enableDisplayColorManagement,
        proof.enabled,
        proof.proofProfile.fingerprint(),
        static_cast<int>(proof.intent),
        static_cast<int>(proof.blackPointCompensation),
        proof.preserveRgbNumbers,
        proof.simulatePaperColor,
        proof.simulateBlackInk);
}

DisplayLut ColorManagementService::buildDisplayLut(const ColorProfile& documentProfile,
                                                   const SoftProofSettings& proofSettings,
                                                   const DisplayColorContext& displayContext,
                                                   int size)
{
    DisplayLut lut;
    lut.size = size;

    const bool proofOn = proofSettings.enabled && proofSettings.proofProfile.isValid();
    const ColorProfile docProfile = documentProfile.isValid()
        ? documentProfile : ColorProfile::sRgb();
    const ColorProfile displayProfile = displayContext.displayProfile.isValid()
        ? displayContext.displayProfile : ColorProfile::sRgb();

    const bool needDisplay = m_settings.enableDisplayColorManagement
                          && !docProfile.equivalentTo(displayProfile);

    if (size < 2 || (!proofOn && !needDisplay)) {
        lut.identity = true;
        return lut;
    }

    // Build the input grid as a 2D image (R fastest, then G, then B). Linear
    // texel index i = b*n² + g*n + r is placed at (x = i % (n*n), y = i / (n*n))
    // so the readback walks the same order the GL_TEXTURE_3D upload expects.
    const int n = size;
    const int width = n * n;
    const int height = n;
    QImage grid(width, height, QImage::Format_RGBA8888);
    grid.fill(Qt::black);
    auto axis = [n](int idx) {
        return static_cast<int>(std::lround(idx * 255.0 / (n - 1)));
    };
    for (int i = 0; i < n * n * n; ++i) {
        const int r = i % n;
        const int g = (i / n) % n;
        const int b = i / (n * n);
        grid.setPixelColor(i % width, i / width, QColor(axis(r), axis(g), axis(b)));
    }

    // Tag the grid with the document profile and reuse the existing display /
    // proof conversion so the LUT carries the exact same colour math.
    DocumentImage src = convertQImageToDocumentImageValue(
        grid, QString(), QStringLiteral("rgba"));
    src.colorProfile = docProfile;

    const DocumentImage out = proofOn
        ? convertForSoftProof(src, docProfile, proofSettings, displayContext).pixels
        : convertForDisplay(src, docProfile, displayContext).pixels;

    QImage result = convertDocumentImageToQImage(out);
    if (result.isNull() || result.width() != width || result.height() != height) {
        lut.identity = true; // safe fallback — never ship a malformed LUT
        return lut;
    }
    result = result.convertToFormat(QImage::Format_RGBA8888);

    lut.rgb.resize(static_cast<size_t>(n) * n * n * 3);
    for (int i = 0; i < n * n * n; ++i) {
        const QColor c = result.pixelColor(i % width, i / width);
        lut.rgb[i * 3 + 0] = static_cast<unsigned char>(c.red());
        lut.rgb[i * 3 + 1] = static_cast<unsigned char>(c.green());
        lut.rgb[i * 3 + 2] = static_cast<unsigned char>(c.blue());
    }
    lut.identity = false;
    return lut;
}

ExportColorResult ColorManagementService::prepareForExport(
    const DocumentImage& flattenedImage,
    const ColorProfile& documentProfile,
    const ExportColorOptions& exportOptions)
{
    ColorProfile destinationProfile;

    switch (exportOptions.mode) {
    case ExportColorMode::ConvertToSRgbAndEmbed:
        destinationProfile = ColorProfile::sRgb();
        break;
    case ExportColorMode::UseDocumentProfileAndEmbed:
    case ExportColorMode::DoNotEmbedProfile:
        destinationProfile = documentProfile.isValid() ? documentProfile : ColorProfile::sRgb();
        break;
    case ExportColorMode::ConvertToSelectedProfileAndEmbed:
        destinationProfile = exportOptions.selectedProfile.isValid()
            ? exportOptions.selectedProfile
            : ColorProfile::sRgb();
        break;
    }

    ExportColorResult result;
    result.profile = destinationProfile;
    result.image = flattenedImage;

    const ColorProfile source = documentProfile.isValid() ? documentProfile : ColorProfile::sRgb();
    if (!source.equivalentTo(destinationProfile)) {
        result.image = convertPixels(flattenedImage,
                                     source,
                                     destinationProfile,
                                     exportOptions.conversionOptions);
    } else {
        result.image.colorProfile = destinationProfile;
        result.image.iccProfile = destinationProfile.iccBytes();
    }

    if (exportOptions.mode != ExportColorMode::DoNotEmbedProfile)
        result.iccBytesToEmbed = destinationProfile.iccBytes();
    else
        result.image.iccProfile.clear();

    return result;
}
