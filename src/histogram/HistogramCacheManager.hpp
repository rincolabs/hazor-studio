#pragma once

#include <QObject>
#include <QImage>
#include <QFutureWatcher>
#include <cstdint>

#include "HistogramTypes.hpp"
#include "HistogramGenerator.hpp"

// Revision key that identifies a histogram result. A cached result stays valid
// while the key is unchanged; any mutation bumps compositionGeneration upstream.
struct HistogramKey {
    uint64_t        generation = ~0ull; // Document::compositionGeneration
    HistogramSource source     = HistogramSource::EntireImage;

    bool operator==(const HistogramKey& o) const
    {
        return generation == o.generation && source == o.source;
    }
    bool operator!=(const HistogramKey& o) const { return !(*this == o); }
};

// Owns the cached HistogramData, tracks dirtiness via the revision key and
// rebuilds asynchronously on a worker thread. Never blocks the UI: while a
// rebuild is in flight the previous (stale) data remains available.
class HistogramCacheManager : public QObject {
    Q_OBJECT
public:
    explicit HistogramCacheManager(QObject* parent = nullptr);
    ~HistogramCacheManager() override;

    const HistogramData& data() const { return m_data; }
    bool isComputing() const { return m_computing; }
    bool isStale() const { return m_stale; }

    // Request a histogram for `source` (a detached, worker-safe QImage) under
    // `key`. If the key already matches the cached result and `force` is false,
    // emits ready() immediately with the cache. Otherwise launches an async
    // rebuild. `mask` is an optional selection mask copied for the worker.
    void request(const QImage& source,
                 const QImage& mask,
                 const HistogramKey& key,
                 const HistogramGenerator::Options& opts,
                 bool force = false);

    void invalidate();

signals:
    void ready();             // m_data updated (cache hit or rebuild finished)
    void computingChanged(bool computing);

private:
    void onFinished();

    HistogramData m_data;
    HistogramKey  m_key;
    bool          m_computing = false;
    bool          m_stale = false;
    uint64_t      m_token = 0;

    QFutureWatcher<HistogramData>* m_watcher = nullptr;
    uint64_t m_pendingToken = 0;
};
