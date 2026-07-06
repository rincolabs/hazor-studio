#pragma once

// Neutral, format-agnostic types that describe the content extracted from an
// external brush file (Krita .kpp/.bundle, GIMP brush, plain image). They
// are the single hand-off point between the format adapters and the rest of the
// editor: no Brush Engine type appears here, and no adapter knows anything about
// the engine. The pipeline is:
//
//   external file → IBrushImportAdapter → ImportedBrushPackage
//                 → BrushPresetConverter → BrushPreset (engine format)
//
// Keeping these types dumb (plain structs, QImage/QVariantMap) means the engine
// never learns format-specific details, exactly as the feature requires.

#include <QString>
#include <QStringList>
#include <QImage>
#include <QSize>
#include <QVariantMap>
#include <QList>
#include <QSet>
#include <QVector>
#include <QPointF>

// ── Source classification ─────────────────────────────────────

enum class BrushImportSourceType {
    KritaKpp,
    KritaBundle,
    GimpBrush,        // GIMP/Krita predefined tip (.gbr / .gih)
    ImageBrushTip,
    Unknown
};

QString brushImportSourceLabel(BrushImportSourceType type);

// ── Diagnostics ───────────────────────────────────────────────

enum class BrushImportSeverity {
    Info,
    Warning,
    Error
};

// One reported fact about an import: a clear, honest message plus a stable code
// the UI can group on. Well-known codes:
//   unsupported_parameter, unsupported_brush_engine, missing_brush_tip,
//   corrupted_file, partial_import, duplicate_name, fallback_used,
//   pressure_curve_approximated
struct BrushImportDiagnostic {
    BrushImportSeverity severity = BrushImportSeverity::Info;
    QString code;
    QString message;
    QString sourceFile;
    QString presetName;
};

// ── Intermediate model ────────────────────────────────────────

struct ImportedBrushTip {
    QString id;
    QString name;

    // alphaImage carries the mask (grayscale or with an alpha channel);
    // colorImage is optional and only used when the source tip is a colour
    // stamp. The converter normalises whichever is present into the engine tip.
    QImage alphaImage;
    QImage colorImage;

    QSize originalSize;

    bool isAlphaOnly = true;
    bool invertedAlpha = false;

    QVariantMap sourceMetadata;

    bool isNull() const { return alphaImage.isNull() && colorImage.isNull(); }
};

// One source sensor with its own response curve. An axis in this model can stack several
// sensors (its "sensorslist"), each reading a different input; the converter turns each
// into an engine Sensor and the engine combines them.
struct ImportedSensorCurve {
    QString sensor;              // source sensor id, e.g. "pressure", "fuzzy", "ascension"
    QVector<QPointF> curve;      // 0..1 control points; empty = linear
    int length = 0;              // fade/distance/time ramp length; 0 = source default
    bool periodic = false;       // wrap the ramp instead of clamping
    float angleOffset = 0.0f;    // drawing-angle sensor: constant offset (degrees)
};

// Engine-agnostic curve option (per-axis sensor+response curve). Carries
// the source's verbatim control points so the converter can reproduce them 1:1.
// `present` means the source actually defined AND enabled it.
struct ImportedCurveOption {
    bool present = false;
    bool enabled = false;
    QVector<ImportedSensorCurve> sensors;  // all sensors; empty = flat strength
    float minValue = 0.0f;
    // Option strength: the sensor output scales into [minValue, maxValue], so a
    // flat option (no sensors / curve disabled) contributes exactly maxValue.
    float maxValue = 1.0f;
    // Sensor-combination mode (the axis "curveMode"): 0=multiply, 1=add,
    // 2=max, 3=min, 4=difference. Only relevant when more than one sensor is stacked.
    int combineMode = 0;
};

// Engine-agnostic procedural-tip descriptor (the <MaskGenerator> tag).
struct ImportedMaskGenerator {
    bool present = false;
    int shape = 0;               // 0 = circle, 1 = rectangle
    int profile = 0;             // 0 = default, 1 = soft, 2 = gaussian
    float hFade = -1.0f;         // <0 = isotropic (derive from hardness)
    float vFade = -1.0f;
    int spikes = 2;              // 2 = none
    float density = 1.0f;
    float randomness = 0.0f;
    QVector<QPointF> softnessCurve;
};

struct ImportedBrushDynamics {
    // Simple sources (a sampled tip, a plain image) that carry no structured
    // dynamics can still ask for the natural pressure→size response; the
    // converter turns this into a Pressure-sensor size option.
    bool pressureAffectsSize = false;

    QVariantMap sourceMetadata;
};

struct ImportedBrushPreset {
    QString id;
    QString name;
    QString groupName;       // group/folder inside the source file, if any

    BrushImportSourceType sourceType = BrushImportSourceType::Unknown;
    QString sourceFilePath;

    ImportedBrushTip tip;

    // Optional preset preview/icon shipped inside the source file (e.g. a .kpp is
    // itself a PNG whose pixels are the preset thumbnail). Used as the brush-list
    // thumbnail, with the rendered tip as the fallback. Null when none.
    QImage previewImage;

    float size = 25.0f;
    float minSize = 1.0f;
    float maxSize = 5000.0f;

    float spacing = 25.0f;   // percent of tip diameter
    float hardness = 1.0f;
    float opacity = 1.0f;
    float flow = 1.0f;

    float angle = 0.0f;      // degrees
    float roundness = 1.0f;

    bool flipX = false;
    bool flipY = false;

    ImportedBrushDynamics dynamics;

    // Structural per-axis dynamics from curve-option presets. Each is optional
    // (`present`); other import sources leave them absent.
    ImportedCurveOption sizeCurve;
    ImportedCurveOption opacityCurve;
    ImportedCurveOption flowCurve;
    ImportedCurveOption rotationCurve;
    ImportedCurveOption ratioCurve;
    ImportedCurveOption scatterCurve;   // strength+sensors of the scatter offset
    bool scatterAxisX = true;           // scatter axes (X = along the stroke)
    bool scatterAxisY = true;
    ImportedMaskGenerator maskGenerator;

    bool hasTipRemap = false;     // imported lightness adjustment
    float tipBrightness = 0.0f;   // -1..1
    float tipContrast = 0.0f;     // -1..1
    float tipMidpoint = 0.5f;     // 0..1

    bool useAutoSpacing = false;     // auto-spacing
    float autoSpacingCoeff = 1.0f;
    QString brushApplication;        // "ALPHAMASK" / "IMAGESTAMP" / "LIGHTNESSMAP"
    int colorAsMask = -1;            // legacy tip-application flag; -1 = absent

    bool hasTexture = false;
    QImage textureImage;
    float textureDepth = 0.0f;
    float textureScale = 1.0f;
    float textureBrightness = 0.0f;  // subtracted from the pattern value
    float textureContrast = 1.0f;    // direct multiplier (1.0 = identity, pivot 0.5)
    float textureNeutralPoint = 0.5f;// neutral-point remap pivot (0..1)
    float textureCutoff = 0.0f;      // 0..1 (cutoff left / min)
    float textureCutoffMax = 1.0f;   // 0..1 (cutoff right / max)
    int   textureCutoffPolicy = 0;   // cutoff policy: 0=off, 1=cut transparent, 2=cut opaque
    bool textureRandomOffsetX = false; // randomise the pattern offset per axis
    bool textureRandomOffsetY = false;
    int textureTexturing = 0;        // TextureConfig::Texturing (0=Multiply, 1=Subtract)
    bool textureInvert = false;      // invert pattern luminance

    bool hasDualBrush = false;
    int dualBrushTipType = 0;
    float dualBrushSize = 20.0f;
    float dualBrushSpacing = 1.0f;
    float dualBrushScatter = 0.0f;
    int dualBrushCount = 1;
    float dualBrushHardness = 0.8f;

    QString blendModeName;
    int smoothingMode = -1;
    float smoothingRadius = 10.0f;
    bool airbrush = false;
    float airbrushRate = 20.0f;      // dabs per second
    int paintMode = -1;              // -1 = unset, 0 = BuildUp, 1 = Wash
    bool wetEdges = false;

    QString originalEngineName;
    QString compatibilityLabel;   // "Compatible" / "Partial" / "Unsupported"

    QVariantMap sourceMetadata;
    QList<BrushImportDiagnostic> diagnostics;

    bool hasError() const {
        for (const auto& d : diagnostics)
            if (d.severity == BrushImportSeverity::Error) return true;
        return false;
    }
    bool isPartial() const {
        for (const auto& d : diagnostics)
            if (d.severity == BrushImportSeverity::Warning) return true;
        return false;
    }
};

struct ImportedBrushPackage {
    QString sourceFilePath;
    BrushImportSourceType sourceType = BrushImportSourceType::Unknown;

    QString packageName;
    QString vendorName;

    QList<ImportedBrushPreset> presets;
    QList<ImportedBrushTip> tips;
    QList<BrushImportDiagnostic> diagnostics;

    bool isValid() const { return !presets.isEmpty(); }
};

// ── Import options ────────────────────────────────────────────

struct BrushImportOptions {
    enum class Destination {
        BrushLibrary,     // persisted to the brush preset library
        CurrentSession,   // kept in memory until the app closes
        CurrentDocument   // per-document resources (not available yet)
    };

    Destination destination = Destination::BrushLibrary;

    bool createGroupFromFileName = true;
    bool preserveExternalGroups = true;
    bool importOnlySelectedPresets = true;

    bool importBrushTips = true;
    bool importCompatibleSettings = true;
    bool importTexturesWhenPossible = true;

    bool generatePreviews = true;

    enum class DuplicatePolicy { Rename, Replace, Skip, Ask };
    DuplicatePolicy duplicatePolicy = DuplicatePolicy::Rename;

    bool showCompatibilityWarnings = true;
    bool invertImportedAlpha = false;
    bool createBasicForUnsupported = true;

    QString targetGroupName;
    QSet<QString> selectedPresetIds;

    // Library folder layout (complement spec).
    bool createFolderFromSourceType = true;
    bool createFolderFromFileName = true;
    bool preserveExternalFolders = true;
    bool expandImportedFoldersAfterImport = true;
    QString rootImportFolderName = QStringLiteral("Imported Brushes");
};

// ── Parser safety limits (shared by the binary adapters) ──────

namespace BrushImportLimits {
constexpr int  MaxBrushesPerFile   = 5000;
constexpr int  MaxBrushTipDimension = 8192;
constexpr qint64 MaxFileSize       = qint64(512) * 1024 * 1024;
constexpr qint64 MaxEntryUncompressed = qint64(64) * 1024 * 1024; // per zip entry
}
