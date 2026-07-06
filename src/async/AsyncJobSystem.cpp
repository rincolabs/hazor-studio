#include "AsyncJobSystem.hpp"
#include "engine/ImageEngine.hpp"
#include "core/Layer.hpp"
#include "memory/BufferPool.hpp"

#include <QMetaObject>
#include <QtConcurrent>
#include <QDebug>
#include <algorithm>
#include <QColor>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

AsyncJobSystem* AsyncJobSystem::s_instance = nullptr;

void AsyncJobSystem::create()
{
    if (!s_instance)
        s_instance = new AsyncJobSystem();
}

void AsyncJobSystem::destroy()
{
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

AsyncJobSystem::AsyncJobSystem(QObject* parent)
    : QObject(parent)
{
    PerformanceConfig cfg = PerformanceConfig::fromDefaultSettings();
    m_pool.setMaxThreadCount(cfg.effectiveMaxThreads());
}

AsyncJobSystem::~AsyncJobSystem()
{
    cancelAll();
    m_pool.waitForDone(3000);
}

uint64_t AsyncJobSystem::enqueue(std::shared_ptr<AsyncJob> job)
{
    job->jobId = m_nextJobId++;

    {
        QMutexLocker lock(&m_mutex);

        // Cancel existing jobs for the same weakLayer
        for (auto& j : m_jobs) {
            if (j->weakLayer == job->weakLayer)
                j->cancelled = true;
        }
        auto end = std::remove_if(m_jobs.begin(), m_jobs.end(),
            [](const auto& j) { return j->cancelled.load(); });
        m_jobs.erase(end, m_jobs.end());

        m_jobs.push_back(job);
    }

    auto future = QtConcurrent::run(&m_pool, [this, job]() {
        executeWorker(job);
    });
    Q_UNUSED(future);

    return job->jobId;
}

void AsyncJobSystem::cancelForLayer(const Layer* layer)
{
    QMutexLocker lock(&m_mutex);
    for (auto& j : m_jobs) {
        if (j->weakLayer == layer)
            j->cancelled = true;
    }
    auto end = std::remove_if(m_jobs.begin(), m_jobs.end(),
        [](const auto& j) { return j->cancelled.load(); });
    m_jobs.erase(end, m_jobs.end());
}

void AsyncJobSystem::cancelAll()
{
    QMutexLocker lock(&m_mutex);
    for (auto& j : m_jobs)
        j->cancelled = true;
    m_jobs.clear();
}

int AsyncJobSystem::pendingCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_jobs.size());
}

void AsyncJobSystem::executeWorker(std::shared_ptr<AsyncJob> job)
{
    if (job->cancelled || job->sourceImage.isNull())
        return;

    QMetaObject::invokeMethod(this, [this, jobId = job->jobId]() {
        emit jobStatusChanged(jobId, tr("Processing..."));
        emit jobProgressChanged(jobId, -1);
    }, Qt::QueuedConnection);

    if (job->type == AsyncJobType::FilterApplyProgressive) {
        executeProgressiveWorker(job);
        return;
    }

    cv::Mat mat = ImageEngine::toCvMat(job->sourceImage);
    if (mat.empty()) return;

    if (job->toolName == "delete_selected") {
        cv::Mat mask;
        if (!job->maskImage.isNull()) {
            mask = cv::Mat(job->maskImage.height(), job->maskImage.width(), CV_8UC1,
                           const_cast<uchar*>(job->maskImage.constBits()),
                           static_cast<size_t>(job->maskImage.bytesPerLine()));
        }
        if (!mask.empty())
            mat.setTo(cv::Scalar(0, 0, 0, 0), mask);
        else
            qWarning() << "[DeleteSel] worker: mask EMPTY -> nothing cleared";
    } else {
        applyImageEngine(job->toolName, job->params, mat);
    }

    if (job->cancelled || mat.empty()) return;

    QMetaObject::invokeMethod(this, [this, jobId = job->jobId]() {
        emit jobStatusChanged(jobId, tr("Finalizing..."));
        emit jobProgressChanged(jobId, 85);
    }, Qt::QueuedConnection);

    job->resultImage = ImageEngine::toQImage(mat);

    if (job->cancelled) return;

    // Deliver to main thread
    QMetaObject::invokeMethod(this, [this, job]() {
        {
            QMutexLocker lock(&m_mutex);
            auto end = std::remove_if(m_jobs.begin(), m_jobs.end(),
                [&](const auto& j) { return j->jobId == job->jobId; });
            m_jobs.erase(end, m_jobs.end());
        }
        if (!job->cancelled)
            emit jobCompleted(job->jobId);
    }, Qt::QueuedConnection);
}

void AsyncJobSystem::executeProgressiveWorker(std::shared_ptr<AsyncJob> job)
{
    if (job->toolName == "delete_selected")
        qWarning() << "[DeleteSel] BUG: delete_selected reached PROGRESSIVE worker"
                   << "(mask ignored, will clear whole tiles) tiles="
                   << job->tileRects.size();
    if (job->cancelled || job->sourceImage.isNull() || job->tileRects.empty())
        return;

    // Sort tiles by distance to viewport center (closest first)
    int cx = job->viewportCenterX;
    int cy = job->viewportCenterY;
    std::sort(job->tileRects.begin(), job->tileRects.end(),
        [cx, cy](const QRect& a, const QRect& b) {
            int da = (a.center().x() - cx) * (a.center().x() - cx)
                   + (a.center().y() - cy) * (a.center().y() - cy);
            int db = (b.center().x() - cx) * (b.center().x() - cx)
                   + (b.center().y() - cy) * (b.center().y() - cy);
            return da < db;
        });

    int radius = job->kernelRadius;
    int iw = job->sourceImage.width();
    int ih = job->sourceImage.height();
    size_t total = job->tileRects.size();
    auto& pool = memory::threadLocalPool();

    for (size_t i = 0; i < total; i += job->batchSize) {
        if (job->cancelled) break;

        size_t end = std::min(i + job->batchSize, total);
        QVector<QRect> batchRects; // using QVector for signal compatibility

        for (size_t j = i; j < end; ++j) {
            const QRect& b = job->tileRects[j];

            QRect srcRect = b.adjusted(-radius, -radius, radius, radius);
            srcRect = srcRect.intersected(QRect(0, 0, iw, ih));
            if (srcRect.isEmpty()) continue;

            // Extract tile region using BufferPool
            QImage srcRegion = pool.acquireImage(srcRect.width(), srcRect.height(),
                                                  QImage::Format_RGBA8888);
            for (int y = 0; y < srcRect.height(); ++y) {
                std::memcpy(srcRegion.scanLine(y),
                           job->sourceImage.constScanLine(srcRect.y() + y) + srcRect.x() * 4,
                           static_cast<size_t>(srcRect.width()) * 4);
            }

            cv::Mat mat = ImageEngine::toCvMatFast(srcRegion);
            if (mat.empty()) { pool.release(srcRegion); continue; }

            applyImageEngine(job->toolName, job->params, mat);

            QImage result = ImageEngine::toQImageFast(mat);
            pool.release(srcRegion);
            if (result.isNull()) continue;

            // Copy cropped result back to sourceImage
            int ox = b.x() - srcRect.x();
            int oy = b.y() - srcRect.y();
            int ow = std::min(b.width(), result.width() - ox);
            int oh = std::min(b.height(), result.height() - oy);

            if (ow > 0 && oh > 0) {
                for (int y = 0; y < oh; ++y) {
                    std::memcpy(job->sourceImage.scanLine(b.y() + y) + b.x() * 4,
                               result.constScanLine(oy + y) + ox * 4,
                               static_cast<size_t>(ow) * 4);
                }
                batchRects.append(b);
            }
        }

        // Notify main thread for GPU upload (worker blocks during processing)
        if (!batchRects.isEmpty() && !job->cancelled) {
            QMetaObject::invokeMethod(this, [this, jobId = job->jobId, batchRects]() {
                emit progressiveBatch(jobId, batchRects);
            }, Qt::BlockingQueuedConnection);
            const int progress = static_cast<int>(
                std::min<size_t>(100, ((end * 100) / std::max<size_t>(1, total))));
            QMetaObject::invokeMethod(this, [this, jobId = job->jobId, progress]() {
                emit jobProgressChanged(jobId, progress);
            }, Qt::QueuedConnection);
        }
    }

    // Set final result and deliver to main thread
    if (!job->cancelled) {
        job->resultImage = job->sourceImage; // shallow copy via implicit sharing
        QMetaObject::invokeMethod(this, [this, job]() {
            {
                QMutexLocker lock(&m_mutex);
                auto end = std::remove_if(m_jobs.begin(), m_jobs.end(),
                    [&](const auto& j) { return j->jobId == job->jobId; });
                m_jobs.erase(end, m_jobs.end());
            }
            if (!job->cancelled)
                emit jobCompleted(job->jobId);
        }, Qt::QueuedConnection);
    }
}

void AsyncJobSystem::applyImageEngine(const std::string& toolName,
                                       const QVariantMap& params,
                                       cv::Mat& mat)
{
    auto d = [&](const QString& key, double def = 0.0) -> double {
        return params.value(key, def).toDouble();
    };
    auto i = [&](const QString& key, int def = 0) -> int {
        return static_cast<int>(d(key, def));
    };

    if (toolName == "gaussian_blur") {
        mat = ImageEngine::gaussianBlur(mat, static_cast<float>(d("radius", 3.0)));
    } else if (toolName == "adjust_color") {
        const float brightness = static_cast<float>(d("brightness", 0.0));
        const float contrast = static_cast<float>(d("contrast", 0.0));
        const float saturation = static_cast<float>(d("saturation", 0.0));
        const float hue = static_cast<float>(d("hue", 0.0));
        const bool autoContrast = d("auto_contrast", 0.0) != 0.0;
        if (autoContrast) mat = ImageEngine::autoContrast(mat);
        if (brightness != 0.0f) mat = ImageEngine::adjustBrightness(mat, brightness);
        if (contrast != 0.0f) mat = ImageEngine::adjustContrast(mat, contrast);
        if (saturation != 0.0f) mat = ImageEngine::adjustSaturation(mat, saturation);
        if (hue != 0.0f) mat = ImageEngine::adjustHue(mat, hue);
    } else if (toolName == "adjust_brightness") {
        mat = ImageEngine::adjustBrightness(mat, static_cast<float>(d("value", 0.0)));
    } else if (toolName == "adjust_contrast") {
        mat = ImageEngine::adjustContrast(mat, static_cast<float>(d("value", 0.0)));
    } else if (toolName == "adjust_saturation") {
        mat = ImageEngine::adjustSaturation(mat, static_cast<float>(d("value", 0.0)));
    } else if (toolName == "adjust_hue") {
        mat = ImageEngine::adjustHue(mat, static_cast<float>(d("value", 0.0)));
    } else if (toolName == "auto_contrast") {
        mat = ImageEngine::autoContrast(mat);
    } else if (toolName == "sharpen") {
        mat = ImageEngine::sharpen(mat, static_cast<float>(d("strength", 1.0)));
    } else if (toolName == "median_blur") {
        mat = ImageEngine::medianBlur(mat, i("kernel_size", 5));
    } else if (toolName == "box_blur") {
        mat = ImageEngine::boxBlur(mat, i("radius", 3));
    } else if (toolName == "bilateral_blur") {
        mat = ImageEngine::bilateralBlur(mat, i("diameter", 9),
            d("sigma_color", 75.0), d("sigma_space", 75.0));
    } else if (toolName == "motion_blur") {
        mat = ImageEngine::motionBlur(mat, i("length", 15), d("angle", 0.0));
    } else if (toolName == "radial_blur") {
        mat = ImageEngine::radialBlur(mat, d("amount", 0.25),
            d("center_x", 0.5), d("center_y", 0.5), i("samples", 12));
    } else if (toolName == "zoom_blur") {
        mat = ImageEngine::zoomBlur(mat, d("amount", 0.25),
            d("center_x", 0.5), d("center_y", 0.5), i("samples", 12));
    } else if (toolName == "edge_detect") {
        mat = ImageEngine::edgeDetect(mat,
            static_cast<float>(d("threshold1", 50.0)),
            static_cast<float>(d("threshold2", 150.0)));
    } else if (toolName == "grayscale") {
        mat = ImageEngine::grayscale(mat);
    } else if (toolName == "invert_colors") {
        mat = ImageEngine::invertColors(mat);
    } else if (toolName == "noise_reduce") {
        mat = ImageEngine::noiseReduce(mat, static_cast<float>(d("strength", 2)));
    } else if (toolName == "posterize") {
        mat = ImageEngine::posterize(mat, i("levels", 8));
    } else if (toolName == "threshold") {
        mat = ImageEngine::threshold(mat, d("value", 128.0));
    } else if (toolName == "remove_background") {
        mat = ImageEngine::removeBackground(mat);
    } else if (toolName == "fill_bucket") {
        const int x = i("x", mat.cols / 2);
        const int y = i("y", mat.rows / 2);
        const float tolerance = static_cast<float>(std::clamp(d("tolerance", 0.0), 0.0, 1.0));
        const QColor color(i("red", 255), i("green", 255), i("blue", 255), i("alpha", 255));
        mat = ImageEngine::fillRegion(mat, x, y, ImageEngine::qColorToScalar(color), tolerance);
    } else if (toolName == "resize_layer") {
        int w = i("width", mat.cols);
        int h = i("height", mat.rows);
        mat = ImageEngine::resize(mat, w, h);
    }
}
