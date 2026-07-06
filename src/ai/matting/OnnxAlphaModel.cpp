#include "ai/matting/OnnxAlphaModel.hpp"

#include "ai/AiImageSnapshot.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiInferenceSession.hpp"
#include "ai/onnx/OnnxSessionHandle.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelTypes.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QElapsedTimer>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace {
constexpr char kModel[] = "model";

const OnnxTensorData* findOutputByNameOrFirst(const std::vector<OnnxTensorData>& outs,
                                              const QString& name)
{
    if (!name.isEmpty())
        for (const auto& o : outs)
            if (o.name == name)
                return &o;
    return outs.empty() ? nullptr : &outs.front();
}
} // namespace

bool OnnxAlphaModel::load(const QString& modelId, const QString& family, QString* error)
{
    auto* registry = AiModelRegistry::instance();
    const AiInstalledModel installed = registry->installedModel(modelId);
    if (!installed.isValid()) {
        if (error) *error = QStringLiteral("Model is not installed: %1").arg(modelId);
        return false;
    }

    m_config = MattingModelConfig::load(installed.directory, family);

    QString sessErr;
    auto session = AiRuntimeManager::instance()->createSession(installed, &sessErr);
    if (!session) {
        if (error) *error = sessErr;
        return false;
    }
    // Single-file model: the registry stores its one file as type "model"
    // (runMatte tolerates a differently-typed single file via fileTypes()).
    m_session = session;

    m_modelId = modelId;
    m_family = family;
    m_provider = session->providerUsed();
    qInfo().noquote() << "[AI][MATTE] Loaded" << modelId
                      << "family=" << family
                      << "inputSize=" << m_config.inputSize
                      << "keepAspect=" << m_config.keepAspectRatio
                      << "provider=" << m_provider;
    return true;
}

AiAlphaResult OnnxAlphaModel::runMatte(const AiImageSnapshot& snapshot,
                                       const std::atomic<bool>* cancel,
                                       const char* logTag,
                                       QString* error)
{
    AiAlphaResult result;
    if (!m_session) {
        if (error) *error = QStringLiteral("Matting session not loaded.");
        return result;
    }
    if (!snapshot.isValid()) {
        if (error) *error = QStringLiteral("Invalid image snapshot for matting.");
        return result;
    }

    const int N = std::max(1, m_config.inputSize);
    const QImage rgbImg = snapshot.image.convertToFormat(QImage::Format_RGB888);
    cv::Mat src(rgbImg.height(), rgbImg.width(), CV_8UC3,
                const_cast<uchar*>(rgbImg.bits()), static_cast<size_t>(rgbImg.bytesPerLine()));

    // ── Resize into the N×N network input, recording the content rect so the
    //    output can be cropped back out of any letterbox padding. ──
    int contentW = N, contentH = N;
    cv::Mat input(N, N, CV_8UC3, cv::Scalar(0, 0, 0));
    if (m_config.keepAspectRatio) {
        const double s = double(N) / std::max(src.cols, src.rows);
        contentW = std::max(1, int(std::lround(src.cols * s)));
        contentH = std::max(1, int(std::lround(src.rows * s)));
        cv::Mat resized;
        const bool shrink = (contentW < src.cols) || (contentH < src.rows);
        cv::resize(src, resized, cv::Size(contentW, contentH), 0, 0,
                   shrink ? cv::INTER_AREA : cv::INTER_LINEAR);
        resized.copyTo(input(cv::Rect(0, 0, contentW, contentH)));
    } else {
        const bool shrink = (N < src.cols) || (N < src.rows);
        cv::resize(src, input, cv::Size(N, N), 0, 0, shrink ? cv::INTER_AREA : cv::INTER_LINEAR);
    }

    if (cancel && cancel->load())
        return result;

    QElapsedTimer timer; timer.start();

    // ── Build the CHW float tensor with per-channel normalisation. ──
    OnnxTensorData in;
    in.name = m_config.inputName; // resolved below if empty
    in.shape = { 1, 3, N, N };
    in.data.resize(size_t(3) * N * N);
    const float mean[3] = { m_config.meanR, m_config.meanG, m_config.meanB };
    const float invStd[3] = {
        1.0f / (m_config.stdR != 0.0f ? m_config.stdR : 1.0f),
        1.0f / (m_config.stdG != 0.0f ? m_config.stdG : 1.0f),
        1.0f / (m_config.stdB != 0.0f ? m_config.stdB : 1.0f)
    };
    const size_t plane = size_t(N) * N;
    float* dstR = in.data.data();
    float* dstG = dstR + plane;
    float* dstB = dstG + plane;
    for (int y = 0; y < N; ++y) {
        const uchar* row = input.ptr<uchar>(y); // RGB888
        for (int x = 0; x < N; ++x) {
            const size_t idx = size_t(y) * N + x;
            const float r = row[x * 3 + 0] / 255.0f;
            const float g = row[x * 3 + 1] / 255.0f;
            const float b = row[x * 3 + 2] / 255.0f;
            dstR[idx] = (r - mean[0]) * invStd[0];
            dstG[idx] = (g - mean[1]) * invStd[1];
            dstB[idx] = (b - mean[2]) * invStd[2];
        }
    }

    auto handle = m_session->handle(QString::fromLatin1(kModel));
    if (!handle) {
        const QStringList types = m_session->fileTypes();
        if (!types.isEmpty())
            handle = m_session->handle(types.first());
    }
    if (!handle) {
        if (error) *error = QStringLiteral("Matting model has no loaded session.");
        return result;
    }
    if (in.name.isEmpty()) {
        const QStringList ins = handle->inputNames();
        in.name = ins.isEmpty() ? QStringLiteral("input") : ins.first();
    }

    std::vector<OnnxTensorData> inputs;
    inputs.push_back(std::move(in));
    std::vector<OnnxTensorData> outputs;
    QString runErr;
    if (!handle->run(inputs, QStringList(), outputs, &runErr)) {
        if (error) *error = QStringLiteral("Matting model failed: %1").arg(runErr);
        return result;
    }
    const qint64 inferMs = timer.elapsed();

    const OnnxTensorData* out = findOutputByNameOrFirst(outputs, m_config.outputName);
    if (!out || out->data.empty()) {
        if (error) *error = QStringLiteral("Matting model produced no output.");
        return result;
    }

    // Output is expected [.., H, W] with H==W==N. Infer the last two dims.
    int outH = N, outW = N;
    if (out->shape.size() >= 2) {
        outH = int(out->shape[out->shape.size() - 2]);
        outW = int(out->shape[out->shape.size() - 1]);
    }
    if (outH <= 0 || outW <= 0 || qint64(outW) * outH > qint64(out->data.size())) {
        if (error) *error = QStringLiteral("Matting output has unexpected size.");
        return result;
    }

    cv::Mat alpha(outH, outW, CV_32F, const_cast<float*>(out->data.data()));
    cv::Mat work = alpha.clone();

    if (m_config.applySigmoid) {
        // Some exports bake the final sigmoid into the graph, so the output is
        // already a probability map in [0,1]. Applying sigmoid again squashes the
        // whole frame toward 0.5-0.73 (background never reaches 0), which shows up
        // as the entire image becoming semi-transparent after removal. Only apply
        // sigmoid when the raw output actually looks like unbounded logits.
        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(work, &mn, &mx);
        const bool alreadyProbabilities = mn >= -1e-3 && mx <= 1.0 + 1e-3;
        if (alreadyProbabilities) {
            qInfo().noquote() << logTag << "output already in [0,1] (range" << mn << mx
                              << ")— skipping redundant sigmoid";
        } else {
            for (int y = 0; y < work.rows; ++y) {
                float* r = work.ptr<float>(y);
                for (int x = 0; x < work.cols; ++x)
                    r[x] = 1.0f / (1.0f + std::exp(-r[x]));
            }
        }
    }
    if (m_config.minMaxNormalize) {
        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(work, &mn, &mx);
        const double range = mx - mn;
        if (range > 1e-6)
            work = (work - mn) / range;
    }
    // Clamp to [0,1] and scale to 8-bit.
    cv::threshold(work, work, 1.0, 1.0, cv::THRESH_TRUNC);
    cv::threshold(work, work, 0.0, 0.0, cv::THRESH_TOZERO);
    cv::Mat alpha8;
    work.convertTo(alpha8, CV_8UC1, 255.0);

    // If the output grid differs from N, scale it to N first so the letterbox
    // crop math is consistent.
    if (alpha8.cols != N || alpha8.rows != N)
        cv::resize(alpha8, alpha8, cv::Size(N, N), 0, 0, cv::INTER_LINEAR);

    // Crop the content rect (drop letterbox padding) then map to document space.
    cv::Mat content = alpha8(cv::Rect(0, 0, std::min(contentW, N), std::min(contentH, N)));

    const QSize docSize = snapshot.documentSize;
    QImage outImg(docSize.width(), docSize.height(), QImage::Format_Grayscale8);
    cv::Mat docMat(docSize.height(), docSize.width(), CV_8UC1, outImg.bits(),
                   static_cast<size_t>(outImg.bytesPerLine()));
    cv::resize(content, docMat, cv::Size(docSize.width(), docSize.height()), 0, 0, cv::INTER_LINEAR);

    cv::Mat nz;
    cv::findNonZero(docMat > 16, nz);
    if (nz.empty()) {
        if (error) *error = QStringLiteral("Matting model returned an empty matte.");
        return result;
    }
    const cv::Rect bb = cv::boundingRect(nz);

    result.alpha = outImg;
    result.bounds = QRectF(bb.x, bb.y, bb.width, bb.height);
    result.ok = true;
    qInfo().noquote() << logTag << "matte"
                      << "snapshot=" << snapshot.image.size()
                      << "input=" << N
                      << "content=" << QSize(contentW, contentH)
                      << "doc=" << docSize
                      << "infer=" << inferMs << "ms"
                      << "provider=" << m_provider;
    return result;
}
