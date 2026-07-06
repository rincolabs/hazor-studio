#include "ai/AiImageSnapshot.hpp"

#include <QtGlobal>

quint64 AiImageSnapshot::hashImage(const QImage& img)
{
    if (img.isNull())
        return 0;

    // FNV-1a over a strided sample of the pixels. We sample (not every byte) so
    // hashing a working-resolution snapshot stays sub-millisecond even for large
    // images, while still changing whenever the content changes. Dimensions are
    // folded in so two different-sized snapshots never collide.
    quint64 h = 1469598103934665603ULL;
    auto mix = [&h](quint64 v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(quint64(img.width()));
    mix(quint64(img.height()));
    mix(quint64(img.format()));

    const int bpl = img.bytesPerLine();
    const int height = img.height();
    // Aim for ~64K sampled bytes regardless of image size.
    const qint64 total = qint64(bpl) * height;
    const int step = qMax(1, int(total / 65536));
    for (int y = 0; y < height; ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < bpl; x += step)
            mix(quint64(line[x]) + 0x9e3779b97f4a7c15ULL);
    }
    return h;
}
