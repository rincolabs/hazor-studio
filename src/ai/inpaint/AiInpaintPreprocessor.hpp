#pragma once

#include "ai/AiRemoveTypes.hpp"
#include "ai/inpaint/AiInpaintTypes.hpp"
#include "ai/models/AiModelDescriptor.hpp"

class AiInpaintPreprocessor {
public:
    static AiPreparedInpaintInput prepare(const QImage& documentImage,
                                          const QImage& documentMask,
                                          const AiModelDescriptor& model,
                                          const AiRemoveOptions& options,
                                          QString* error = nullptr);

    static QImage blendGeneratedPatch(const QImage& originalRoi,
                                      const QImage& generatedRoi,
                                      const QImage& maskRoi);

private:
    static QImage ensureGrayscale8(const QImage& image);
    static QImage thresholdMask(const QImage& mask);
    static QImage dilateMask(const QImage& mask, int pixels);
    static QImage featherMask(const QImage& mask, int pixels);
    static QRect nonZeroBounds(const QImage& mask);
    static QRect alignRoi(QRect roi, const QRect& documentBounds, int multiple);
    static QRect squareRoi(QRect roi, const QRect& documentBounds, int multiple);
};
