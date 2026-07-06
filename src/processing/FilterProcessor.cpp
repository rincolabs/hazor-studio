#include "FilterProcessor.hpp"
#include "core/Tile.hpp"
#include "engine/ImageEngine.hpp"
#include "memory/BufferPool.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cstring>
#include <QDebug>

namespace processing {

// ── Classification helpers ─────────────────────────────────────

bool FilterProcessor::isTileable(const std::string& toolName)
{
    // Transform operations (geometry-changing) are not tileable
    // Global operations (need full histogram or global stats) are not tileable
    static const std::vector<std::string> nonTileable = {
        "crop", "rotate", "flip_horizontal", "flip_vertical",
        "resize_layer", "auto_contrast", "remove_background",
        "new_document", "resize_document", "add_layer", "remove_layer",
        "duplicate_layer", "merge_visible", "merge_down",
        "merge_layers", "flatten_image", "rasterize_layer",
        "fill_layer",  // fill_layer is per-pixel but needs special handling
        // Radial/zoom blur warp the whole image around a global center, so they
        // cannot be computed correctly from isolated padded tiles.
        "radial_blur", "zoom_blur"
    };

    for (const auto& nt : nonTileable)
        if (toolName == nt)
            return false;

    // Everything else is tileable (adjustments, filters, effects)
    return true;
}

bool FilterProcessor::isNeighborhoodFilter(const std::string& toolName)
{
    return toolName == "gaussian_blur"
        || toolName == "median_blur"
        || toolName == "sharpen"
        || toolName == "noise_reduce"
        || toolName == "edge_detect"
        || toolName == "box_blur"
        || toolName == "bilateral_blur"
        || toolName == "motion_blur";
}

int FilterProcessor::kernelRadius(const std::string& toolName,
                                   const QVariantMap& params)
{
    if (toolName == "gaussian_blur") {
        double radius = params.value("radius", 3.0).toDouble();
        int ksize = static_cast<int>(std::max(1.0, radius * 2.0 + 1.0));
        if (ksize % 2 == 0) ++ksize;
        return ksize / 2 + 1; // extra margin for safety
    }
    if (toolName == "median_blur") {
        int ksize = params.value("kernel_size", 5).toInt();
        if (ksize % 2 == 0) ++ksize;
        return ksize / 2 + 1;
    }
    if (toolName == "sharpen") {
        // Uses internal GaussianBlur 5x5
        return 3;
    }
    if (toolName == "noise_reduce") {
        double strength = params.value("strength", 2.0).toDouble();
        int d = static_cast<int>(strength * 2.0 + 1.0);
        if (d % 2 == 0) ++d;
        return d / 2 + 2;
    }
    if (toolName == "edge_detect") {
        // Canny uses internal Sobel 3x3
        return 2;
    }
    if (toolName == "box_blur") {
        int radius = params.value("radius", 3).toInt();
        return std::max(0, radius) + 1;
    }
    if (toolName == "bilateral_blur") {
        int d = params.value("diameter", 9).toInt();
        if (d % 2 == 0) ++d;
        return d / 2 + 1;
    }
    if (toolName == "motion_blur") {
        int length = params.value("length", 15).toInt();
        // Kernel is length×length; half its diagonal bounds the reach.
        return length / 2 + 1;
    }
    return 0;
}

// ── Separable Gaussian blur (two 1D passes, preserves alpha) ──

static void applySeparableBlurInPlace(cv::Mat& bgra, float radius)
{
    int ksize = static_cast<int>(std::max(1.0f, radius * 2.0f + 1.0f));
    if (ksize % 2 == 0) ++ksize;
    if (ksize <= 1 || bgra.empty()) return;

    if (bgra.channels() != 4) {
        cv::Mat temp;
        cv::GaussianBlur(bgra, temp, cv::Size(ksize, 1), radius, 0, cv::BORDER_REPLICATE);
        cv::GaussianBlur(temp, bgra, cv::Size(1, ksize), 0, radius, cv::BORDER_REPLICATE);
        return;
    }

    std::vector<cv::Mat> ch(4);
    cv::split(bgra, ch);

    // Blur in premultiplied-alpha space: a straight blur drags the (black)
    // colour of transparent pixels into opaque edges, leaving a dark halo. The
    // alpha channel is blurred too so soft shapes (brush dabs) grow/soften past
    // their original silhouette instead of being clipped to the dab bounds.
    cv::Mat alphaF;
    ch[3].convertTo(alphaF, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> work(4);
    for (int i = 0; i < 3; ++i) {
        cv::Mat cf;
        ch[i].convertTo(cf, CV_32F, 1.0 / 255.0);
        work[i] = cf.mul(alphaF);              // premultiplied colour
    }
    work[3] = alphaF;

    // Two 1D passes per channel (incl. premultiplied alpha): O(2k·N) vs O(k²·N).
    for (int i = 0; i < 4; ++i) {
        cv::Mat temp;
        cv::GaussianBlur(work[i], temp, cv::Size(ksize, 1), radius, 0, cv::BORDER_REPLICATE);
        cv::GaussianBlur(temp, work[i], cv::Size(1, ksize), 0, radius, cv::BORDER_REPLICATE);
    }

    for (int i = 0; i < 3; ++i) {
        cv::divide(work[i], work[3], work[i]);  // unpremultiply (A==0 → 0)
        work[i].convertTo(ch[i], CV_8U, 255.0); // saturates to [0,255]
    }
    work[3].convertTo(ch[3], CV_8U, 255.0);

    cv::merge(ch, bgra);
}

// ── Apply tool on cv::Mat (private helper) ─────────────────────

static void applyToolOnMat(cv::Mat& mat,
                            const std::string& toolName,
                            const QVariantMap& params)
{
    auto d = [&](const QString& key, double def = 0.0) -> double {
        return params.value(key, def).toDouble();
    };
    auto i = [&](const QString& key, int def = 0) -> int {
        return params.value(key, def).toInt();
    };

    if (toolName == "adjust_brightness")
        mat = ImageEngine::adjustBrightness(mat, static_cast<float>(d("value", 0.0)));
    else if (toolName == "adjust_contrast")
        mat = ImageEngine::adjustContrast(mat, static_cast<float>(d("value", 0.0)));
    else if (toolName == "adjust_saturation")
        mat = ImageEngine::adjustSaturation(mat, static_cast<float>(d("value", 0.0)));
    else if (toolName == "adjust_hue")
        mat = ImageEngine::adjustHue(mat, static_cast<float>(d("value", 0.0)));
    else if (toolName == "gaussian_blur") {
        float radius = static_cast<float>(d("radius", 3.0));
        if (radius > 2.0f)
            applySeparableBlurInPlace(mat, radius);
        else
            mat = ImageEngine::gaussianBlur(mat, radius);
    }
    else if (toolName == "sharpen")
        mat = ImageEngine::sharpen(mat, static_cast<float>(d("strength", 1.0)));
    else if (toolName == "median_blur")
        mat = ImageEngine::medianBlur(mat, i("kernel_size", 5));
    else if (toolName == "box_blur")
        mat = ImageEngine::boxBlur(mat, i("radius", 3));
    else if (toolName == "bilateral_blur")
        mat = ImageEngine::bilateralBlur(mat, i("diameter", 9),
            d("sigma_color", 75.0), d("sigma_space", 75.0));
    else if (toolName == "motion_blur")
        mat = ImageEngine::motionBlur(mat, i("length", 15), d("angle", 0.0));
    else if (toolName == "radial_blur")
        mat = ImageEngine::radialBlur(mat, d("amount", 0.25),
            d("center_x", 0.5), d("center_y", 0.5), i("samples", 12));
    else if (toolName == "zoom_blur")
        mat = ImageEngine::zoomBlur(mat, d("amount", 0.25),
            d("center_x", 0.5), d("center_y", 0.5), i("samples", 12));
    else if (toolName == "edge_detect")
        mat = ImageEngine::edgeDetect(mat,
            static_cast<float>(d("threshold1", 50.0)),
            static_cast<float>(d("threshold2", 150.0)));
    else if (toolName == "grayscale")
        mat = ImageEngine::grayscale(mat);
    else if (toolName == "invert_colors")
        mat = ImageEngine::invertColors(mat);
    else if (toolName == "noise_reduce")
        mat = ImageEngine::noiseReduce(mat, static_cast<float>(d("strength", 2.0)));
    else if (toolName == "posterize")
        mat = ImageEngine::posterize(mat, i("levels", 8));
    else if (toolName == "threshold")
        mat = ImageEngine::threshold(mat, d("value", 128.0));
    else if (toolName == "auto_contrast")
        mat = ImageEngine::autoContrast(mat);
    else if (toolName == "remove_background")
        mat = ImageEngine::removeBackground(mat);
    else if (toolName == "adjust_color") {
        float brightness = static_cast<float>(d("brightness", 0.0));
        float contrast   = static_cast<float>(d("contrast",   0.0));
        float saturation = static_cast<float>(d("saturation", 0.0));
        float hue        = static_cast<float>(d("hue",        0.0));
        bool  autoContr  = d("auto_contrast", 0.0) != 0.0;
        if (autoContr)          mat = ImageEngine::autoContrast(mat);
        if (brightness != 0.0f) mat = ImageEngine::adjustBrightness(mat, brightness);
        if (contrast   != 0.0f) mat = ImageEngine::adjustContrast(mat,   contrast);
        if (saturation != 0.0f) mat = ImageEngine::adjustSaturation(mat, saturation);
        if (hue        != 0.0f) mat = ImageEngine::adjustHue(mat,        hue);
    }
}

// ── Apply chain of tools on a single cv::Mat ───────────────────

static void applyChainOnMat(cv::Mat& mat,
                             const std::vector<std::pair<std::string, QVariantMap>>& chain)
{
    for (const auto& [toolName, params] : chain)
        applyToolOnMat(mat, toolName, params);
}

// ── Point filter: apply directly on QImage region (zero-copy) ──

static void applyPointFilterOnQImageRegion(QImage& image, const QRect& rect,
                                            const std::string& toolName,
                                            const QVariantMap& params)
{
    if (rect.isEmpty() || image.isNull()) return;
    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);

    // Convert the region to BGRA in-place
    cv::Mat fullMat = cv::Mat(image.height(), image.width(), CV_8UC4,
                              image.bits(),
                              static_cast<size_t>(image.bytesPerLine()));
    cv::Mat tileMat(fullMat, cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()));

    // RGBA → BGRA in-place
    cv::cvtColor(tileMat, tileMat, cv::COLOR_RGBA2BGRA);

    // Apply filter on BGRA region
    applyToolOnMat(tileMat, toolName, params);

    // BGRA → RGBA in-place
    cv::cvtColor(tileMat, tileMat, cv::COLOR_BGRA2RGBA);
}

// ── Public API ─────────────────────────────────────────────────

int FilterProcessor::processTiles(QImage& image,
                                   const std::vector<core::Tile*>& tiles,
                                   const std::string& toolName,
                                   const QVariantMap& params)
{
    if (image.isNull() || image.format() != QImage::Format_RGBA8888)
        return 0;

    int radius = kernelRadius(toolName, params);
    int iw = image.width();
    int ih = image.height();
    int processed = 0;

    auto& pool = memory::threadLocalPool();

    for (auto* tile : tiles) {
        if (!tile) continue;

        QRect b = tile->bounds;
        if (b.isEmpty()) continue;

        // ── Point filter fast path (no padding, zero-copy) ──
        if (radius == 0) {
            QRect clipped = b.intersected(QRect(0, 0, iw, ih));
            if (clipped.isEmpty()) continue;
            applyPointFilterOnQImageRegion(image, clipped, toolName, params);
            tile->markDirty();
            ++processed;
            continue;
        }

        // ── Neighborhood filter path (needs padding) ──
        QRect srcRect = b.adjusted(-radius, -radius, radius, radius);
        srcRect = srcRect.intersected(QRect(0, 0, iw, ih));
        if (srcRect.isEmpty()) continue;

        // Extract region using BufferPool to reduce allocations
        QImage srcRegion = pool.acquireImage(srcRect.width(), srcRect.height(),
                                              QImage::Format_RGBA8888);
        for (int y = 0; y < srcRect.height(); ++y) {
            std::memcpy(srcRegion.scanLine(y),
                        image.constScanLine(srcRect.y() + y) + srcRect.x() * 4,
                        static_cast<size_t>(srcRect.width()) * 4);
        }

        cv::Mat mat = ImageEngine::toCvMatFast(srcRegion);
        if (mat.empty()) { pool.release(srcRegion); continue; }

        applyToolOnMat(mat, toolName, params);
        if (mat.empty()) { pool.release(srcRegion); continue; }

        QImage result = ImageEngine::toQImageFast(mat);
        if (result.isNull()) { pool.release(srcRegion); continue; }

        // Copy cropped result back to original image
        int ox = b.x() - srcRect.x();
        int oy = b.y() - srcRect.y();
        int ow = std::min(b.width(), result.width() - ox);
        int oh = std::min(b.height(), result.height() - oy);

        if (ow > 0 && oh > 0) {
            for (int y = 0; y < oh; ++y) {
                const uchar* srcRow = result.constScanLine(oy + y) + ox * 4;
                uchar* dstRow = image.scanLine(b.y() + y) + b.x() * 4;
                std::memcpy(dstRow, srcRow, static_cast<size_t>(ow) * 4);
            }
        }

        pool.release(srcRegion);
        tile->markDirty();
        ++processed;
    }

    return processed;
}

int FilterProcessor::processRect(QImage& image,
                                  int imageW, int imageH,
                                  const QRect& pixelRect,
                                  int tileSize,
                                  const std::vector<core::Tile*>& intersectingTiles,
                                  const std::string& toolName,
                                  const QVariantMap& params)
{
    Q_UNUSED(imageW);
    Q_UNUSED(imageH);
    Q_UNUSED(pixelRect);
    Q_UNUSED(tileSize);

    if (intersectingTiles.empty())
        return 0;

    return processTiles(image, intersectingTiles, toolName, params);
}

QImage FilterProcessor::processFull(const QImage& image,
                                     const std::string& toolName,
                                     const QVariantMap& params)
{
    if (image.isNull())
        return {};

    cv::Mat mat = ImageEngine::toCvMatFast(image);
    if (mat.empty()) { return {}; }

    applyToolOnMat(mat, toolName, params);
    if (mat.empty()) { return image; }

    QImage result = ImageEngine::toQImageFast(mat);
    return result;
}

QImage FilterProcessor::processBatch(
    const QImage& image,
    const std::vector<std::pair<std::string, QVariantMap>>& chain)
{
    if (image.isNull() || chain.empty())
        return {};

    cv::Mat mat = ImageEngine::toCvMatFast(image);
    if (mat.empty()) return {};

    applyChainOnMat(mat, chain);
    if (mat.empty()) return image;

    return ImageEngine::toQImageFast(mat);
}

int FilterProcessor::processBatchTiles(
    QImage& image,
    const std::vector<core::Tile*>& tiles,
    const std::vector<std::pair<std::string, QVariantMap>>& chain)
{
    if (image.isNull() || chain.empty() || image.format() != QImage::Format_RGBA8888)
        return 0;

    int iw = image.width();
    int ih = image.height();
    int processed = 0;

    // Find the widest kernel radius across the chain
    int maxRadius = 0;
    for (const auto& [toolName, params] : chain)
        maxRadius = std::max(maxRadius, kernelRadius(toolName, params));

    auto& pool = memory::threadLocalPool();
    bool allPoint = (maxRadius == 0);

    for (auto* tile : tiles) {
        if (!tile) continue;
        QRect b = tile->bounds;
        if (b.isEmpty()) continue;

        if (allPoint) {
            QRect clipped = b.intersected(QRect(0, 0, iw, ih));
            if (clipped.isEmpty()) continue;
            // Apply chain in-place on the QImage region (one RGBA↔BGRA round-trip)
            cv::Mat fullMat = cv::Mat(image.height(), image.width(), CV_8UC4,
                                      image.bits(),
                                      static_cast<size_t>(image.bytesPerLine()));
            cv::Mat tileMat(fullMat, cv::Rect(clipped.x(), clipped.y(),
                                               clipped.width(), clipped.height()));
            cv::cvtColor(tileMat, tileMat, cv::COLOR_RGBA2BGRA);
            applyChainOnMat(tileMat, chain);
            cv::cvtColor(tileMat, tileMat, cv::COLOR_BGRA2RGBA);
            tile->markDirty();
            ++processed;
            continue;
        }

        // Neighborhood filter chain: extract, apply all, copy back
        QRect srcRect = b.adjusted(-maxRadius, -maxRadius, maxRadius, maxRadius);
        srcRect = srcRect.intersected(QRect(0, 0, iw, ih));
        if (srcRect.isEmpty()) continue;

        QImage srcRegion = pool.acquireImage(srcRect.width(), srcRect.height(),
                                              QImage::Format_RGBA8888);
        for (int y = 0; y < srcRect.height(); ++y) {
            std::memcpy(srcRegion.scanLine(y),
                        image.constScanLine(srcRect.y() + y) + srcRect.x() * 4,
                        static_cast<size_t>(srcRect.width()) * 4);
        }

        cv::Mat mat = ImageEngine::toCvMatFast(srcRegion);
        if (mat.empty()) { pool.release(srcRegion); continue; }

        applyChainOnMat(mat, chain);
        if (mat.empty()) { pool.release(srcRegion); continue; }

        QImage result = ImageEngine::toQImageFast(mat);
        if (result.isNull()) { pool.release(srcRegion); continue; }

        int ox = b.x() - srcRect.x();
        int oy = b.y() - srcRect.y();
        int ow = std::min(b.width(), result.width() - ox);
        int oh = std::min(b.height(), result.height() - oy);

        if (ow > 0 && oh > 0) {
            for (int y = 0; y < oh; ++y) {
                const uchar* srcRow = result.constScanLine(oy + y) + ox * 4;
                uchar* dstRow = image.scanLine(b.y() + y) + b.x() * 4;
                std::memcpy(dstRow, srcRow, static_cast<size_t>(ow) * 4);
            }
        }

        pool.release(srcRegion);
        tile->markDirty();
        ++processed;
    }

    return processed;
}

} // namespace processing
