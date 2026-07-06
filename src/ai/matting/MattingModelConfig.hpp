#pragma once

#include <QString>

// Inference parameters for a matting / background-removal ONNX model. Different
// families normalise their input differently and emit their alpha differently;
// rather than hard-coding that per class, each model is described by this config.
// Sensible per-family defaults are built in (defaultsForFamily); a model folder
// may ship a `config.json` sidecar to override them, exactly like SamModelConfig
// does for SAM. The architecture therefore supports custom local models without
// code changes, as long as they follow the documented tensor convention.
struct MattingModelConfig {
    int  inputSize = 1024;       // square network input (longest side when keepAspect)
    bool keepAspectRatio = true; // letterbox to square (true) vs stretch (false)

    // Input normalisation: value = (pixel/255 - mean) / std, per RGB channel.
    float meanR = 0.5f, meanG = 0.5f, meanB = 0.5f;
    float stdR  = 1.0f, stdG  = 1.0f, stdB  = 1.0f;

    // Output handling.
    bool applySigmoid    = false; // squash raw logits through a sigmoid
    bool minMaxNormalize = false; // rescale output to its own [min,max] (RMBG-style)

    // Optional explicit tensor names; empty = use the model's first input/output.
    QString inputName;
    QString outputName;

    // Built-in defaults for "birefnet" / "modnet" / "rmbg"; a generic matting
    // default for anything else.
    static MattingModelConfig defaultsForFamily(const QString& family);

    // Loads defaults for `family`, then applies any `config.json` override found
    // in `modelDir`. Never throws.
    static MattingModelConfig load(const QString& modelDir, const QString& family);
};
