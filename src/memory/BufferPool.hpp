#pragma once

#include <QImage>
#include <opencv2/core.hpp>
#include <vector>
#include <cstddef>

namespace memory {

class BufferPool {
public:
    explicit BufferPool(size_t maxPerType = 4);
    ~BufferPool() = default;

    QImage acquireImage(int w, int h, QImage::Format fmt = QImage::Format_RGBA8888);
    void release(QImage& img);

    cv::Mat acquireMat(int rows, int cols, int type);
    void release(cv::Mat& mat);

    void clear();

private:
    struct ImageSlot { QImage img; int64_t lastUse; };
    struct MatSlot   { cv::Mat mat; int64_t lastUse; };

    std::vector<ImageSlot> m_imagePool;
    std::vector<MatSlot>   m_matPool;
    size_t m_maxPerType;

    int64_t now() const;
    void evict(std::vector<ImageSlot>& pool);
    void evict(std::vector<MatSlot>& pool);
};

// Thread-local singleton — no contention between workers.
BufferPool& threadLocalPool();

} // namespace memory
