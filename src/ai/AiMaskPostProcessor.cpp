#include "ai/AiMaskPostProcessor.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

cv::Mat toMat(const QImage& img)
{
    QImage g = img.format() == QImage::Format_Grayscale8
                   ? img
                   : img.convertToFormat(QImage::Format_Grayscale8);
    // Deep-copy into an owned Mat (the QImage may be temporary).
    cv::Mat view(g.height(), g.width(), CV_8UC1,
                 const_cast<uchar*>(g.bits()), static_cast<size_t>(g.bytesPerLine()));
    return view.clone();
}

QImage toImage(const cv::Mat& m)
{
    QImage out(m.cols, m.rows, QImage::Format_Grayscale8);
    for (int y = 0; y < m.rows; ++y)
        std::memcpy(out.scanLine(y), m.ptr<uchar>(y), size_t(m.cols));
    return out;
}

// Odd kernel size from a 0..100 control scaled by the image's longest side.
int kernelFromControl(int control, const cv::Size& size, double maxFrac)
{
    if (control <= 0)
        return 0;
    const double longest = std::max(size.width, size.height);
    int k = int(std::lround(longest * maxFrac * (control / 100.0)));
    k = std::max(1, k);
    if (k % 2 == 0)
        ++k;
    return k;
}

} // namespace

QImage AiMaskPostProcessor::applyRefine(const QImage& alpha, const AiRefineOptions& options)
{
    if (alpha.isNull())
        return alpha;
    cv::Mat m = toMat(alpha);
    const cv::Size sz = m.size();

    // 1) Shift edge: grow (+) / shrink (-) via morphology on a soft mask. The
    //    kernel scales with the image so the control behaves consistently.
    if (options.shiftEdge != 0) {
        // Up to ~1% of the longest side at full strength.
        const double longest = std::max(sz.width, sz.height);
        const int k = std::max(1, int(std::lround(longest * 0.01 * (std::abs(options.shiftEdge) / 100.0))));
        const cv::Mat se = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * k + 1, 2 * k + 1));
        if (options.shiftEdge > 0)
            cv::dilate(m, m, se);
        else
            cv::erode(m, m, se);
    }

    // 2) Smooth: gaussian blur on the alpha to soften jagged edges.
    if (int ks = kernelFromControl(options.smooth, sz, 0.02))
        cv::GaussianBlur(m, m, cv::Size(ks, ks), 0.0);

    // 3) Contrast: S-curve around 0.5 to harden/soften the transition band.
    if (options.contrast > 0) {
        const double gain = 1.0 + (options.contrast / 100.0) * 9.0; // 1..10
        cv::Mat lut(1, 256, CV_8UC1);
        uchar* p = lut.ptr<uchar>(0);
        for (int i = 0; i < 256; ++i) {
            const double x = i / 255.0;
            const double s = 1.0 / (1.0 + std::exp(-gain * (x - 0.5)));
            p[i] = cv::saturate_cast<uchar>(s * 255.0);
        }
        cv::LUT(m, lut, m);
    }

    // 4) Feather: a second, dedicated blur expressed directly in px.
    if (options.feather > 0) {
        int ks = options.feather * 2 + 1; // feather is a radius
        if (ks % 2 == 0) ++ks;
        cv::GaussianBlur(m, m, cv::Size(ks, ks), 0.0);
    }

    // 5) Cleanup on a binary view (keeps the soft alpha for the kept regions).
    if (options.removeSmallIslands || options.fillSmallHoles) {
        cv::Mat bin;
        cv::threshold(m, bin, 127, 255, cv::THRESH_BINARY);
        const double total = double(sz.width) * sz.height;
        const int minArea = std::max(16, int(total * 0.0010)); // 0.1% of the frame

        if (options.removeSmallIslands) {
            cv::Mat labels, stats, cent;
            const int n = cv::connectedComponentsWithStats(bin, labels, stats, cent, 8);
            for (int y = 0; y < m.rows; ++y) {
                const int* lrow = labels.ptr<int>(y);
                uchar* mrow = m.ptr<uchar>(y);
                for (int x = 0; x < m.cols; ++x) {
                    const int lbl = lrow[x];
                    if (lbl > 0 && lbl < n
                        && stats.at<int>(lbl, cv::CC_STAT_AREA) < minArea)
                        mrow[x] = 0;
                }
            }
        }
        if (options.fillSmallHoles) {
            cv::Mat inv;
            cv::threshold(m, inv, 127, 255, cv::THRESH_BINARY_INV);
            cv::Mat labels, stats, cent;
            const int n = cv::connectedComponentsWithStats(inv, labels, stats, cent, 8);
            // label 0 is the "outside" connected to the border; fill the rest of
            // the holes that are smaller than minArea.
            for (int y = 0; y < m.rows; ++y) {
                const int* lrow = labels.ptr<int>(y);
                uchar* mrow = m.ptr<uchar>(y);
                for (int x = 0; x < m.cols; ++x) {
                    const int lbl = lrow[x];
                    if (lbl > 0 && lbl < n
                        && stats.at<int>(lbl, cv::CC_STAT_AREA) < minArea)
                        mrow[x] = 255;
                }
            }
        }
    }

    // 6) Optional hard threshold when the user does not want soft edges.
    if (!options.preserveSoftEdges)
        cv::threshold(m, m, 127, 255, cv::THRESH_BINARY);

    return toImage(m);
}

QImage AiMaskPostProcessor::intersectWithBase(const QImage& alpha, const QImage* baseMask)
{
    if (!baseMask || baseMask->isNull() || alpha.isNull())
        return alpha;
    if (baseMask->size() != alpha.size())
        return alpha;

    cv::Mat a = toMat(alpha);
    cv::Mat b = toMat(*baseMask);

    // Dilate the base a little so the matte keeps soft edges just outside the
    // hard SAM boundary instead of clipping hair/fringe.
    const int k = std::max(1, int(std::lround(std::max(a.cols, a.rows) * 0.01)));
    const cv::Mat se = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * k + 1, 2 * k + 1));
    cv::Mat bDil;
    cv::dilate(b, bDil, se);

    cv::Mat out;
    cv::multiply(a, bDil, out, 1.0 / 255.0);
    return toImage(out);
}
