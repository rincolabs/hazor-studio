#pragma once

#include "ai/AiRemoveTypes.hpp"
#include "ai/models/AiModelDescriptor.hpp"

#include <QImage>
#include <QMetaType>
#include <QRect>

#include <atomic>

struct AiPreparedInpaintInput {
    QImage modelImage;    // RGBA8888, model size
    QImage modelMask;     // Grayscale8, model size
    QImage originalRoi;   // RGBA8888, document ROI size
    QImage maskRoi;       // Grayscale8, document ROI size
    QRect documentRoi;
    QSize modelSize;

    bool isValid() const
    {
        return !modelImage.isNull() && !modelMask.isNull()
            && !originalRoi.isNull() && !maskRoi.isNull()
            && !documentRoi.isEmpty();
    }
};

struct AiInpaintRequest {
    QImage sourceImage; // RGBA8888, document color space
    QImage mask;        // Grayscale8, sourceImage size
    AiRemoveOptions options;
    AiModelDescriptor model;
    quint64 sourceRevision = 0;
    quint64 documentId = 0;
    quint64 jobId = 0;
    std::atomic_bool* cancel = nullptr;
};

struct AiInpaintResult {
    QImage patch;     // RGBA8888, document ROI size
    QImage blendMask; // Grayscale8, document ROI size
    QRect documentRoi;
    QString modelId;
    quint64 jobId = 0;
    quint64 sourceRevision = 0;
    quint64 documentId = 0;
    QString providerUsed;
    QString error;
    bool cancelled = false;

    bool isValid() const
    {
        return error.isEmpty() && !cancelled && !patch.isNull()
            && !blendMask.isNull() && !documentRoi.isEmpty();
    }
};

Q_DECLARE_METATYPE(AiInpaintResult)
