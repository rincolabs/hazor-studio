#include "ImageEngine.hpp"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <functional>
#include <cmath>

static std::vector<cv::Mat> splitBgra(const cv::Mat& input)
{
    std::vector<cv::Mat> ch;
    cv::split(input, ch);
    return ch;
}

static cv::Mat mergeWithAlpha(const std::vector<cv::Mat>& bgrChannels, const cv::Mat& alpha)
{
    std::vector<cv::Mat> outCh = {bgrChannels[0], bgrChannels[1], bgrChannels[2], alpha};
    cv::Mat out;
    cv::merge(outCh, out);
    return out;
}

static cv::Mat processBgrOnly(const cv::Mat& input,
                               std::function<cv::Mat(const cv::Mat&)> fn)
{
    auto ch = splitBgra(input);
    cv::Mat bgr;
    cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);
    cv::Mat result = fn(bgr);
    auto resultCh = splitBgra(result);
    return mergeWithAlpha(resultCh, ch[3]);
}

QImage ImageEngine::toQImage(const cv::Mat& mat)
{
    if (mat.empty())
        return {};

    cv::Mat tmp;
    if (mat.channels() == 4)
        cv::cvtColor(mat, tmp, cv::COLOR_BGRA2RGBA);
    else if (mat.channels() == 3)
        cv::cvtColor(mat, tmp, cv::COLOR_BGR2RGB);
    else
        tmp = mat;

    return QImage(tmp.data, tmp.cols, tmp.rows,
                  static_cast<int>(tmp.step),
                  QImage::Format_RGBA8888).copy();
}

cv::Mat ImageEngine::toCvMat(const QImage& image)
{
    if (image.isNull())
        return {};

    QImage conv = image.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat mat(conv.height(), conv.width(), CV_8UC4,
                const_cast<uchar*>(conv.constBits()),
                static_cast<size_t>(conv.bytesPerLine()));
    cv::Mat out;
    cv::cvtColor(mat, out, cv::COLOR_RGBA2BGRA);
    return out.clone();
}

cv::Mat ImageEngine::toCvMatFast(const QImage& image)
{
    if (image.isNull()) return {};
    QImage conv = image;
    if (conv.format() != QImage::Format_RGBA8888)
        conv = conv.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat wrapped(conv.height(), conv.width(), CV_8UC4,
                    const_cast<uchar*>(conv.constBits()),
                    static_cast<size_t>(conv.bytesPerLine()));
    cv::Mat result = wrapped.clone();
    cv::cvtColor(result, result, cv::COLOR_RGBA2BGRA);
    return result;
}

QImage ImageEngine::toQImageFast(const cv::Mat& mat)
{
    if (mat.empty()) return {};
    cv::Mat tmp = mat.clone();
    if (tmp.channels() == 4)
        cv::cvtColor(tmp, tmp, cv::COLOR_BGRA2RGBA);
    else if (tmp.channels() == 3)
        cv::cvtColor(tmp, tmp, cv::COLOR_BGR2RGB);
    return QImage(tmp.data, tmp.cols, tmp.rows,
                  static_cast<int>(tmp.step),
                  QImage::Format_RGBA8888).copy();
}

cv::Mat ImageEngine::wrapCvMat(QImage& image)
{
    if (image.isNull()) return {};
    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);
    return cv::Mat(image.height(), image.width(), CV_8UC4,
                   image.bits(),
                   static_cast<size_t>(image.bytesPerLine()));
}

QImage ImageEngine::wrapQImage(cv::Mat& mat)
{
    if (mat.empty()) return {};
    if (mat.channels() == 4)
        cv::cvtColor(mat, mat, cv::COLOR_BGRA2RGBA);
    else if (mat.channels() == 3)
        cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);
    return QImage(mat.data, mat.cols, mat.rows,
                  static_cast<int>(mat.step),
                  QImage::Format_RGBA8888);
}

void ImageEngine::ensureRGBA(cv::Mat& mat)
{
    if (mat.channels() == 3)
        cv::cvtColor(mat, mat, cv::COLOR_BGR2BGRA);
    else if (mat.channels() == 1)
        cv::cvtColor(mat, mat, cv::COLOR_GRAY2BGRA);
}

cv::Mat ImageEngine::adjustBrightness(const cv::Mat& input, float value)
{
    return processBgrOnly(input, [value](const cv::Mat& bgr) {
        cv::Mat out;
        bgr.convertTo(out, -1, 1.0, static_cast<double>(value * 255.0));
        return out;
    });
}

cv::Mat ImageEngine::adjustContrast(const cv::Mat& input, float value)
{
    return processBgrOnly(input, [value](const cv::Mat& bgr) {
        double f = (259.0 * (value + 1.0)) / (259.0 - value);
        cv::Mat out;
        bgr.convertTo(out, -1, f, 128.0 * (1.0 - f));
        return out;
    });
}

cv::Mat ImageEngine::adjustSaturation(const cv::Mat& input, float value)
{
    auto ch = splitBgra(input);
    cv::Mat bgr;
    cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    channels[1] = cv::min(channels[1] * (1.0 + value), 255.0);
    cv::merge(channels, hsv);
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);

    return mergeWithAlpha(splitBgra(bgr), ch[3]);
}

cv::Mat ImageEngine::adjustHue(const cv::Mat& input, float value)
{
    auto ch = splitBgra(input);
    cv::Mat bgr;
    cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    int shift = static_cast<int>(value * 180.0);
    for (int y = 0; y < channels[0].rows; ++y) {
        uchar* row = channels[0].ptr<uchar>(y);
        for (int x = 0; x < channels[0].cols; ++x)
            row[x] = (row[x] + shift) % 180;
    }
    cv::merge(channels, hsv);
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);

    return mergeWithAlpha(splitBgra(bgr), ch[3]);
}

// ── Alpha-aware spatial blurs ───────────────────────────────────────────
// Blurs must run in premultiplied-alpha space. The colour stored in a fully
// transparent pixel is meaningless (typically black), so a straight-alpha blur
// drags that black into the opaque neighbours and leaves a dark halo around
// brush dabs and cut-outs. Filtering the alpha channel too lets a soft shape
// actually grow/soften past its original silhouette instead of being clamped
// to the dab bounds.
static cv::Mat blurGaussianPremultiplied(const cv::Mat& input, float radius)
{
    if (radius <= 0.0f)
        return input.clone();

    int ksize = static_cast<int>(std::ceil(radius)) * 2 + 1;
    if (ksize < 1) ksize = 1;
    if (ksize % 2 == 0) ++ksize;

    if (input.channels() != 4) {
        cv::Mat out;
        cv::GaussianBlur(input, out, cv::Size(ksize, ksize), radius);
        return out;
    }

    cv::Mat f;
    input.convertTo(f, CV_32F, 1.0 / 255.0);          // CV_32FC4
    std::vector<cv::Mat> ch;
    cv::split(f, ch);                                  // B G R A
    for (int c = 0; c < 3; ++c)
        ch[c] = ch[c].mul(ch[3]);                      // premultiply colour by alpha
    cv::Mat pm;
    cv::merge(ch, pm);

    cv::Mat blurred;
    cv::GaussianBlur(pm, blurred, cv::Size(ksize, ksize), radius);

    std::vector<cv::Mat> bch;
    cv::split(blurred, bch);                           // premult B G R, blurred A
    for (int c = 0; c < 3; ++c)
        cv::divide(bch[c], bch[3], bch[c]);            // unpremultiply (A==0 → 0)

    cv::Mat out, out8;
    cv::merge(bch, out);
    out.convertTo(out8, CV_8U, 255.0);                 // saturates to [0,255]
    return out8;
}

static cv::Mat blurMedianPremultiplied(const cv::Mat& input, int ksize)
{
    if (ksize % 2 == 0) ++ksize;
    if (ksize < 3)
        return input.clone();
    if (input.channels() != 4) {
        cv::Mat out;
        cv::medianBlur(input, out, ksize);
        return out;
    }

    std::vector<cv::Mat> ch;
    cv::split(input, ch);                              // 8U B G R A
    cv::Mat alphaF;
    ch[3].convertTo(alphaF, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> pm(4);
    for (int c = 0; c < 3; ++c) {
        cv::Mat cf;
        ch[c].convertTo(cf, CV_32F);
        cf = cf.mul(alphaF);
        cf.convertTo(pm[c], CV_8U);                    // premultiplied colour (8U)
    }
    pm[3] = ch[3];
    cv::Mat pmMat;
    cv::merge(pm, pmMat);

    cv::Mat blurred;
    cv::medianBlur(pmMat, blurred, ksize);             // 8U/4ch supports any aperture

    std::vector<cv::Mat> bch;
    cv::split(blurred, bch);
    cv::Mat bAlphaF;
    bch[3].convertTo(bAlphaF, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> out(4);
    for (int c = 0; c < 3; ++c) {
        cv::Mat cf;
        bch[c].convertTo(cf, CV_32F);
        cv::divide(cf, bAlphaF, cf);                   // unpremultiply (A==0 → 0)
        cf.convertTo(out[c], CV_8U);                   // saturates
    }
    out[3] = bch[3];
    cv::Mat result;
    cv::merge(out, result);
    return result;
}

cv::Mat ImageEngine::gaussianBlur(const cv::Mat& input, float radius)
{
    return blurGaussianPremultiplied(input, radius);
}

cv::Mat ImageEngine::sharpen(const cv::Mat& input, float strength)
{
    return processBgrOnly(input, [strength](const cv::Mat& bgr) {
        cv::Mat blurred;
        cv::GaussianBlur(bgr, blurred, cv::Size(5, 5), 0);
        cv::Mat out;
        cv::addWeighted(bgr, 1.0 + strength, blurred, -strength, 0, out);
        return out;
    });
}

cv::Mat ImageEngine::medianBlur(const cv::Mat& input, int ksize)
{
    return blurMedianPremultiplied(input, ksize);
}

// Run a blur-like op in premultiplied-alpha space so transparent (black) pixels
// don't bleed dark halos into opaque edges. fn receives and returns a CV_32FC4
// premultiplied BGRA mat (alpha kept straight, colour premultiplied by alpha).
static cv::Mat applyPremultiplied(const cv::Mat& input,
                                  const std::function<cv::Mat(const cv::Mat&)>& fn)
{
    if (input.channels() != 4) {
        cv::Mat f;
        input.convertTo(f, CV_32F, 1.0 / 255.0);
        cv::Mat r = fn(f);
        cv::Mat out8;
        r.convertTo(out8, CV_8U, 255.0);
        return out8;
    }

    cv::Mat f;
    input.convertTo(f, CV_32F, 1.0 / 255.0);          // CV_32FC4, 0..1
    std::vector<cv::Mat> ch;
    cv::split(f, ch);                                  // B G R A
    for (int c = 0; c < 3; ++c)
        ch[c] = ch[c].mul(ch[3]);                      // premultiply colour by alpha
    cv::Mat pm;
    cv::merge(ch, pm);

    cv::Mat blurred = fn(pm);                          // premultiplied BGRA in/out

    std::vector<cv::Mat> bch;
    cv::split(blurred, bch);                           // premult B G R, blurred A
    for (int c = 0; c < 3; ++c)
        cv::divide(bch[c], bch[3], bch[c]);            // unpremultiply (A==0 → 0)

    cv::Mat out, out8;
    cv::merge(bch, out);
    out.convertTo(out8, CV_8U, 255.0);                 // saturates to [0,255]
    return out8;
}

cv::Mat ImageEngine::boxBlur(const cv::Mat& input, int radius)
{
    if (radius <= 0)
        return input.clone();
    const int k = radius * 2 + 1;
    return applyPremultiplied(input, [k](const cv::Mat& pm) {
        cv::Mat out;
        cv::blur(pm, out, cv::Size(k, k), cv::Point(-1, -1), cv::BORDER_REPLICATE);
        return out;
    });
}

cv::Mat ImageEngine::bilateralBlur(const cv::Mat& input, int diameter,
                                   double sigmaColor, double sigmaSpace)
{
    if (diameter < 1)
        return input.clone();
    if (diameter % 2 == 0) ++diameter;                 // bilateral wants odd
    diameter = std::min(diameter, 51);
    sigmaColor = std::clamp(sigmaColor, 1.0, 200.0);
    sigmaSpace = std::clamp(sigmaSpace, 1.0, 200.0);

    if (input.channels() != 4) {
        cv::Mat out;
        cv::bilateralFilter(input, out, diameter, sigmaColor, sigmaSpace,
                            cv::BORDER_REPLICATE);
        return out;
    }

    std::vector<cv::Mat> ch;
    cv::split(input, ch);                              // 8U B G R A
    cv::Mat alphaF;
    ch[3].convertTo(alphaF, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> pm(3);
    for (int c = 0; c < 3; ++c) {
        cv::Mat cf;
        ch[c].convertTo(cf, CV_32F);
        cf = cf.mul(alphaF);
        cf.convertTo(pm[c], CV_8U);                    // premultiplied colour (8U)
    }
    cv::Mat bgrPm;
    cv::merge(pm, bgrPm);

    cv::Mat bgrFilt;
    cv::bilateralFilter(bgrPm, bgrFilt, diameter, sigmaColor, sigmaSpace,
                        cv::BORDER_REPLICATE);
    cv::Mat alphaFilt;
    cv::bilateralFilter(ch[3], alphaFilt, diameter, sigmaColor, sigmaSpace,
                        cv::BORDER_REPLICATE);

    cv::Mat alphaFiltF;
    alphaFilt.convertTo(alphaFiltF, CV_32F, 1.0 / 255.0);
    std::vector<cv::Mat> bf;
    cv::split(bgrFilt, bf);

    std::vector<cv::Mat> out(4);
    for (int c = 0; c < 3; ++c) {
        cv::Mat cf;
        bf[c].convertTo(cf, CV_32F);
        cv::divide(cf, alphaFiltF, cf);                // unpremultiply (A==0 → 0)
        cf.convertTo(out[c], CV_8U);
    }
    out[3] = alphaFilt;
    cv::Mat result;
    cv::merge(out, result);
    return result;
}

cv::Mat ImageEngine::motionBlurKernel(int length, double angleDeg)
{
    if (length < 1) length = 1;
    cv::Mat kernel = cv::Mat::zeros(length, length, CV_32F);
    const int mid = length / 2;
    for (int x = 0; x < length; ++x)
        kernel.at<float>(mid, x) = 1.0f;               // central horizontal line

    if (length > 1) {
        const cv::Point2f center(length / 2.0f, length / 2.0f);
        cv::Mat rot = cv::getRotationMatrix2D(center, angleDeg, 1.0);
        cv::Mat rotated;
        cv::warpAffine(kernel, rotated, rot, kernel.size(),
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
        kernel = rotated;
    }

    const double s = cv::sum(kernel)[0];
    if (s > 1e-6) kernel /= s;                         // normalize to sum 1.0
    return kernel;
}

cv::Mat ImageEngine::motionBlur(const cv::Mat& input, int length, double angleDeg)
{
    if (length <= 1)
        return input.clone();
    cv::Mat kernel = motionBlurKernel(length, angleDeg);
    return applyPremultiplied(input, [&kernel](const cv::Mat& pm) {
        cv::Mat out;
        cv::filter2D(pm, out, -1, kernel, cv::Point(-1, -1), 0,
                     cv::BORDER_REPLICATE);
        return out;
    });
}

cv::Mat ImageEngine::radialBlur(const cv::Mat& input, double amount,
                                double centerX, double centerY, int samples)
{
    if (amount <= 0.0)
        return input.clone();
    samples = std::clamp(samples, 2, 64);
    const double maxAngle = 15.0;                      // degrees at amount=1
    const cv::Point2f center(static_cast<float>(centerX * input.cols),
                             static_cast<float>(centerY * input.rows));
    return applyPremultiplied(input, [&](const cv::Mat& pm) {
        cv::Mat accum = cv::Mat::zeros(pm.size(), pm.type());
        for (int i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / (samples - 1) - 0.5; // -0.5..0.5
            const double ang = t * 2.0 * maxAngle * amount;
            cv::Mat rot = cv::getRotationMatrix2D(center, ang, 1.0);
            cv::Mat warped;
            cv::warpAffine(pm, warped, rot, pm.size(),
                           cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            accum += warped;
        }
        accum /= static_cast<double>(samples);
        return accum;
    });
}

cv::Mat ImageEngine::zoomBlur(const cv::Mat& input, double amount,
                              double centerX, double centerY, int samples)
{
    if (amount <= 0.0)
        return input.clone();
    samples = std::clamp(samples, 2, 64);
    const double cx = centerX * input.cols;
    const double cy = centerY * input.rows;
    return applyPremultiplied(input, [&](const cv::Mat& pm) {
        cv::Mat accum = cv::Mat::zeros(pm.size(), pm.type());
        for (int i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / (samples - 1);
            const double scale = 1.0 + t * amount * 0.3; // up to +30% at amount=1
            // Scale about (cx, cy): keeps the centre fixed.
            cv::Mat m = (cv::Mat_<double>(2, 3) <<
                         scale, 0.0, cx * (1.0 - scale),
                         0.0, scale, cy * (1.0 - scale));
            cv::Mat warped;
            cv::warpAffine(pm, warped, m, pm.size(),
                           cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            accum += warped;
        }
        accum /= static_cast<double>(samples);
        return accum;
    });
}

cv::Mat ImageEngine::edgeDetect(const cv::Mat& input, float threshold1, float threshold2)
{
    cv::Mat gray;
    if (input.channels() == 4)
        cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
    else
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

    cv::Mat edges;
    cv::Canny(gray, edges, threshold1, threshold2);

    cv::Mat out;
    cv::cvtColor(edges, out, cv::COLOR_GRAY2BGRA);
    return out;
}

cv::Mat ImageEngine::crop(const cv::Mat& input, int x, int y, int w, int h)
{
    cv::Rect roi(x, y, w, h);
    roi &= cv::Rect(0, 0, input.cols, input.rows);
    return input(roi).clone();
}

cv::Mat ImageEngine::rotate(const cv::Mat& input, float angleDeg)
{
    cv::Mat out;
    cv::Point2f center(input.cols / 2.0f, input.rows / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D(center, angleDeg, 1.0);

    cv::Rect2f bbox = cv::RotatedRect(cv::Point2f(), input.size(), angleDeg).boundingRect2f();
    rot.at<double>(0, 2) += bbox.width / 2.0 - center.x;
    rot.at<double>(1, 2) += bbox.height / 2.0 - center.y;

    cv::warpAffine(input, out, rot, bbox.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

cv::Mat ImageEngine::flipHorizontal(const cv::Mat& input)
{
    cv::Mat out;
    cv::flip(input, out, 1);
    return out;
}

cv::Mat ImageEngine::flipVertical(const cv::Mat& input)
{
    cv::Mat out;
    cv::flip(input, out, 0);
    return out;
}

cv::Mat ImageEngine::resize(const cv::Mat& input, int w, int h)
{
    cv::Mat out;
    cv::resize(input, out, cv::Size(w, h), 0, 0, cv::INTER_AREA);
    return out;
}

cv::Mat ImageEngine::grayscale(const cv::Mat& input)
{
    auto ch = splitBgra(input);
    cv::Mat bgr;
    cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Mat grayBgr;
    cv::cvtColor(gray, grayBgr, cv::COLOR_GRAY2BGR);

    return mergeWithAlpha(splitBgra(grayBgr), ch[3]);
}

cv::Mat ImageEngine::invertColors(const cv::Mat& input)
{
    auto ch = splitBgra(input);
    for (int c = 0; c < 3; ++c)
        cv::bitwise_not(ch[c], ch[c]);
    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

cv::Mat ImageEngine::autoContrast(const cv::Mat& input)
{
    auto ch = splitBgra(input);
    std::vector<cv::Mat> bgrCh = {ch[0], ch[1], ch[2]};

    for (auto& c : bgrCh) {
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};
        cv::Mat hist;
        cv::calcHist(&c, 1, 0, cv::Mat(), hist, 1, &histSize, &histRange);

        float total = c.total();
        float lowThresh  = total * 0.005f;
        float highThresh = total * 0.995f;

        int vmin = 0, vmax = 255, i = 0;
        float sum = 0;
        for (; i < 256; ++i) {
            sum += hist.at<float>(i);
            if (sum >= lowThresh) { vmin = i; break; }
        }
        for (; i < 256; ++i) {
            sum += hist.at<float>(i);
            if (sum >= highThresh) { vmax = i; break; }
        }

        if (vmax <= vmin) continue;

        cv::Mat lut(1, 256, CV_8UC1);
        for (int j = 0; j < 256; ++j) {
            if (j <= vmin)
                lut.at<uchar>(j) = 0;
            else if (j >= vmax)
                lut.at<uchar>(j) = 255;
            else
                lut.at<uchar>(j) = static_cast<uchar>(255 * (j - vmin) / (vmax - vmin));
        }
        cv::LUT(c, lut, c);
    }

    cv::Mat out;
    cv::merge(std::vector<cv::Mat>{bgrCh[0], bgrCh[1], bgrCh[2], ch[3]}, out);
    return out;
}

cv::Mat ImageEngine::noiseReduce(const cv::Mat& input, float strength)
{
    int d = static_cast<int>(strength * 2 + 1);
    if (d % 2 == 0) ++d;

    return processBgrOnly(input, [d, strength](const cv::Mat& bgr) {
        cv::Mat out;
        cv::bilateralFilter(bgr, out, d, strength * 50, strength * 50);
        return out;
    });
}

cv::Mat ImageEngine::posterize(const cv::Mat& input, int levels)
{
    cv::Mat out = input.clone();
    float step = 255.0f / levels;
    out.forEach<cv::Vec4b>([&](cv::Vec4b& p, const int*) {
        for (int c = 0; c < 3; ++c)
            p[c] = static_cast<uchar>(std::round(p[c] / step) * step);
    });
    return out;
}

cv::Mat ImageEngine::threshold(const cv::Mat& input, double value)
{
    auto ch = splitBgra(input);
    cv::Mat bgr;
    cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, gray, value, 255, cv::THRESH_BINARY);
    cv::Mat grayBgr;
    cv::cvtColor(gray, grayBgr, cv::COLOR_GRAY2BGR);

    return mergeWithAlpha(splitBgra(grayBgr), ch[3]);
}

cv::Mat ImageEngine::removeBackground(const cv::Mat& input)
{
    cv::Mat out = input.clone();
    cv::Mat gray;
    cv::cvtColor(out, gray, cv::COLOR_BGRA2GRAY);
    cv::Scalar mean = cv::mean(gray);
    cv::Mat mask;
    cv::threshold(gray, mask, mean[0] * 0.9, 255, cv::THRESH_BINARY_INV);
    for (int y = 0; y < out.rows; ++y)
        for (int x = 0; x < out.cols; ++x)
            if (mask.at<uchar>(y, x) > 0)
                out.at<cv::Vec4b>(y, x)[3] = 0;
    return out;
}

cv::Mat ImageEngine::fillRegion(const cv::Mat& input, int x, int y,
                                 const cv::Scalar& color, float tolerance)
{
    if (input.empty()) return {};
    cv::Mat out = input.clone();
    int lo = static_cast<int>(tolerance * 255);
    int up = static_cast<int>(tolerance * 255);

    if (out.channels() == 4) {
        auto ch = splitBgra(out);
        cv::Mat bgr;
        cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);

        // Use MASK_ONLY to find the filled region without relying on BGR
        // color comparison — this fixes the case where fill color == seed BGR
        // (e.g. black fill on a transparent layer where all BGR values are 0).
        cv::Mat floodMask = cv::Mat::zeros(bgr.rows + 2, bgr.cols + 2, CV_8U);
        cv::floodFill(bgr, floodMask, cv::Point(x, y), cv::Scalar(0),
                      nullptr,
                      cv::Scalar(lo, lo, lo),
                      cv::Scalar(up, up, up),
                      cv::FLOODFILL_MASK_ONLY | cv::FLOODFILL_FIXED_RANGE | (1 << 8));
        // Crop the 1-pixel border added by floodFill mask convention
        cv::Mat fillMask = floodMask(cv::Rect(1, 1, bgr.cols, bgr.rows));

        cv::Scalar bgrColor(color[0], color[1], color[2]);
        bgr.setTo(bgrColor, fillMask);
        ch[3].setTo(cv::Scalar(color[3]), fillMask);

        auto bgrCh = splitBgra(bgr);
        return mergeWithAlpha(bgrCh, ch[3]);
    }

    cv::floodFill(out, cv::Point(x, y), color, nullptr,
                  cv::Scalar(lo, lo, lo),
                  cv::Scalar(up, up, up),
                  cv::FLOODFILL_FIXED_RANGE);
    return out;
}

cv::Mat ImageEngine::compositeLayers(const std::vector<cv::Mat>& layers,
                                      const std::vector<float>& opacities,
                                      const std::vector<bool>& visibilities,
                                      QSize canvasSize)
{
    cv::Mat result = cv::Mat::zeros(canvasSize.height(), canvasSize.width(), CV_8UC4);

    for (size_t i = 0; i < layers.size(); ++i) {
        if (i >= visibilities.size() || !visibilities[i]) continue;
        cv::Mat layer = layers[i].clone();
        ensureRGBA(layer);

        float opacity = (i < opacities.size()) ? opacities[i] : 1.0f;

        cv::Mat resized;
        if (layer.size() != result.size())
            cv::resize(layer, resized, result.size());
        else
            resized = layer;

        cv::Mat srcF;
        resized.convertTo(srcF, CV_32FC4, 1.0 / 255.0);

        if (opacity < 1.0f) {
            std::vector<cv::Mat> rgba(4);
            cv::split(srcF, rgba);
            rgba[3] *= opacity;
            cv::merge(rgba, srcF);
        }

        cv::Mat dstF;
        result.convertTo(dstF, CV_32FC4, 1.0 / 255.0);

        std::vector<cv::Mat> s(4), d(4);
        cv::split(srcF, s);
        cv::split(dstF, d);

        cv::Mat oneMinusSrcA = cv::Scalar(1.0f) - s[3];

        for (int c = 0; c < 3; ++c)
            d[c] = d[c].mul(oneMinusSrcA) + s[c].mul(s[3]);

        d[3] = s[3] + d[3].mul(oneMinusSrcA);

        cv::merge(d, dstF);
        dstF.convertTo(result, CV_8UC4, 255.0);
    }

    return result;
}

cv::Scalar ImageEngine::qColorToScalar(const QColor& c)
{
    return cv::Scalar(c.blue(), c.green(), c.red(), c.alpha());
}

QColor ImageEngine::scalarToQColor(const cv::Scalar& s)
{
    return QColor(static_cast<int>(s[2]),
                  static_cast<int>(s[1]),
                  static_cast<int>(s[0]),
                  static_cast<int>(s[3]));
}
