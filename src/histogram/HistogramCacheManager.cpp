#include "HistogramCacheManager.hpp"

#include <QtConcurrent>

HistogramCacheManager::HistogramCacheManager(QObject* parent)
    : QObject(parent)
{
    m_watcher = new QFutureWatcher<HistogramData>(this);
    connect(m_watcher, &QFutureWatcher<HistogramData>::finished,
            this, &HistogramCacheManager::onFinished);
}

HistogramCacheManager::~HistogramCacheManager()
{
    if (m_watcher) {
        m_watcher->disconnect(this);
        m_watcher->waitForFinished();
    }
}

void HistogramCacheManager::invalidate()
{
    m_key = HistogramKey{};
    m_data.clear();
    m_stale = false;
}

void HistogramCacheManager::request(const QImage& source,
                                    const QImage& mask,
                                    const HistogramKey& key,
                                    const HistogramGenerator::Options& opts,
                                    bool force)
{
    // Cache hit: nothing changed since last build.
    if (!force && m_data.valid && key == m_key && !m_stale) {
        emit ready();
        return;
    }

    // Mark stale so the UI can show the previous data plus a warning while the
    // worker recomputes. Detached copies keep the worker independent of the
    // document being mutated on the UI thread.
    m_stale = true;
    const uint64_t token = ++m_token;
    m_pendingToken = token;
    m_key = key;

    QImage src = source;             // implicitly shared handle (COW)
    QImage msk = mask;
    HistogramGenerator::Options o = opts;

    if (!m_computing) {
        m_computing = true;
        emit computingChanged(true);
    }

    QFuture<HistogramData> fut = QtConcurrent::run(
        [src, msk, o]() -> HistogramData {
            const QImage* maskPtr = msk.isNull() ? nullptr : &msk;
            return HistogramGenerator::generate(src, maskPtr, o);
        });
    m_watcher->setFuture(fut);
}

void HistogramCacheManager::onFinished()
{
    const HistogramData result = m_watcher->result();

    // Ignore results superseded by a newer request.
    if (m_pendingToken != m_token)
        return;

    m_data = result;
    m_stale = false;
    if (m_computing) {
        m_computing = false;
        emit computingChanged(false);
    }
    emit ready();
}
