#pragma once

#include "color/ColorManagementSettings.hpp"
#include "color/ColorEngine.hpp"
#include "color/DisplayProfileService.hpp"
#include "color/SoftProofSettings.hpp"
#include "io/ImageCodec.hpp"

#include <memory>
#include <vector>

class Document;

struct OpenColorPolicyContext {
    QString filePath;
    bool interactive = true;

    ColorProfile workingSpace;
    MissingProfilePolicy missingProfilePolicy = MissingProfilePolicy::AssumeSRgb;
    ProfileMismatchPolicy mismatchPolicy = ProfileMismatchPolicy::PreserveEmbeddedProfile;

    bool allowDialog = true;
};

struct ColorManagedOpenResult {
    DocumentImage image;

    ColorProfile originalEmbeddedProfile;
    ColorProfile finalDocumentProfile;

    bool profileWasMissing = false;
    bool profileWasAssumed = false;
    bool profileWasConverted = false;
    bool profileWasPreserved = false;
    bool userWasWarned = false;

    QString warningMessage;
};

struct DisplayRenderResult {
    DocumentImage pixels;
    ColorProfile sourceProfile;
    ColorProfile displayProfile;
};

struct ProofRenderResult {
    DocumentImage pixels;
    ColorProfile documentProfile;
    ColorProfile proofProfile;
    ColorProfile displayProfile;
};

enum class ExportColorMode {
    ConvertToSRgbAndEmbed,
    UseDocumentProfileAndEmbed,
    ConvertToSelectedProfileAndEmbed,
    DoNotEmbedProfile
};

struct ExportColorOptions {
    ExportColorMode mode = ExportColorMode::ConvertToSRgbAndEmbed;

    ColorProfile selectedProfile;
    ColorConversionOptions conversionOptions;

    bool warnIfProfileCannotBeEmbedded = true;
    bool warnIfFormatDoesNotSupportProfile = true;
};

struct ExportColorResult {
    DocumentImage image;
    ColorProfile profile;
    QByteArray iccBytesToEmbed;
    QString warningMessage;
};

// Sampled 3D LUT for the GPU display transform (document → monitor, incl. soft
// proof). `rgb` holds size³ RGB8 triplets, the R axis varying fastest, then G,
// then B — the layout a GL_TEXTURE_3D expects. `identity == true` means no
// transform is needed (sRGB doc on sRGB display, proof off, or display CM off);
// the caller should skip the managed display pass entirely in that case.
struct DisplayLut {
    int size = 0;
    std::vector<unsigned char> rgb;
    bool identity = true;
};

// Stable identity of everything that affects the document → display transform
// (profiles + soft proof + display CM toggle). Drives LUT-rebuild caching and
// mirrors SPEC's DisplayCacheKey. Defined in ColorManagementService.cpp.
quint64 colorDisplayToken(const ColorProfile& docProfile,
                          const SoftProofSettings& proof,
                          const DisplayColorContext& display,
                          bool enableDisplayColorManagement);

class ColorManagementService {
public:
    ColorManagementService(std::unique_ptr<ColorConversionEngine> engine,
                           ColorManagementSettings settings);

    static ColorManagementService& instance();

    const ColorManagementSettings& settings() const;
    void setSettings(const ColorManagementSettings& settings);
    void reloadSettings();
    ColorConversionOptions defaultConversionOptions() const;

    OpenColorPolicyContext defaultOpenContext(const QString& filePath) const;

    ColorManagedOpenResult prepareImageForOpen(const DocumentImage& loadedImage,
                                               const OpenColorPolicyContext& context);

    DocumentImage convertPixels(const DocumentImage& pixels,
                                const ColorProfile& source,
                                const ColorProfile& destination,
                                const ColorConversionOptions& options);

    QColor convertColor(const QColor& color,
                        const ColorProfile& source,
                        const ColorProfile& destination,
                        const ColorConversionOptions& options);

    void assignProfile(Document* document, const ColorProfile& newProfile);

    DisplayRenderResult convertForDisplay(const DocumentImage& documentComposite,
                                          const ColorProfile& documentProfile,
                                          const DisplayColorContext& displayContext);

    ProofRenderResult convertForSoftProof(const DocumentImage& documentComposite,
                                          const ColorProfile& documentProfile,
                                          const SoftProofSettings& proofSettings,
                                          const DisplayColorContext& displayContext);

    // Samples the document → display transform (+ soft proof) into a 3D LUT for
    // the GPU final display pass. Returns an identity LUT when no managed
    // conversion is required. Reuses convertForDisplay / convertForSoftProof.
    DisplayLut buildDisplayLut(const ColorProfile& documentProfile,
                               const SoftProofSettings& proofSettings,
                               const DisplayColorContext& displayContext,
                               int size = 33);

    ExportColorResult prepareForExport(const DocumentImage& flattenedImage,
                                       const ColorProfile& documentProfile,
                                       const ExportColorOptions& exportOptions);

private:
    std::unique_ptr<ColorConversionEngine> m_engine;
    ColorManagementSettings m_settings;
};
