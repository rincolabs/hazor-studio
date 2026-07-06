#include "PreviewRenderer.hpp"
#include "FilterProcessor.hpp"
#include "engine/ImageEngine.hpp"
#include <QtConcurrent>
#include <QDebug>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace processing {

PreviewRenderer::PreviewRenderer(QObject* parent)
    : QObject(parent)
{
}

PreviewRenderer::~PreviewRenderer()
{
    cancel();
    m_pool.waitForDone(2000);
}

void PreviewRenderer::generatePreview(const QImage& source,
                                       const std::string& toolName,
                                       const QVariantMap& params,
                                       QSize targetSize)
{
    cancel();

    if (source.isNull()) {
        emit previewReady({});
        return;
    }

    m_busy = true;
    m_cancelled = false;

    // Run in thread pool to avoid blocking UI
    auto future = QtConcurrent::run(&m_pool, [this, source, toolName, params, targetSize]() {
        if (m_cancelled) {
            m_busy = false;
            return;
        }

        QImage preview;

        const bool needsDownscale = source.width() > targetSize.width()
                                 || source.height() > targetSize.height();
        // Downscale if source is larger than target
        if (needsDownscale) {
            cv::Mat cvSrc = ImageEngine::toCvMatFast(source);
            if (cvSrc.empty()) {
                m_busy = false;
                return;
            }

            // Compute aspect-preserving downscale size
            double scale = std::min(
                static_cast<double>(targetSize.width())  / cvSrc.cols,
                static_cast<double>(targetSize.height()) / cvSrc.rows);
            int dw = std::max(1, static_cast<int>(cvSrc.cols * scale));
            int dh = std::max(1, static_cast<int>(cvSrc.rows * scale));

            cv::Mat downscaled;
            cv::resize(cvSrc, downscaled, cv::Size(dw, dh), 0, 0, cv::INTER_AREA);

            if (m_cancelled) {
                m_busy = false;
                return;
            }

            // Apply filter on downscaled version
            bool usedFastPath = false;
            cv::Mat result = downscaled.clone();
            if (toolName == "gaussian_blur") {
                result = ImageEngine::gaussianBlur(result,
                    static_cast<float>(params.value("radius", 3.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "sharpen") {
                result = ImageEngine::sharpen(result,
                    static_cast<float>(params.value("strength", 1.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "median_blur") {
                result = ImageEngine::medianBlur(result,
                    params.value("kernel_size", 5).toInt());
                usedFastPath = true;
            } else if (toolName == "box_blur") {
                result = ImageEngine::boxBlur(result,
                    params.value("radius", 3).toInt());
                usedFastPath = true;
            } else if (toolName == "bilateral_blur") {
                result = ImageEngine::bilateralBlur(result,
                    params.value("diameter", 9).toInt(),
                    params.value("sigma_color", 75.0).toDouble(),
                    params.value("sigma_space", 75.0).toDouble());
                usedFastPath = true;
            } else if (toolName == "motion_blur") {
                result = ImageEngine::motionBlur(result,
                    params.value("length", 15).toInt(),
                    params.value("angle", 0.0).toDouble());
                usedFastPath = true;
            } else if (toolName == "radial_blur") {
                result = ImageEngine::radialBlur(result,
                    params.value("amount", 0.25).toDouble(),
                    params.value("center_x", 0.5).toDouble(),
                    params.value("center_y", 0.5).toDouble(),
                    params.value("samples", 12).toInt());
                usedFastPath = true;
            } else if (toolName == "zoom_blur") {
                result = ImageEngine::zoomBlur(result,
                    params.value("amount", 0.25).toDouble(),
                    params.value("center_x", 0.5).toDouble(),
                    params.value("center_y", 0.5).toDouble(),
                    params.value("samples", 12).toInt());
                usedFastPath = true;
            } else if (toolName == "noise_reduce") {
                result = ImageEngine::noiseReduce(result,
                    static_cast<float>(params.value("strength", 2.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "edge_detect") {
                result = ImageEngine::edgeDetect(result,
                    static_cast<float>(params.value("threshold1", 50.0).toDouble()),
                    static_cast<float>(params.value("threshold2", 150.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "grayscale") {
                result = ImageEngine::grayscale(result);
                usedFastPath = true;
            } else if (toolName == "invert_colors") {
                result = ImageEngine::invertColors(result);
                usedFastPath = true;
            } else if (toolName == "posterize") {
                result = ImageEngine::posterize(result,
                    params.value("levels", 8).toInt());
                usedFastPath = true;
            } else if (toolName == "threshold") {
                result = ImageEngine::threshold(result,
                    params.value("value", 128.0).toDouble());
                usedFastPath = true;
            } else if (toolName == "auto_contrast") {
                result = ImageEngine::autoContrast(result);
                usedFastPath = true;
            } else if (toolName == "remove_background") {
                result = ImageEngine::removeBackground(result);
                usedFastPath = true;
            } else if (toolName == "adjust_brightness") {
                result = ImageEngine::adjustBrightness(result,
                    static_cast<float>(params.value("value", 0.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "adjust_contrast") {
                result = ImageEngine::adjustContrast(result,
                    static_cast<float>(params.value("value", 0.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "adjust_saturation") {
                result = ImageEngine::adjustSaturation(result,
                    static_cast<float>(params.value("value", 0.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "adjust_hue") {
                result = ImageEngine::adjustHue(result,
                    static_cast<float>(params.value("value", 0.0).toDouble()));
                usedFastPath = true;
            } else if (toolName == "adjust_color") {
                float brightness = static_cast<float>(params.value("brightness", 0.0).toDouble());
                float contrast   = static_cast<float>(params.value("contrast",   0.0).toDouble());
                float saturation = static_cast<float>(params.value("saturation", 0.0).toDouble());
                float hue        = static_cast<float>(params.value("hue",        0.0).toDouble());
                bool  autoContr  = params.value("auto_contrast", 0.0).toDouble() != 0.0;
                if (autoContr)          result = ImageEngine::autoContrast(result);
                if (brightness != 0.0f) result = ImageEngine::adjustBrightness(result, brightness);
                if (contrast   != 0.0f) result = ImageEngine::adjustContrast(result,   contrast);
                if (saturation != 0.0f) result = ImageEngine::adjustSaturation(result, saturation);
                if (hue        != 0.0f) result = ImageEngine::adjustHue(result,        hue);
                usedFastPath = true;
            } else {
                // Fallback: apply on downscaled via FilterProcessor for performance
                QImage downscaledQ = ImageEngine::toQImageFast(downscaled);
                preview = FilterProcessor::processFull(downscaledQ.isNull() ? source : downscaledQ, toolName, params);
            }

            if (!m_cancelled && !result.empty() && usedFastPath) {
                preview = ImageEngine::toQImageFast(result);
            }
        } else {
            // Source is small enough, apply directly
            preview = FilterProcessor::processFull(source, toolName, params);
        }

        if (m_cancelled) {
            m_busy = false;
            return;
        }

        QImage result = preview;
        QMetaObject::invokeMethod(this, [this, result]() {
            m_busy = false;
            emit previewReady(result);
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
}

void PreviewRenderer::cancel()
{
    m_cancelled = true;
    m_busy = false;
}

} // namespace processing
