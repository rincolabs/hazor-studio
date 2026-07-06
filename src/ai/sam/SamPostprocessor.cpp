#include "ai/sam/SamPostprocessor.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>
#include <cmath>

namespace {

// Binary snapshot-resolution mask (CV_8UC1, 0/255) from candidate logits.
cv::Mat binaryFromLogits(const SamMaskCandidate& cand, float threshold)
{
    const int w = cand.size.width();
    const int h = cand.size.height();
    cv::Mat m(h, w, CV_8UC1);
    const float* src = cand.logits.data();
    for (int y = 0; y < h; ++y) {
        uchar* row = m.ptr<uchar>(y);
        for (int x = 0; x < w; ++x)
            row[x] = (src[y * w + x] > threshold) ? 255 : 0;
    }
    return m;
}

} // namespace

AiMaskResult SamPostprocessor::toDocumentMask(const SamMaskCandidate& candidate,
                                              const QSize& documentSize,
                                              const AiSelectionOptions& options)
{
    AiMaskResult result;
    if (!candidate.isValid() || documentSize.width() <= 0 || documentSize.height() <= 0)
        return result;

    cv::Mat snapMask = binaryFromLogits(candidate, options.maskThreshold);

    // Snapshot-space bbox of the raw decoder mask (before mapping to document).
    {
        cv::Mat snz;
        cv::findNonZero(snapMask, snz);
        const cv::Rect sbb = snz.empty() ? cv::Rect() : cv::boundingRect(snz);
        qInfo().noquote() << "[AI][SEL] snapshot mask size=" << candidate.size
                          << "snapBBox=(" << sbb.x << "," << sbb.y << ")-("
                          << (sbb.x + sbb.width) << "," << (sbb.y + sbb.height) << ")";
    }

    const int dw = documentSize.width();
    const int dh = documentSize.height();

    QImage out(dw, dh, QImage::Format_Grayscale8);
    cv::Mat docMask(dh, dw, CV_8UC1, out.bits(),
                    static_cast<size_t>(out.bytesPerLine()));
    cv::resize(snapMask, docMask, cv::Size(dw, dh), 0, 0,
               options.antiAlias ? cv::INTER_LINEAR : cv::INTER_NEAREST);

    // Document-space bounding box of the selection.
    cv::Mat nz;
    cv::findNonZero(docMask > 127, nz);
    if (nz.empty())
        return result; // produced nothing — caller treats as "no object found"
    const cv::Rect bb = cv::boundingRect(nz);
    qInfo().noquote() << "[AI][SEL] doc mask size=" << documentSize
                      << "docBBox=(" << bb.x << "," << bb.y << ")-("
                      << (bb.x + bb.width) << "," << (bb.y + bb.height) << ")";

    result.mask = out;
    result.bounds = QRectF(bb.x, bb.y, bb.width, bb.height);
    result.score = candidate.score;
    result.ok = true;
    return result;
}

SamPostprocessor::MaskStats SamPostprocessor::analyze(const SamMaskCandidate& candidate)
{
    MaskStats stats;
    if (!candidate.isValid())
        return stats;

    const int w = candidate.size.width();
    const int h = candidate.size.height();
    stats.width = w;
    stats.height = h;

    const float* src = candidate.logits.data();
    double sumX = 0.0, sumY = 0.0;
    qint64 count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (src[y * w + x] > 0.0f) {
                sumX += x;
                sumY += y;
                ++count;
            }
        }
    }
    if (count == 0)
        return stats;

    stats.empty = false;
    stats.areaFraction = double(count) / (double(w) * double(h));
    stats.centroid = QPointF(sumX / count, sumY / count);

    const double cx = w * 0.5;
    const double cy = h * 0.5;
    const double dx = (stats.centroid.x() - cx) / std::max(1.0, cx);
    const double dy = (stats.centroid.y() - cy) / std::max(1.0, cy);
    stats.centerDistanceFrac = std::sqrt(dx * dx + dy * dy);
    return stats;
}

void SamPostprocessor::keepLargestComponent(SamMaskCandidate& candidate)
{
    if (!candidate.isValid())
        return;

    cv::Mat bin = binaryFromLogits(candidate, 0.0f);
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8);
    if (n <= 2)
        return; // background + at most one blob: nothing to prune

    int best = -1;
    int bestArea = 0;
    for (int i = 1; i < n; ++i) {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    if (best < 0)
        return;

    const int w = candidate.size.width();
    const int h = candidate.size.height();
    float* dst = candidate.logits.data();
    for (int y = 0; y < h; ++y) {
        const int* lrow = labels.ptr<int>(y);
        for (int x = 0; x < w; ++x) {
            if (lrow[x] != best && dst[y * w + x] > 0.0f)
                dst[y * w + x] = -1.0f; // drop pixels outside the largest blob
        }
    }
}
