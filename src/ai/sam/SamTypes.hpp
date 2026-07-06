#pragma once

#include <QString>
#include <QSize>

#include <cstdint>
#include <memory>
#include <vector>

// ── SAM 1 inference value types ───────────────────────────────────────────────
// These describe the data that flows between the SAM preprocessor, encoder,
// decoder and postprocessor. They carry no ONNX types (those stay inside the
// onnx/ layer) and no Document/Layer types (those stay in the editor).

// A computed image embedding: the encoder output [1, 256, 64, 64] kept as a flat
// float buffer, plus the geometry needed by the decoder to interpret it.
struct SamEmbedding {
    std::vector<float> data;     // 1*256*64*64 floats
    QSize snapshotSize;          // size of the image fed to preprocessing (orig_im_size)
    QSize resizedSize;           // ResizeLongestSide result actually fed to the encoder
    int   inputSize = 1024;      // model square input size (SAM = 1024)

    bool isValid() const { return !data.empty(); }
    int64_t channels() const { return 256; }
    int64_t gridH() const { return 64; }
    int64_t gridW() const { return 64; }
};

// Cache key for an embedding. Two requests sharing this key may reuse the same
// embedding; any field difference forces a re-encode.
struct SamEmbeddingKey {
    quint64 documentId = 0;
    int     sourceLayerIndex = -1;  // -1 = composite
    quint64 sourceRevision = 0;     // snapshot content hash
    QString modelId;
    QString provider;               // embeddings can be device/provider dependent
    QSize   snapshotSize;
    int     inputSize = 1024;

    bool operator==(const SamEmbeddingKey& o) const
    {
        return documentId == o.documentId
            && sourceLayerIndex == o.sourceLayerIndex
            && sourceRevision == o.sourceRevision
            && modelId == o.modelId
            && provider == o.provider
            && snapshotSize == o.snapshotSize
            && inputSize == o.inputSize;
    }
};

inline uint qHash(const SamEmbeddingKey& k, uint seed = 0)
{
    uint h = seed;
    auto mix = [&h](uint v) { h = (h * 31u) ^ v; };
    mix(uint(k.documentId));
    mix(uint(k.documentId >> 32));
    mix(uint(k.sourceLayerIndex));
    mix(uint(k.sourceRevision));
    mix(uint(k.sourceRevision >> 32));
    mix(uint(qHash(k.modelId)));
    mix(uint(qHash(k.provider)));
    mix(uint(k.snapshotSize.width()));
    mix(uint(k.snapshotSize.height()));
    mix(uint(k.inputSize));
    return h;
}

// A raw decoder result before it is turned into a document-space mask: the
// upscaled mask logits at snapshot resolution plus the low-res logits that can
// be fed back as mask_input for iterative refinement.
struct SamMaskCandidate {
    std::vector<float> logits;   // snapshotH * snapshotW (single mask)
    QSize size;                  // snapshot size (== logits dimensions)
    std::vector<float> lowResLogits; // 256*256, for mask_input feedback
    float score = 0.0f;          // iou prediction

    bool isValid() const { return !logits.empty(); }
};
