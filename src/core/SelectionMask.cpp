#include "SelectionMask.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

void SelectionMask::create(int w, int h)
{
    m_mask = QImage(w, h, QImage::Format_Grayscale8);
    m_mask.fill(0);
    m_active = false;
    m_cacheValid = false;
}

void SelectionMask::clear()
{
    if (!m_mask.isNull())
        m_mask.fill(0);
    m_active = false;
    m_cacheValid = false;
}

static void combineMask(QImage& dst, const QImage& src, SelectMode mode)
{
    int w = std::min(dst.width(), src.width());
    int h = std::min(dst.height(), src.height());

    for (int y = 0; y < h; ++y) {
        const uchar* srcRow = src.constScanLine(y);
        uchar* dstRow = dst.scanLine(y);
        for (int x = 0; x < w; ++x) {
            switch (mode) {
            case SelectMode::Replace:
                dstRow[x] = srcRow[x];
                break;
            case SelectMode::Add:
                dstRow[x] = std::max(dstRow[x], srcRow[x]);
                break;
            case SelectMode::Subtract:
                // Saturating subtract / min keep soft (feathered/anti-aliased)
                // edges intact; the old binary versions (src>0 → 0 / keep)
                // turned any combine into hard edges.
                dstRow[x] = static_cast<uchar>(
                    std::max(0, static_cast<int>(dstRow[x]) - static_cast<int>(srcRow[x])));
                break;
            case SelectMode::Intersect:
                dstRow[x] = std::min(dstRow[x], srcRow[x]);
                break;
            }
        }
    }
}

void SelectionMask::setRect(const QRectF& rectDoc, SelectMode mode, bool antiAlias)
{
    if (m_mask.isNull()) return;

    int w = m_mask.width();
    int h = m_mask.height();

    double l = rectDoc.left();
    double t = rectDoc.top();
    double r = l + rectDoc.width() - 1.0;
    double b = t + rectDoc.height() - 1.0;

    // A rect entirely outside the canvas must be rejected before clamping;
    // clamping it would collapse to a phantom 1px line stuck to the border.
    if (std::round(r) < 0.0 || std::round(b) < 0.0 ||
        std::round(l) > w - 1.0 || std::round(t) > h - 1.0)
        return;

    int x1 = std::clamp(static_cast<int>(std::round(l)), 0, w - 1);
    int y1 = std::clamp(static_cast<int>(std::round(t)), 0, h - 1);
    int x2 = std::clamp(static_cast<int>(std::round(r)), 0, w - 1);
    int y2 = std::clamp(static_cast<int>(std::round(b)), 0, h - 1);

    if (x2 < x1 || y2 < y1) return;

    if (mode == SelectMode::Replace)
        m_mask.fill(0);

    QImage temp(w, h, QImage::Format_Grayscale8);
    temp.fill(0);

    for (int y = y1; y <= y2; ++y) {
        uchar* row = temp.scanLine(y);
        for (int x = x1; x <= x2; ++x)
            row[x] = 255;
    }

    if (antiAlias) {
        // Soften only the new shape before combining (like ellipse/polygon);
        // blurring m_mask after the combine would re-soften the whole
        // existing selection on every Add/Subtract/Intersect.
        cv::Mat cvTemp(h, w, CV_8UC1, temp.bits(),
                       static_cast<size_t>(temp.bytesPerLine()));
        cv::GaussianBlur(cvTemp, cvTemp, cv::Size(3, 3), 0.35);
    }

    combineMask(m_mask, temp, mode);
    m_active = true;
    m_cacheValid = false;
}

void SelectionMask::combineGrayscaleMask(const QImage& mask8, SelectMode mode)
{
    if (mask8.isNull()) return;

    const int w = mask8.width();
    const int h = mask8.height();
    if (m_mask.isNull() || m_mask.width() != w || m_mask.height() != h) {
        // No (or mismatched) selection yet: start from an empty mask of the
        // incoming size so the combine rule has a well-defined base.
        m_mask = QImage(w, h, QImage::Format_Grayscale8);
        m_mask.fill(0);
    }

    QImage src = mask8.format() == QImage::Format_Grayscale8
        ? mask8 : mask8.convertToFormat(QImage::Format_Grayscale8);

    combineMask(m_mask, src, mode);
    m_active = true;
    m_cacheValid = false;
}

void SelectionMask::setEllipse(const QRectF& rectDoc, SelectMode mode, bool antiAlias)
{
    if (m_mask.isNull()) return;

    int w = m_mask.width();
    int h = m_mask.height();

    // Same inclusive pixel convention as setRect (last pixel = l + w - 1) so
    // the same drag reaches exactly the same extreme rows/columns with both
    // marquee tools.
    double l = rectDoc.left();
    double t = rectDoc.top();
    double r = l + rectDoc.width() - 1.0;
    double b = t + rectDoc.height() - 1.0;

    double cx = (l + r) / 2.0;
    double cy = (t + b) / 2.0;
    double rx = (r - l) / 2.0;
    double ry = (b - t) / 2.0;

    if (rx < 1.0 || ry < 1.0) {
        setRect(rectDoc, mode, antiAlias);
        return;
    }

    if (mode == SelectMode::Replace)
        m_mask.fill(0);

    QImage temp(w, h, QImage::Format_Grayscale8);
    temp.fill(0);

    int x1 = std::clamp(static_cast<int>(std::floor(cx - rx)) - 1, 0, w - 1);
    int y1 = std::clamp(static_cast<int>(std::floor(cy - ry)) - 1, 0, h - 1);
    int x2 = std::clamp(static_cast<int>(std::ceil(cx + rx)) + 1,  0, w - 1);
    int y2 = std::clamp(static_cast<int>(std::ceil(cy + ry)) + 1,  0, h - 1);

    double rxi = 1.0 / (rx * rx);
    double ryi = 1.0 / (ry * ry);

    for (int y = y1; y <= y2; ++y) {
        uchar* row = temp.scanLine(y);
        double dy = y - cy;
        for (int x = x1; x <= x2; ++x) {
            double dx = x - cx;
            double d = dx * dx * rxi + dy * dy * ryi;
            if (antiAlias) {
                // ~1px coverage band regardless of radius: pixel distance to
                // the boundary ≈ (1 - d) / |∇d|. A fixed band on d itself
                // scales with the radius (20px of blur on a 400px ellipse).
                double g = 2.0 * std::sqrt(dx * dx * rxi * rxi + dy * dy * ryi * ryi);
                double distPx = (1.0 - d) / std::max(g, 1e-9);
                double cov = std::clamp(distPx + 0.5, 0.0, 1.0);
                if (cov > 0.0)
                    row[x] = static_cast<uchar>(cov * 255.0 + 0.5);
            } else if (d <= 1.0) {
                row[x] = 255;
            }
        }
    }

    combineMask(m_mask, temp, mode);
    m_active = true;
    m_cacheValid = false;
}

void SelectionMask::setPolygon(const std::vector<QPointF>& points, SelectMode mode, bool antiAlias)
{
    if (m_mask.isNull() || points.size() < 3) return;

    int w = m_mask.width();
    int h = m_mask.height();

    if (mode == SelectMode::Replace)
        m_mask.fill(0);

    cv::Mat cvMask(h, w, CV_8UC1, cv::Scalar(0));

    std::vector<cv::Point> cvPts;
    cvPts.reserve(points.size());
    for (auto& p : points)
        cvPts.push_back(cv::Point(static_cast<int>(std::round(p.x())),
                                  static_cast<int>(std::round(p.y()))));

    cv::fillPoly(cvMask, std::vector<std::vector<cv::Point>>{cvPts},
                 cv::Scalar(255));

    QImage temp(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        const uchar* src = cvMask.ptr<uchar>(y);
        uchar* dst = temp.scanLine(y);
        std::copy(src, src + w, dst);
    }

    if (antiAlias) {
        cv::Mat cvTemp(h, w, CV_8UC1, temp.bits(),
                       static_cast<size_t>(temp.bytesPerLine()));
        cv::GaussianBlur(cvTemp, cvTemp, cv::Size(3, 3), 0.4);
    }

    combineMask(m_mask, temp, mode);
    m_active = true;
    m_cacheValid = false;
}

void SelectionMask::setMagicWand(int seedX, int seedY, float tolerance, bool contiguous,
                                 SelectMode mode, const uchar* srcBits, int srcW, int srcH,
                                 const double* invAffine)
{
    if (m_mask.isNull() || !srcBits) return;

    int mw = m_mask.width();
    int mh = m_mask.height();

    // Validate the seed before mutating anything: rejecting after the
    // Replace-clear below would turn an out-of-bounds click into an
    // accidental deselect.
    if (invAffine) {
        if (seedX < 0 || seedX >= mw || seedY < 0 || seedY >= mh)
            return;
    } else {
        // The fast-path mats are min(document, layer)-sized — bound the seed
        // to the mat, not to the source image, or floodFill on a layer larger
        // than the document reads past the mat.
        if (seedX < 0 || seedX >= std::min(mw, srcW) ||
            seedY < 0 || seedY >= std::min(mh, srcH))
            return;
    }

    if (mode == SelectMode::Replace)
        m_mask.fill(0);

    if (invAffine) {
        // Transformed layer: iterate document-pixel positions, map doc→layer to read color.
        // seedX, seedY are in document-pixel coords.
        // srcW, srcH are the layer image dimensions.
        double inv00 = invAffine[0], inv01 = invAffine[1], inv02 = invAffine[2];
        double inv10 = invAffine[3], inv11 = invAffine[4], inv12 = invAffine[5];

        // Get seed color from layer pixel at the seed's document position
        double lxSeed = inv00 * seedX + inv01 * seedY + inv02;
        double lySeed = inv10 * seedX + inv11 * seedY + inv12;
        int lxSeedI = std::clamp(static_cast<int>(lxSeed + 0.5), 0, srcW - 1);
        int lySeedI = std::clamp(static_cast<int>(lySeed + 0.5), 0, srcH - 1);
        const uchar* sp = srcBits + (lySeedI * srcW + lxSeedI) * 4;
        int sR = sp[0], sG = sp[1], sB = sp[2], sA = sp[3];

        cv::Mat diff(mh, mw, CV_8UC1, cv::Scalar(0));

        for (int y = 0; y < mh; ++y) {
            uchar* dRow = diff.ptr<uchar>(y);
            for (int x = 0; x < mw; ++x) {
                double lx = inv00 * x + inv01 * y + inv02;
                double ly = inv10 * x + inv11 * y + inv12;
                int lxI = std::clamp(static_cast<int>(lx + 0.5), 0, srcW - 1);
                int lyI = std::clamp(static_cast<int>(ly + 0.5), 0, srcH - 1);
                const uchar* px = srcBits + (lyI * srcW + lxI) * 4;
                dRow[x] = colorMatches(px, sR, sG, sB, sA, tolerance) ? 255 : 0;
            }
        }

        QImage temp(mw, mh, QImage::Format_Grayscale8);

        if (contiguous) {
            cv::Mat flood = diff.clone();
            cv::floodFill(flood, cv::Point(seedX, seedY), cv::Scalar(128),
                          nullptr, cv::Scalar(0), cv::Scalar(0));
            for (int y = 0; y < mh; ++y) {
                uchar* dst = temp.scanLine(y);
                const uchar* src = flood.ptr<uchar>(y);
                for (int x = 0; x < mw; ++x)
                    dst[x] = (src[x] == 128) ? 255 : 0;
            }
        } else {
            for (int y = 0; y < mh; ++y) {
                uchar* dst = temp.scanLine(y);
                const uchar* src = diff.ptr<uchar>(y);
                std::copy(src, src + mw, dst);
            }
        }

        combineMask(m_mask, temp, mode);
    } else {
        // Untransformed layer: doc-pixel = layer-pixel (fast path)
        const uchar* seedPixel = srcBits + (seedY * srcW + seedX) * 4;
        int sR = seedPixel[0], sG = seedPixel[1], sB = seedPixel[2], sA = seedPixel[3];

        int scaleW = std::min(mw, srcW);
        int scaleH = std::min(mh, srcH);

        cv::Mat diff(scaleH, scaleW, CV_8UC1, cv::Scalar(0));

        for (int y = 0; y < scaleH; ++y) {
            const uchar* row = srcBits + y * srcW * 4;
            uchar* dRow = diff.ptr<uchar>(y);
            for (int x = 0; x < scaleW; ++x) {
                dRow[x] = colorMatches(row + x * 4, sR, sG, sB, sA, tolerance) ? 255 : 0;
            }
        }

        QImage temp(scaleW, scaleH, QImage::Format_Grayscale8);

        if (contiguous) {
            cv::Mat flood = diff.clone();
            cv::floodFill(flood, cv::Point(seedX, seedY), cv::Scalar(128),
                          nullptr, cv::Scalar(0), cv::Scalar(0));
            for (int y = 0; y < scaleH; ++y) {
                uchar* dst = temp.scanLine(y);
                const uchar* src = flood.ptr<uchar>(y);
                for (int x = 0; x < scaleW; ++x)
                    dst[x] = (src[x] == 128) ? 255 : 0;
            }
        } else {
            for (int y = 0; y < scaleH; ++y) {
                uchar* dst = temp.scanLine(y);
                const uchar* src = diff.ptr<uchar>(y);
                std::copy(src, src + scaleW, dst);
            }
        }

        combineMask(m_mask, temp, mode);
    }

    m_active = true;
    m_cacheValid = false;
}

void SelectionMask::invert()
{
    if (m_mask.isNull()) return;

    for (int y = 0; y < m_mask.height(); ++y) {
        uchar* row = m_mask.scanLine(y);
        for (int x = 0; x < m_mask.width(); ++x)
            row[x] = 255 - row[x];
    }
    m_active = true;
    m_cacheValid = false;
}

void SelectionMask::feather(float radius)
{
    if (m_mask.isNull() || radius <= 0.0f) return;

    int ksize = static_cast<int>(2 * std::ceil(radius) + 1);
    cv::Mat mask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));
    cv::GaussianBlur(mask, mask, cv::Size(ksize, ksize), radius);
    m_cacheValid = false;
}

void SelectionMask::grow(int pixels)
{
    if (m_mask.isNull() || pixels <= 0) return;

    cv::Mat mask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
        cv::Size(2 * pixels + 1, 2 * pixels + 1));
    cv::dilate(mask, mask, kernel);
    m_cacheValid = false;
}

void SelectionMask::shrink(int pixels)
{
    if (m_mask.isNull() || pixels <= 0) return;

    cv::Mat mask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
        cv::Size(2 * pixels + 1, 2 * pixels + 1));
    cv::erode(mask, mask, kernel);
    m_cacheValid = false;
}

void SelectionMask::border(int pixels)
{
    if (m_mask.isNull() || pixels <= 0) return;

    cv::Mat mask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
        cv::Size(2 * pixels + 1, 2 * pixels + 1));
    cv::Mat dilated;
    cv::dilate(mask, dilated, kernel);
    cv::Mat borderOnly = dilated - mask;
    borderOnly.copyTo(mask);
    m_cacheValid = false;
}

void SelectionMask::smooth(float radius)
{
    if (m_mask.isNull() || radius <= 0.0f) return;

    int ksize = static_cast<int>(2 * std::ceil(radius) + 1);
    cv::Mat mask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
        cv::Size(ksize, ksize));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    m_cacheValid = false;
}

void SelectionMask::translate(int dx, int dy)
{
    if (m_mask.isNull() || (dx == 0 && dy == 0)) return;

    int w = m_mask.width();
    int h = m_mask.height();

    cv::Mat mask(h, w, CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));
    cv::Mat shifted = cv::Mat::zeros(h, w, CV_8UC1);
    cv::Mat M = (cv::Mat_<float>(2, 3) << 1, 0, static_cast<float>(dx),
                                            0, 1, static_cast<float>(dy));
    cv::warpAffine(mask, shifted, M, mask.size(),
                   cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));
    shifted.copyTo(mask);
    m_cacheValid = false;
}

void SelectionMask::applyTransform(float scaleX, float scaleY, float angleDeg,
                                   int centerX, int centerY)
{
    if (m_mask.isNull()) return;

    int w = m_mask.width();
    int h = m_mask.height();

    cv::Mat mask(h, w, CV_8UC1, m_mask.bits(),
                 static_cast<size_t>(m_mask.bytesPerLine()));

    cv::Point2f center(static_cast<float>(centerX),
                       static_cast<float>(centerY));
    cv::Mat rot = cv::getRotationMatrix2D(center, angleDeg, 1.0);
    rot.at<double>(0, 0) *= scaleX;
    rot.at<double>(0, 1) *= scaleX;
    rot.at<double>(1, 0) *= scaleY;
    rot.at<double>(1, 1) *= scaleY;

    // Adjust translation to keep center-aligned after scale
    rot.at<double>(0, 2) += center.x * (1.0 - scaleX);
    rot.at<double>(1, 2) += center.y * (1.0 - scaleY);

    cv::Mat result = cv::Mat::zeros(h, w, CV_8UC1);
    cv::warpAffine(mask, result, rot, mask.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    result.copyTo(mask);
    m_cacheValid = false;
}

void SelectionMask::resize(int w, int h)
{
    if (m_mask.isNull()) {
        create(w, h);
        return;
    }

    QImage newMask(w, h, QImage::Format_Grayscale8);
    newMask.fill(0);

    int cw = std::min(w, m_mask.width());
    int ch = std::min(h, m_mask.height());
    for (int y = 0; y < ch; ++y) {
        const uchar* src = m_mask.constScanLine(y);
        uchar* dst = newMask.scanLine(y);
        std::copy(src, src + cw, dst);
    }

    m_mask = newMask;
    m_cacheValid = false;
}

void SelectionMask::updateCache() const
{
    m_cachedEmpty = true;
    m_cachedBounds = QRectF();
    m_cacheValid = true;

    if (m_mask.isNull()) return;

    int w = m_mask.width();
    int h = m_mask.height();
    int minX = w, minY = h, maxX = -1, maxY = -1;

    for (int y = 0; y < h; ++y) {
        const uchar* row = m_mask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (row[x] > 0) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX >= 0) {
        m_cachedEmpty = false;
        m_cachedBounds = QRectF(minX, minY, maxX - minX + 1, maxY - minY + 1);
    }
}

QRectF SelectionMask::bounds() const
{
    if (m_mask.isNull()) return {};
    if (!m_cacheValid) updateCache();
    return m_cachedBounds;
}

bool SelectionMask::isEmpty() const
{
    if (m_mask.isNull()) return true;
    if (!m_cacheValid) updateCache();
    return m_cachedEmpty;
}

bool SelectionMask::isSelected(int x, int y) const
{
    if (m_mask.isNull()) return false;
    if (x < 0 || x >= m_mask.width() || y < 0 || y >= m_mask.height())
        return false;
    return m_mask.constScanLine(y)[x] > 0;
}
