#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <QImage>
#include <QSize>
#include <QRect>
#include <QColor>
#include <string>
#include <vector>

class ImageEngine {
public:
    ImageEngine() = delete;

    static QImage toQImage(const cv::Mat& mat);
    static cv::Mat toCvMat(const QImage& image);

    // Optimized bridges (fewer allocations).
    // toCvMatFast: clones once, then does cvtColor in-place (1 alloc vs 3).
    static cv::Mat toCvMatFast(const QImage& image);
    // toQImageFast: clones cv::Mat, cvtColor in-place, then wraps + copies to QImage (1 copy vs 2).
    static QImage toQImageFast(const cv::Mat& mat);

    // Zero-copy wrappers (modifies source in-place).
    // wrapCvMat: wraps QImage buffer as cv::Mat without copying.
    static cv::Mat wrapCvMat(QImage& image);
    // wrapQImage: wraps cv::Mat buffer as QImage without copying (cvtColor in-place).
    // The cv::Mat must outlive the returned QImage.
    static QImage wrapQImage(cv::Mat& mat);

    static cv::Mat adjustBrightness(const cv::Mat& input, float value);
    static cv::Mat adjustContrast(const cv::Mat& input, float value);
    static cv::Mat adjustSaturation(const cv::Mat& input, float value);
    static cv::Mat adjustHue(const cv::Mat& input, float value);

    static cv::Mat gaussianBlur(const cv::Mat& input, float radius);
    static cv::Mat sharpen(const cv::Mat& input, float strength);
    static cv::Mat medianBlur(const cv::Mat& input, int ksize);
    static cv::Mat edgeDetect(const cv::Mat& input, float threshold1, float threshold2);

    // New blur filters (all alpha-preserving, premultiplied, BORDER_REPLICATE).
    static cv::Mat boxBlur(const cv::Mat& input, int radius);
    static cv::Mat bilateralBlur(const cv::Mat& input, int diameter,
                                 double sigmaColor, double sigmaSpace);
    static cv::Mat motionBlur(const cv::Mat& input, int length, double angleDeg);
    // center is normalized 0..1 relative to the image bounds.
    static cv::Mat radialBlur(const cv::Mat& input, double amount,
                              double centerX, double centerY, int samples);
    static cv::Mat zoomBlur(const cv::Mat& input, double amount,
                            double centerX, double centerY, int samples);

    // Motion blur kernel: square length×length, central horizontal line rotated
    // by angleDeg, normalized to sum 1.0. Exposed for testing.
    static cv::Mat motionBlurKernel(int length, double angleDeg);

    static cv::Mat crop(const cv::Mat& input, int x, int y, int w, int h);
    static cv::Mat rotate(const cv::Mat& input, float angleDeg);
    static cv::Mat flipHorizontal(const cv::Mat& input);
    static cv::Mat flipVertical(const cv::Mat& input);
    static cv::Mat resize(const cv::Mat& input, int w, int h);

    // ── Resampling layer pixels ──────────────────────────────────────────
    //
    // Any filter that mixes neighbouring pixels must run on *premultiplied*
    // alpha. With straight alpha the colour stored in a fully transparent
    // pixel is arbitrary (usually black) yet still gets its share of the
    // weight, so every soft or antialiased edge picks up a dark fringe —
    // the same defect the projection mip pyramid was built to avoid
    // (docs/RENDER-PIPELINE.md §4.2).
    //
    // The round trip is done in 8 bits, matching Qt's
    // Format_RGBA8888_Premultiplied and ProjectionCache's upload, so the
    // whole 8-bit pipeline stays consistent. It costs precision in the
    // colour of near-transparent pixels, which is where it is least
    // visible; a float path belongs with the linear-light work (P6), not
    // here.
    //
    // 4-channel CV_8U input is premultiplied; anything else (notably
    // single-channel masks, which have no alpha to premultiply by) is
    // forwarded to OpenCV untouched. Do NOT route mask warps through these
    // "for consistency" — it would be a regression.
    static void premultiplyInPlace(cv::Mat& bgra8);
    static void unpremultiplyInPlace(cv::Mat& bgra8);

    // `flags` takes the usual cv:: interpolation bits (plus WARP_INVERSE_MAP).
    // `borderValue` is read in premultiplied space; the transparent default
    // (0,0,0,0) means the same thing in both spaces.
    static cv::Mat warpAffinePremultiplied(const cv::Mat& input, const cv::Mat& m,
                                           cv::Size dsize, int flags,
                                           int borderMode = cv::BORDER_CONSTANT,
                                           const cv::Scalar& borderValue = cv::Scalar());
    static cv::Mat resizePremultiplied(const cv::Mat& input, cv::Size dsize,
                                       int interpolation);

    static cv::Mat grayscale(const cv::Mat& input);
    static cv::Mat invertColors(const cv::Mat& input);
    static cv::Mat autoContrast(const cv::Mat& input);
    static cv::Mat noiseReduce(const cv::Mat& input, float strength);
    static cv::Mat posterize(const cv::Mat& input, int levels);
    static cv::Mat threshold(const cv::Mat& input, double value);
    static cv::Mat removeBackground(const cv::Mat& input);

    static cv::Mat fillRegion(const cv::Mat& input, int x, int y, const cv::Scalar& color, float tolerance = 0.0f);

    static cv::Mat compositeLayers(const std::vector<cv::Mat>& layers,
                                    const std::vector<float>& opacities,
                                    const std::vector<bool>& visibilities,
                                    QSize canvasSize);

    static cv::Scalar qColorToScalar(const QColor& c);
    static QColor scalarToQColor(const cv::Scalar& s);

    static bool isAvailable() { return true; }

private:
    static void ensureRGBA(cv::Mat& mat);
};
