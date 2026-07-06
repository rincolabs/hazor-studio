#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <cstdint>

enum class AiSnapshotSource {
    CurrentLayer = 0,
    CurrentAndBelow = 1,
    AllVisible = 2
};

enum class AiRemoveOutputMode {
    ActiveLayer = 0,
    NewLayer = 1
};

enum class AiRemoveEngine {
    Auto = 0,
    DirectInpaint = 1,
    StableDiffusion = 2
};

struct AiRemoveOptions {
    QString modelId;
    AiRemoveEngine engine = AiRemoveEngine::Auto;
    QString prompt = QStringLiteral("background only, natural continuation, seamless fill");
    QString negativePrompt = QStringLiteral("object, person, duplicate, artifact, blur, distortion, text, watermark");

    int maskGrowPx = 8;
    int maskFeatherPx = 4;
    int paddingPx = 64;

    int steps = 20;
    float strength = 0.85f;
    float guidanceScale = 7.5f;
    quint64 seed = 0;
    bool randomSeed = true;

    AiSnapshotSource source = AiSnapshotSource::AllVisible;
    AiRemoveOutputMode outputMode = AiRemoveOutputMode::NewLayer;
    bool runOnRelease = true;
};

struct AiRemoveApplyRequest {
    QImage generatedPatch; // RGBA8888, documentRoi size
    QImage blendMask;      // Grayscale8, documentRoi size
    QRect documentRoi;
    AiRemoveOutputMode outputMode = AiRemoveOutputMode::NewLayer;
    QString sourceModelId;
    QString prompt;
};
