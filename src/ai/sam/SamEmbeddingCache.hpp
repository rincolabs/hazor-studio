#pragma once

#include "ai/sam/SamTypes.hpp"

#include <QHash>
#include <QList>
#include <QMutex>

#include <memory>

// LRU cache of SAM image embeddings keyed by document/layer/revision/model/
// provider/size. Running the encoder on a large image is the expensive part of
// SAM; reusing the embedding lets repeated clicks/box prompts run only the cheap
// decoder. Thread-safe: the AI worker thread reads/writes while the UI thread may
// invalidate on document/layer/provider changes.
class SamEmbeddingCache {
public:
    explicit SamEmbeddingCache(int maxEntries = 2);

    void setMaxEntries(int maxEntries);

    bool contains(const SamEmbeddingKey& key) const;
    std::shared_ptr<const SamEmbedding> get(const SamEmbeddingKey& key);
    void put(const SamEmbeddingKey& key, std::shared_ptr<const SamEmbedding> embedding);

    void invalidateDocument(quint64 documentId);
    void invalidateProvider(const QString& provider); // device-dependent embeddings
    void clear();

    int  count() const;
    qint64 approximateBytes() const;

private:
    struct Entry {
        SamEmbeddingKey key;
        std::shared_ptr<const SamEmbedding> embedding;
        quint64 lastUse = 0;
    };

    void evictIfNeeded();

    mutable QMutex m_mutex;
    QList<Entry> m_entries;       // small N (maxCachedDocuments); linear scan is fine
    int m_maxEntries = 2;
    quint64 m_clock = 0;
};
