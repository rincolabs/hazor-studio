#pragma once

#include "ai/matting/MattingModelConfig.hpp"
#include "ai/matting/AiRefineTypes.hpp"

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <memory>

class AiInferenceSession;
struct AiImageSnapshot;

// Shared inference core for single-session alpha models (BiRefNet, MODNet, RMBG).
// The three concrete classes differ only in which interface they expose and their
// default config; everything ONNX — session load, preprocessing, run,
// postprocessing into a document-space alpha — lives here so it is written once.
// Not an interface: the concrete models hold one of these by composition.
class OnnxAlphaModel {
public:
    bool load(const QString& modelId, const QString& family, QString* error);

    QString modelId() const { return m_modelId; }
    QString providerUsed() const { return m_provider; }
    const MattingModelConfig& config() const { return m_config; }

    // Runs the model on the snapshot and returns a soft alpha at document
    // resolution. `logTag` is a "[AI][MATTE]"-style prefix for timing logs.
    AiAlphaResult runMatte(const AiImageSnapshot& snapshot,
                           const std::atomic<bool>* cancel,
                           const char* logTag,
                           QString* error);

private:
    QString m_modelId;
    QString m_family;
    QString m_provider;
    MattingModelConfig m_config;
    std::shared_ptr<AiInferenceSession> m_session;
};
