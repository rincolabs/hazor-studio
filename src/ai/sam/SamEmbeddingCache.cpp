#include "ai/sam/SamEmbeddingCache.hpp"

#include <QtGlobal>

#include <algorithm>

SamEmbeddingCache::SamEmbeddingCache(int maxEntries)
    : m_maxEntries(qMax(1, maxEntries))
{
}

void SamEmbeddingCache::setMaxEntries(int maxEntries)
{
    QMutexLocker lock(&m_mutex);
    m_maxEntries = qMax(1, maxEntries);
    evictIfNeeded();
}

bool SamEmbeddingCache::contains(const SamEmbeddingKey& key) const
{
    QMutexLocker lock(&m_mutex);
    for (const Entry& e : m_entries)
        if (e.key == key)
            return true;
    return false;
}

std::shared_ptr<const SamEmbedding> SamEmbeddingCache::get(const SamEmbeddingKey& key)
{
    QMutexLocker lock(&m_mutex);
    for (Entry& e : m_entries) {
        if (e.key == key) {
            e.lastUse = ++m_clock;
            return e.embedding;
        }
    }
    return nullptr;
}

void SamEmbeddingCache::put(const SamEmbeddingKey& key,
                            std::shared_ptr<const SamEmbedding> embedding)
{
    if (!embedding)
        return;
    QMutexLocker lock(&m_mutex);
    for (Entry& e : m_entries) {
        if (e.key == key) {
            e.embedding = std::move(embedding);
            e.lastUse = ++m_clock;
            return;
        }
    }
    m_entries.push_back(Entry{ key, std::move(embedding), ++m_clock });
    evictIfNeeded();
}

void SamEmbeddingCache::invalidateDocument(quint64 documentId)
{
    QMutexLocker lock(&m_mutex);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                        [documentId](const Entry& e) {
                            return e.key.documentId == documentId;
                        }),
                    m_entries.end());
}

void SamEmbeddingCache::invalidateProvider(const QString& provider)
{
    QMutexLocker lock(&m_mutex);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                        [&provider](const Entry& e) {
                            return e.key.provider == provider;
                        }),
                    m_entries.end());
}

void SamEmbeddingCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
}

int SamEmbeddingCache::count() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

qint64 SamEmbeddingCache::approximateBytes() const
{
    QMutexLocker lock(&m_mutex);
    qint64 bytes = 0;
    for (const Entry& e : m_entries)
        if (e.embedding)
            bytes += qint64(e.embedding->data.size()) * qint64(sizeof(float));
    return bytes;
}

void SamEmbeddingCache::evictIfNeeded()
{
    // Caller holds the lock.
    while (m_entries.size() > m_maxEntries) {
        int oldest = 0;
        for (int i = 1; i < m_entries.size(); ++i)
            if (m_entries[i].lastUse < m_entries[oldest].lastUse)
                oldest = i;
        m_entries.removeAt(oldest);
    }
}
