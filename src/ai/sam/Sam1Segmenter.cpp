#include "ai/sam/Sam1Segmenter.hpp"

#include "ai/sam/SamPreprocessor.hpp"
#include "ai/sam/SamCoordinateMapper.hpp"
#include "ai/sam/SamModelConfig.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiInferenceSession.hpp"
#include "ai/onnx/OnnxSessionHandle.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelTypes.hpp"

#include <QElapsedTimer>
#include <QDebug>
#include <QDir>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace {
constexpr char kEncoder[] = "encoder";
constexpr char kDecoder[] = "decoder";

const OnnxTensorData* findOutput(const std::vector<OnnxTensorData>& outs,
                                 const QString& name)
{
    for (const auto& o : outs)
        if (o.name == name)
            return &o;
    return nullptr;
}
} // namespace

std::shared_ptr<Sam1Segmenter> Sam1Segmenter::load(const QString& modelId, QString* error)
{
    auto* registry = AiModelRegistry::instance();
    const AiInstalledModel installed = registry->installedModel(modelId);
    if (!installed.isValid()) {
        if (error) *error = QStringLiteral("SAM model is not installed: %1").arg(modelId);
        return nullptr;
    }

    int inputSize = 1024;
    const QString cfgPath = SamModelConfig::findConfigInDir(installed.directory);
    if (!cfgPath.isEmpty()) {
        const SamModelConfig cfg = SamModelConfig::parseFile(cfgPath);
        if (cfg.inputSize > 0)
            inputSize = cfg.inputSize;
    }

    // createSession resolves the installed files (encoder/decoder) directly.
    QString sessErr;
    auto session = AiRuntimeManager::instance()->createSession(installed, &sessErr);
    if (!session) {
        if (error) *error = sessErr;
        return nullptr;
    }
    if (!session->hasHandle(QString::fromLatin1(kEncoder))
        || !session->hasHandle(QString::fromLatin1(kDecoder))) {
        if (error)
            *error = QStringLiteral("SAM model '%1' is missing an encoder or decoder file.").arg(modelId);
        return nullptr;
    }

    auto seg = std::shared_ptr<Sam1Segmenter>(new Sam1Segmenter());
    seg->m_modelId = modelId;
    seg->m_inputSize = inputSize;
    seg->m_session = session;
    seg->m_provider = session->providerUsed();

    // Determined per loaded model (runtime), from the decoder's actual outputs:
    // SAM-family decoders expose low_res_masks → canonical path (we post-process
    // ourselves); a decoder that only has `masks` uses the baked output.
    const bool hasLowRes = session->handle(QString::fromLatin1(kDecoder))
                               ->outputNames().contains(QStringLiteral("low_res_masks"));
    qInfo().noquote() << "[AI][SAM] Loaded" << modelId
                      << "inputSize=" << inputSize
                      << "provider=" << seg->m_provider
                      << "maskPath=" << (hasLowRes ? "low_res (canonical)" : "masks (fallback)");
    return seg;
}

std::shared_ptr<SamEmbedding> Sam1Segmenter::buildEmbedding(const QImage& snapshot, QString* error)
{
    if (!m_session) {
        if (error) *error = QStringLiteral("SAM session not loaded.");
        return nullptr;
    }

    QElapsedTimer timer;
    timer.start();
    SamPreprocessor::Result pre = SamPreprocessor::buildEncoderInput(snapshot, m_inputSize);
    if (!pre.ok) {
        if (error) *error = QStringLiteral("Failed to preprocess image for SAM encoder.");
        return nullptr;
    }
    const qint64 preMs = timer.elapsed();

    auto encoder = m_session->handle(QString::fromLatin1(kEncoder));
    std::vector<OnnxTensorData> inputs;
    inputs.push_back(std::move(pre.inputTensor));
    std::vector<OnnxTensorData> outputs;
    QString runErr;
    timer.restart();
    if (!encoder->run(inputs, { QStringLiteral("image_embeddings") }, outputs, &runErr)) {
        if (error) *error = QStringLiteral("SAM encoder failed: %1").arg(runErr);
        return nullptr;
    }
    const qint64 encMs = timer.elapsed();

    const OnnxTensorData* embOut = findOutput(outputs, QStringLiteral("image_embeddings"));
    if (!embOut || embOut->data.empty()) {
        if (error) *error = QStringLiteral("SAM encoder produced no embedding.");
        return nullptr;
    }

    auto emb = std::make_shared<SamEmbedding>();
    emb->data = embOut->data;
    emb->snapshotSize = pre.snapshotSize;
    emb->resizedSize = pre.mapper.resizedSize();
    emb->inputSize = m_inputSize;

    qInfo().noquote() << "[AI][SAM] Embedding built"
                      << "snapshot=" << pre.snapshotSize
                      << "resized=" << emb->resizedSize
                      << "preprocess=" << preMs << "ms"
                      << "encode=" << encMs << "ms"
                      << "provider=" << m_provider;
    return emb;
}

SamMaskCandidate Sam1Segmenter::decode(const SamEmbedding& embedding,
                                       const QVector<AiPromptPoint>& pointsSnapshot,
                                       const QRectF* boxSnapshot,
                                       QString* error)
{
    SamMaskCandidate candidate;
    if (!m_session) {
        if (error) *error = QStringLiteral("SAM session not loaded.");
        return candidate;
    }
    if (!embedding.isValid()) {
        if (error) *error = QStringLiteral("SAM embedding is invalid.");
        return candidate;
    }

    const SamCoordinateMapper mapper(embedding.snapshotSize, embedding.inputSize);

    // Assemble point_coords / point_labels in model space following the official
    // SAM ONNX convention (labels: 1=fg, 0=bg, 2=box-TL, 3=box-BR, -1=padding).
    std::vector<float> coords;
    std::vector<float> labels;
    if (boxSnapshot) {
        const QRectF b = mapper.snapshotBoxToModel(boxSnapshot->normalized());
        coords.push_back(float(b.left()));  coords.push_back(float(b.top()));    labels.push_back(2.0f);
        coords.push_back(float(b.right())); coords.push_back(float(b.bottom())); labels.push_back(3.0f);
        for (const AiPromptPoint& p : pointsSnapshot) {
            const QPointF m = mapper.snapshotToModel(p.pos);
            coords.push_back(float(m.x())); coords.push_back(float(m.y()));
            labels.push_back(p.foreground ? 1.0f : 0.0f);
        }
    } else {
        for (const AiPromptPoint& p : pointsSnapshot) {
            const QPointF m = mapper.snapshotToModel(p.pos);
            coords.push_back(float(m.x())); coords.push_back(float(m.y()));
            labels.push_back(p.foreground ? 1.0f : 0.0f);
        }
        // Padding point required when there is no box prompt.
        coords.push_back(0.0f); coords.push_back(0.0f);
        labels.push_back(-1.0f);
    }
    const int64_t numPoints = static_cast<int64_t>(labels.size());
    if (numPoints == 0) {
        if (error) *error = QStringLiteral("SAM decode called with an empty prompt.");
        return candidate;
    }

    std::vector<OnnxTensorData> inputs;
    {
        OnnxTensorData t; t.name = QStringLiteral("image_embeddings");
        t.shape = { 1, 256, 64, 64 }; t.data = embedding.data;
        inputs.push_back(std::move(t));
    }
    {
        OnnxTensorData t; t.name = QStringLiteral("point_coords");
        t.shape = { 1, numPoints, 2 }; t.data = std::move(coords);
        inputs.push_back(std::move(t));
    }
    {
        OnnxTensorData t; t.name = QStringLiteral("point_labels");
        t.shape = { 1, numPoints }; t.data = std::move(labels);
        inputs.push_back(std::move(t));
    }
    {
        OnnxTensorData t; t.name = QStringLiteral("mask_input");
        t.shape = { 1, 1, 256, 256 }; t.data.assign(256 * 256, 0.0f);
        inputs.push_back(std::move(t));
    }
    {
        OnnxTensorData t; t.name = QStringLiteral("has_mask_input");
        t.shape = { 1 }; t.data = { 0.0f };
        inputs.push_back(std::move(t));
    }
    {
        OnnxTensorData t; t.name = QStringLiteral("orig_im_size");
        t.shape = { 2 };
        t.data = { float(embedding.snapshotSize.height()),
                   float(embedding.snapshotSize.width()) };
        inputs.push_back(std::move(t));
    }

    auto decoder = m_session->handle(QString::fromLatin1(kDecoder));

    // Prefer the canonical path: post-process the raw low-res logits ourselves
    // (this is exactly what the reference SamPredictor.postprocess_masks does).
    // It's robust across exports and aspect ratios, and avoids the convenience
    // `masks` upscale baked into the graph — which is mislocated/compressed in
    // some (non-official) exports. We only fall back to `masks` for a model that
    // doesn't expose `low_res_masks` (not the case for SAM-family decoders).
    const bool hasLowRes = decoder->outputNames().contains(QStringLiteral("low_res_masks"));

    std::vector<OnnxTensorData> outputs;
    QString runErr;
    QElapsedTimer timer; timer.start();
    const QStringList requested = hasLowRes
        ? QStringList{ QStringLiteral("low_res_masks"), QStringLiteral("iou_predictions") }
        : QStringList{ QStringLiteral("masks"), QStringLiteral("iou_predictions") };
    if (!decoder->run(inputs, requested, outputs, &runErr)) {
        if (error) *error = QStringLiteral("SAM decoder failed: %1").arg(runErr);
        return candidate;
    }
    const qint64 decMs = timer.elapsed();

    if (hasLowRes) {
        const OnnxTensorData* lr = findOutput(outputs, QStringLiteral("low_res_masks"));
        if (!lr || lr->data.empty()) {
            if (error) *error = QStringLiteral("SAM decoder produced no mask.");
            return candidate;
        }
        int lrH = 256, lrW = 256;
        if (lr->shape.size() >= 2) {
            lrH = int(lr->shape[lr->shape.size() - 2]);
            lrW = int(lr->shape[lr->shape.size() - 1]);
        }
        if (qint64(lrW) * lrH != qint64(lr->data.size())) {
            if (error) *error = QStringLiteral("SAM low-res mask has unexpected size.");
            return candidate;
        }

        const int inputSize = embedding.inputSize;
        const QSize resized = embedding.resizedSize;   // un-padded region inside the square
        const QSize snap = embedding.snapshotSize;

        cv::Mat low(lrH, lrW, CV_32F, const_cast<float*>(lr->data.data()));
        // 1) upscale low-res logits to the full padded square (input_size²).
        cv::Mat square;
        cv::resize(low, square, cv::Size(inputSize, inputSize), 0, 0, cv::INTER_LINEAR);
        // 2) crop the top-left un-padded region (SAM pads bottom/right).
        const int cropW = std::clamp(resized.width(),  1, inputSize);
        const int cropH = std::clamp(resized.height(), 1, inputSize);
        cv::Mat cropped = square(cv::Rect(0, 0, cropW, cropH));
        // 3) resize to snapshot resolution → mask logits in snapshot space.
        cv::Mat snapMask;
        cv::resize(cropped, snapMask, cv::Size(snap.width(), snap.height()), 0, 0, cv::INTER_LINEAR);

        candidate.logits.assign(snapMask.ptr<float>(0),
                                snapMask.ptr<float>(0) + snapMask.total());
        candidate.size = snap;
        candidate.lowResLogits = lr->data;
    } else {
        // Fallback: the decoder only exposes the pre-upscaled `masks` output.
        const OnnxTensorData* masks = findOutput(outputs, QStringLiteral("masks"));
        if (!masks || masks->data.empty()) {
            if (error) *error = QStringLiteral("SAM decoder produced no mask.");
            return candidate;
        }
        int maskH = embedding.snapshotSize.height();
        int maskW = embedding.snapshotSize.width();
        if (masks->shape.size() >= 2) {
            maskH = int(masks->shape[masks->shape.size() - 2]);
            maskW = int(masks->shape[masks->shape.size() - 1]);
        }
        if (qint64(maskW) * maskH != qint64(masks->data.size())) {
            if (qint64(embedding.snapshotSize.width()) * embedding.snapshotSize.height()
                == qint64(masks->data.size())) {
                maskW = embedding.snapshotSize.width();
                maskH = embedding.snapshotSize.height();
            } else {
                if (error) *error = QStringLiteral("SAM decoder mask has unexpected size.");
                return candidate;
            }
        }
        candidate.logits = masks->data;
        candidate.size = QSize(maskW, maskH);
    }

    if (const OnnxTensorData* iou = findOutput(outputs, QStringLiteral("iou_predictions")))
        candidate.score = iou->data.empty() ? 0.0f : iou->data[0];

    qInfo().noquote() << "[AI][SAM] Decoded mask"
                      << "points=" << numPoints
                      << "box=" << (boxSnapshot != nullptr)
                      << "path=" << (hasLowRes ? "low_res" : "masks")
                      << "decode=" << decMs << "ms"
                      << "score=" << candidate.score;
    return candidate;
}
