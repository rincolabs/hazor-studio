#include "ai/inpaint/AiInpaintPreprocessor.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>
#include <QObject>

#include <algorithm>

namespace {

int maskNonZero(const QImage& mask)
{
    if (mask.isNull())
        return 0;
    QImage gray = mask.format() == QImage::Format_Grayscale8
        ? mask
        : mask.convertToFormat(QImage::Format_Grayscale8);
    int count = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            if (row[x] > 0)
                ++count;
    }
    return count;
}

int roundUpToMultiple(int value, int multiple)
{
    const int m = std::max(1, multiple);
    return ((std::max(1, value) + m - 1) / m) * m;
}

} // namespace

QImage AiInpaintPreprocessor::ensureGrayscale8(const QImage& image)
{
    return image.format() == QImage::Format_Grayscale8
        ? image.copy()
        : image.convertToFormat(QImage::Format_Grayscale8);
}

QImage AiInpaintPreprocessor::thresholdMask(const QImage& mask)
{
    QImage out = ensureGrayscale8(mask);
    for (int y = 0; y < out.height(); ++y) {
        uchar* row = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x)
            row[x] = row[x] > 0 ? 255 : 0;
    }
    return out;
}

QImage AiInpaintPreprocessor::dilateMask(const QImage& mask, int pixels)
{
    if (pixels <= 0 || mask.isNull())
        return mask.copy();
    QImage out = ensureGrayscale8(mask);
    cv::Mat mat(out.height(), out.width(), CV_8UC1, out.bits(),
                static_cast<size_t>(out.bytesPerLine()));
    const int radius = std::max(1, pixels);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                               cv::Size(radius * 2 + 1, radius * 2 + 1));
    cv::dilate(mat, mat, kernel);
    return out;
}

QImage AiInpaintPreprocessor::featherMask(const QImage& mask, int pixels)
{
    if (pixels <= 0 || mask.isNull())
        return mask.copy();
    QImage out = ensureGrayscale8(mask);
    cv::Mat mat(out.height(), out.width(), CV_8UC1, out.bits(),
                static_cast<size_t>(out.bytesPerLine()));
    const int k = std::max(3, pixels * 2 + 1) | 1;
    cv::GaussianBlur(mat, mat, cv::Size(k, k), std::max(0.5, pixels / 2.0));
    return out;
}

QRect AiInpaintPreprocessor::nonZeroBounds(const QImage& mask)
{
    if (mask.isNull())
        return {};

    int minX = mask.width();
    int minY = mask.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (row[x] == 0)
                continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY)
        return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QRect AiInpaintPreprocessor::alignRoi(QRect roi, const QRect& documentBounds, int multiple)
{
    const int m = std::max(1, multiple);
    int left = (roi.left() / m) * m;
    int top = (roi.top() / m) * m;
    int right = ((roi.right() + m) / m) * m - 1;
    int bottom = ((roi.bottom() + m) / m) * m - 1;
    return QRect(QPoint(left, top), QPoint(right, bottom)).intersected(documentBounds);
}

QRect AiInpaintPreprocessor::squareRoi(QRect roi, const QRect& documentBounds, int multiple)
{
    if (roi.isEmpty() || documentBounds.isEmpty())
        return roi;

    const int m = std::max(1, multiple);
    int side = std::max(roi.width(), roi.height());
    side = ((side + m - 1) / m) * m;
    if (side > documentBounds.width() || side > documentBounds.height())
        return roi;

    auto clamp = [](int v, int lo, int hi) {
        return std::max(lo, std::min(v, hi));
    };

    const QPoint center = roi.center();
    const int maxLeft = documentBounds.right() - side + 1;
    const int maxTop = documentBounds.bottom() - side + 1;
    int left = center.x() - side / 2;
    int top = center.y() - side / 2;
    left = (left / m) * m;
    top = (top / m) * m;
    left = clamp(left, documentBounds.left(), maxLeft);
    top = clamp(top, documentBounds.top(), maxTop);
    return QRect(left, top, side, side);
}

AiPreparedInpaintInput AiInpaintPreprocessor::prepare(const QImage& documentImage,
                                                      const QImage& documentMask,
                                                      const AiModelDescriptor& model,
                                                      const AiRemoveOptions& options,
                                                      QString* error)
{
    AiPreparedInpaintInput out;
    if (documentImage.isNull() || documentMask.isNull()) {
        if (error) *error = QObject::tr("AI Remove needs an image and mask.");
        return out;
    }
    if (documentImage.size() != documentMask.size()) {
        if (error) *error = QObject::tr("AI Remove mask does not match the document size.");
        return out;
    }

    QImage mask = thresholdMask(documentMask);
    if (options.maskGrowPx > 0)
        mask = dilateMask(mask, options.maskGrowPx);

    const QRect bounds = nonZeroBounds(mask);
    if (bounds.isEmpty()) {
        if (error) *error = QObject::tr("Draw a lasso around the object first.");
        return out;
    }

    const QRect docBounds(QPoint(0, 0), documentImage.size());
    int multiple = std::max(1, model.input.value(QStringLiteral("requiredMultiple"), 8).toInt());
    const bool isStableDiffusion = model.family == QLatin1String("stable_diffusion")
        || model.type == QLatin1String("stable_diffusion");
    if (isStableDiffusion)
        multiple = std::max(multiple, 64);
    const int fixedSize = model.input.value(QStringLiteral("input_size"), 0).toInt();
    QRect padded = bounds.adjusted(-options.paddingPx, -options.paddingPx,
                                   options.paddingPx, options.paddingPx)
                       .intersected(docBounds);
    if (fixedSize > 0)
        padded = squareRoi(padded, docBounds, multiple);
    QRect roi = alignRoi(padded, docBounds, multiple);
    if (roi.isEmpty()) {
        if (error) *error = QObject::tr("AI Remove region is outside the document.");
        return out;
    }

    QImage blendMask = options.maskFeatherPx > 0
        ? featherMask(mask, options.maskFeatherPx)
        : mask;
    out.originalRoi = documentImage.convertToFormat(QImage::Format_RGBA8888).copy(roi);
    out.maskRoi = blendMask.copy(roi);
    out.documentRoi = roi;

    QSize modelSize = roi.size();
    if (fixedSize > 0)
        modelSize = QSize(fixedSize, fixedSize);
    else if (isStableDiffusion)
        modelSize = QSize(roundUpToMultiple(modelSize.width(), multiple),
                          roundUpToMultiple(modelSize.height(), multiple));

    qInfo().noquote() << "[AI][INPAINT] Preprocess"
                      << "model=" << model.id
                      << "document=" << documentImage.size()
                      << "maskBounds=" << bounds
                      << "roi=" << roi
                      << "modelSize=" << modelSize
                      << "requiredMultiple=" << multiple
                      << "padding=" << options.paddingPx
                      << "maskGrow=" << options.maskGrowPx
                      << "maskFeather=" << options.maskFeatherPx
                      << "maskNonZero=" << maskNonZero(mask);

    out.modelSize = modelSize;
    out.modelImage = out.originalRoi.scaled(modelSize, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGBA8888);
    out.modelMask = out.maskRoi.scaled(modelSize, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_Grayscale8);
    return out;
}

QImage AiInpaintPreprocessor::blendGeneratedPatch(const QImage& originalRoi,
                                                  const QImage& generatedRoi,
                                                  const QImage& maskRoi)
{
    if (originalRoi.isNull() || generatedRoi.isNull() || maskRoi.isNull())
        return {};
    QImage original = originalRoi.convertToFormat(QImage::Format_RGBA8888);
    QImage generated = generatedRoi.size() == original.size()
        ? generatedRoi.convertToFormat(QImage::Format_RGBA8888)
        : generatedRoi.scaled(original.size(), Qt::IgnoreAspectRatio,
                              Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
    QImage mask = maskRoi.size() == original.size()
        ? ensureGrayscale8(maskRoi)
        : maskRoi.scaled(original.size(), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);

    QImage out(original.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < out.height(); ++y) {
        const QRgb* o = reinterpret_cast<const QRgb*>(original.constScanLine(y));
        const QRgb* g = reinterpret_cast<const QRgb*>(generated.constScanLine(y));
        const uchar* m = mask.constScanLine(y);
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const int a = m[x];
            const int inv = 255 - a;
            const int r = (qRed(o[x]) * inv + qRed(g[x]) * a) / 255;
            const int gg = (qGreen(o[x]) * inv + qGreen(g[x]) * a) / 255;
            const int b = (qBlue(o[x]) * inv + qBlue(g[x]) * a) / 255;
            dst[x] = qRgba(r, gg, b, qAlpha(o[x]));
        }
    }
    return out;
}
