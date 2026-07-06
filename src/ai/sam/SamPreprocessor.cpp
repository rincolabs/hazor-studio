#include "ai/sam/SamPreprocessor.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>

SamPreprocessor::Result SamPreprocessor::buildEncoderInput(const QImage& snapshot,
                                                           int inputSize)
{
    Result r;
    if (snapshot.isNull() || inputSize <= 0)
        return r;

    const QSize snapSize = snapshot.size();
    SamCoordinateMapper mapper(snapSize, inputSize);
    const QSize resized = mapper.resizedSize();
    if (resized.width() <= 0 || resized.height() <= 0)
        return r;

    // Normalise to a tightly-packed RGB888 buffer (drop alpha; SAM ignores it).
    QImage rgb = snapshot.convertToFormat(QImage::Format_RGB888);
    cv::Mat src(rgb.height(), rgb.width(), CV_8UC3,
                rgb.bits(), static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat resizedMat;
    // INTER_AREA for downscaling preserves detail without aliasing; LINEAR up.
    const bool shrinking = resized.width() < snapSize.width()
                        || resized.height() < snapSize.height();
    cv::resize(src, resizedMat, cv::Size(resized.width(), resized.height()), 0, 0,
               shrinking ? cv::INTER_AREA : cv::INTER_LINEAR);

    OnnxTensorData& t = r.inputTensor;
    t.name = QStringLiteral("input_image");
    t.shape = { resized.height(), resized.width(), 3 };
    t.data.resize(static_cast<size_t>(resized.width()) * resized.height() * 3);

    // HWC float, RGB order, 0..255 (the ONNX graph normalises internally).
    float* dst = t.data.data();
    for (int y = 0; y < resized.height(); ++y) {
        const uchar* row = resizedMat.ptr<uchar>(y);
        const int n = resized.width() * 3;
        for (int i = 0; i < n; ++i)
            dst[i] = float(row[i]);
        dst += n;
    }

    r.mapper = mapper;
    r.snapshotSize = snapSize;
    r.ok = true;
    return r;
}
