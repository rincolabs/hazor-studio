#pragma once

#include <QString>

// Descriptor for a SAM model declared by a sidecar config file (config.yaml /
// config.json) living in the model's folder, as introduced in Etapa 2:
//
//   type: segment_anything
//   name: sam_vit_b_01ec64
//   display_name: Segment Anything (ViT-B)
//   encoder_model_path: sam_vit_b_01ec64.encoder.onnx
//   decoder_model_path: sam_vit_b_01ec64.decoder.onnx
//   input_size: 1024
//   max_width: 1024
//   max_height: 682
//
// The architecture never assumes SAM is a single ONNX file: a model is one
// logical entity with two physical files (encoder + decoder). This struct is the
// parsed form; the registry turns it into an AiInstalledModel so it shows up in
// the same model list as manifest-installed models.
struct SamModelConfig {
    QString type;              // expected: "segment_anything"
    QString name;
    QString displayName;
    QString encoderModelPath;  // filename, relative to the model folder
    QString decoderModelPath;
    int inputSize = 1024;
    int maxWidth = 0;          // 0 = unconstrained
    int maxHeight = 0;

    bool isSegmentAnything() const
    {
        return type.compare(QStringLiteral("segment_anything"), Qt::CaseInsensitive) == 0;
    }
    bool isValid() const
    {
        return isSegmentAnything()
            && !encoderModelPath.isEmpty()
            && !decoderModelPath.isEmpty();
    }

    // Parses a config.yaml / config.json describing a SAM model. Returns an
    // invalid config (isValid()==false) on any problem; never throws. The parser
    // is intentionally tiny (flat key: value), enough for the documented schema
    // without pulling in a YAML dependency.
    static SamModelConfig parseFile(const QString& configPath);
    static SamModelConfig parseYaml(const QString& text);
    static SamModelConfig parseJson(const QString& text);

    // Locates a config descriptor inside a model folder, if present.
    // Returns an empty string when none exists.
    static QString findConfigInDir(const QString& dir);
};
