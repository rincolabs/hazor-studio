#pragma once

#include "ai/AiRemoveTypes.hpp"
#include "ai/inpaint/StableDiffusionLatentUtils.hpp"
#include "ai/inpaint/StableDiffusionScheduler.hpp"
#include "ai/inpaint/StableDiffusionTokenizer.hpp"
#include "ai/models/AiModelDescriptor.hpp"
#include "ai/onnx/OnnxSessionHandle.hpp"

#include <QImage>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

class AiSessionBundle;

struct StableDiffusionTextToImageRequest {
    QString prompt;
    QString negativePrompt;
    QSize size = QSize(512, 512);
    int steps = 20;
    float guidanceScale = 7.5f;
    quint64 seed = 0;
    std::atomic_bool* cancel = nullptr;
};

struct StableDiffusionImageToImageRequest : StableDiffusionTextToImageRequest {
    QImage image;
    float strength = 0.85f;
};

struct StableDiffusionInpaintRequest : StableDiffusionImageToImageRequest {
    QImage mask;
};

class StableDiffusionOnnxPipeline {
public:
    StableDiffusionOnnxPipeline(std::shared_ptr<AiSessionBundle> bundle,
                                AiModelDescriptor descriptor);

    QImage textToImage(const StableDiffusionTextToImageRequest& request,
                       QString* error = nullptr);
    QImage imageToImage(const StableDiffusionImageToImageRequest& request,
                        QString* error = nullptr);
    QImage inpaint(const StableDiffusionInpaintRequest& request,
                   QString* error = nullptr);

private:
    StableDiffusionTensor encodePrompt(const QString& prompt, QString* error);
    StableDiffusionTensor vaeEncode(const StableDiffusionTensor& image, QString* error);
    StableDiffusionTensor vaeDecode(const StableDiffusionTensor& latents, QString* error);
    StableDiffusionTensor runUnet(const StableDiffusionTensor& latents,
                                  const StableDiffusionTensor& maskLatents,
                                  const StableDiffusionTensor& maskedImageLatents,
                                  int timestep,
                                  const StableDiffusionTensor& promptEmbeds,
                                  QString* error);
    StableDiffusionTensor guidedNoise(const StableDiffusionTensor& latents,
                                      const StableDiffusionTensor& maskLatents,
                                      const StableDiffusionTensor& maskedImageLatents,
                                      int timestep,
                                      const StableDiffusionTensor& promptEmbeds,
                                      const StableDiffusionTensor& negativeEmbeds,
                                      float guidanceScale,
                                      QString* error);
    StableDiffusionTensor firstOutputAsTensor(const std::vector<OnnxTensorData>& outputs,
                                              QString* error) const;
    OnnxTensorData tensorInput(const QString& name, const StableDiffusionTensor& tensor) const;
    float latentScalingFactor() const;
    bool ensureTokenizer(QString* error);
    bool ensureScheduler(QString* error);
    QString filePath(const QString& componentName) const;

    std::shared_ptr<AiSessionBundle> m_bundle;
    AiModelDescriptor m_descriptor;
    StableDiffusionTokenizer m_tokenizer;
    StableDiffusionScheduler m_scheduler;
    bool m_tokenizerLoaded = false;
    bool m_schedulerLoaded = false;
};
