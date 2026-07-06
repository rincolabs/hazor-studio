#include "BufferPool.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace memory {

static thread_local BufferPool s_tlPool(4);

BufferPool& threadLocalPool()
{
    return s_tlPool;
}

BufferPool::BufferPool(size_t maxPerType)
    : m_maxPerType(maxPerType)
{
}

int64_t BufferPool::now() const
{
    auto t = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               t.time_since_epoch()).count();
}

QImage BufferPool::acquireImage(int w, int h, QImage::Format fmt)
{
    // Find best-fit: exact match first, then smallest >= requested
    int bestIdx = -1;
    for (int i = 0; i < static_cast<int>(m_imagePool.size()); ++i) {
        auto& slot = m_imagePool[i];
        if (slot.img.isNull()) continue;
        if (slot.img.width() >= w && slot.img.height() >= h && slot.img.format() == fmt) {
            if (bestIdx < 0 || (slot.img.width() == w && slot.img.height() == h)) {
                bestIdx = i;
                if (slot.img.width() == w && slot.img.height() == h)
                    break; // exact match
            }
        }
    }

    if (bestIdx >= 0) {
        QImage result = std::move(m_imagePool[bestIdx].img);
        m_imagePool.erase(m_imagePool.begin() + bestIdx);
        return result;
    }

    return QImage(w, h, fmt);
}

void BufferPool::release(QImage& img)
{
    if (img.isNull()) return;
    int64_t ts = now();
    img.fill(0); // clear sensitive data
    m_imagePool.push_back({std::move(img), ts});
    img = QImage(); // ensure caller can't use it
    evict(m_imagePool);
}

cv::Mat BufferPool::acquireMat(int rows, int cols, int type)
{
    int bestIdx = -1;
    for (int i = 0; i < static_cast<int>(m_matPool.size()); ++i) {
        auto& slot = m_matPool[i];
        if (slot.mat.empty()) continue;
        if (slot.mat.rows >= rows && slot.mat.cols >= cols && slot.mat.type() == type) {
            if (bestIdx < 0 || (slot.mat.rows == rows && slot.mat.cols == cols)) {
                bestIdx = i;
                if (slot.mat.rows == rows && slot.mat.cols == cols)
                    break;
            }
        }
    }

    if (bestIdx >= 0) {
        cv::Mat result = m_matPool[bestIdx].mat;
        m_matPool.erase(m_matPool.begin() + bestIdx);
        return result;
    }

    return cv::Mat(rows, cols, type);
}

void BufferPool::release(cv::Mat& mat)
{
    if (mat.empty()) return;
    int64_t ts = now();
    mat = cv::Scalar::all(0); // clear
    m_matPool.push_back({mat, ts});
    mat = cv::Mat(); // detach
    evict(m_matPool);
}

template <typename T>
static void evictPool(std::vector<T>& pool, size_t maxPerType)
{
    if (pool.size() <= maxPerType) return;

    // Sort by lastUse ascending (oldest first)
    std::sort(pool.begin(), pool.end(),
        [](const T& a, const T& b) { return a.lastUse < b.lastUse; });

    // Evict oldest until within limit
    pool.erase(pool.begin(), pool.begin() + (pool.size() - maxPerType));
}

void BufferPool::evict(std::vector<ImageSlot>& pool)
{
    if (pool.size() <= m_maxPerType) return;
    std::sort(pool.begin(), pool.end(),
        [](const ImageSlot& a, const ImageSlot& b) { return a.lastUse < b.lastUse; });
    pool.erase(pool.begin(), pool.begin() + (pool.size() - m_maxPerType));
}

void BufferPool::evict(std::vector<MatSlot>& pool)
{
    if (pool.size() <= m_maxPerType) return;
    std::sort(pool.begin(), pool.end(),
        [](const MatSlot& a, const MatSlot& b) { return a.lastUse < b.lastUse; });
    pool.erase(pool.begin(), pool.begin() + (pool.size() - m_maxPerType));
}

void BufferPool::clear()
{
    m_imagePool.clear();
    m_matPool.clear();
}

} // namespace memory
