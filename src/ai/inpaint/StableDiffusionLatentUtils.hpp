#pragma once

#include <QImage>
#include <QSize>

#include <cstdint>
#include <vector>

struct StableDiffusionTensor {
    std::vector<int64_t> shape;
    std::vector<float> data;

    int64_t elementCount() const;
    bool isValid() const { return elementCount() > 0 && data.size() == static_cast<size_t>(elementCount()); }
};

namespace StableDiffusionLatentUtils {

StableDiffusionTensor randomNormal(const std::vector<int64_t>& shape, quint64 seed);
StableDiffusionTensor zeros(const std::vector<int64_t>& shape);
StableDiffusionTensor add(const StableDiffusionTensor& a, const StableDiffusionTensor& b);
StableDiffusionTensor subtract(const StableDiffusionTensor& a, const StableDiffusionTensor& b);
StableDiffusionTensor multiply(const StableDiffusionTensor& a, float scalar);
StableDiffusionTensor addScaled(const StableDiffusionTensor& base,
                                const StableDiffusionTensor& delta,
                                float scale);
StableDiffusionTensor concatChannels(const StableDiffusionTensor& a,
                                     const StableDiffusionTensor& b,
                                     const StableDiffusionTensor& c);
StableDiffusionTensor maskedImage(const StableDiffusionTensor& image,
                                  const StableDiffusionTensor& mask);
StableDiffusionTensor resizeMaskToLatents(const StableDiffusionTensor& mask,
                                          const QSize& latentSize);
StableDiffusionTensor imageToNchw(const QImage& image, const QSize& size);
StableDiffusionTensor maskToNchw(const QImage& mask, const QSize& size);
QImage nchwToImage(const StableDiffusionTensor& tensor, bool signedUnitRange = true);

} // namespace StableDiffusionLatentUtils
