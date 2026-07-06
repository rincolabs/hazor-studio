#pragma once
#include <QSize>
#include <QRectF>

class Document;

enum class RenderTargetType { Canvas, Export, Thumbnail, Flatten };

struct RenderContext {
    Document*        document   = nullptr;
    QSize            outputSize;
    QRectF           documentRect;
    RenderTargetType targetType = RenderTargetType::Canvas;
    bool             highQuality = false;
};
