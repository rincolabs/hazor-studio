#pragma once

#include "ai/sam/SamTypes.hpp"
#include "ai/AiSelectionTypes.hpp"

#include <QString>
#include <QImage>
#include <QRectF>
#include <QVector>

#include <memory>

class AiInferenceSession;

// Drives a SAM 1 model's two ONNX sessions (encoder + decoder) through the
// central AiRuntimeManager. It owns no UI/document state: it turns an image
// snapshot into an embedding and a prompt into mask logits, all in snapshot
// pixel coordinates. Heavy — every method here must run on a worker thread.
//
// The encoder runs once per snapshot (its output is cached by the pipeline); the
// decoder runs per prompt, reusing the embedding. This split is the whole reason
// SAM feels interactive.
class Sam1Segmenter {
public:
    // Resolves the installed model, reads its config (input_size) and builds the
    // encoder/decoder sessions via AiRuntimeManager (honouring the user's
    // Execution Provider). Returns nullptr with *error set on failure. Must be
    // called from the AI worker thread.
    static std::shared_ptr<Sam1Segmenter> load(const QString& modelId, QString* error);

    QString modelId() const { return m_modelId; }
    QString providerUsed() const { return m_provider; }
    int inputSize() const { return m_inputSize; }

    // Runs the encoder. Returns nullptr with *error set on failure.
    std::shared_ptr<SamEmbedding> buildEmbedding(const QImage& snapshot, QString* error);

    // Runs the decoder once. `points` and `box` are in SNAPSHOT pixel space;
    // pass box=nullptr for a points-only prompt. Returns an invalid candidate
    // with *error set on failure.
    SamMaskCandidate decode(const SamEmbedding& embedding,
                            const QVector<AiPromptPoint>& pointsSnapshot,
                            const QRectF* boxSnapshot,
                            QString* error);

private:
    Sam1Segmenter() = default;

    QString m_modelId;
    QString m_provider;
    int m_inputSize = 1024;
    std::shared_ptr<AiInferenceSession> m_session;
};
