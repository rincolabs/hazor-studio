#include "Document.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

void Document::saveSelectionToChannel(const QString& name)
{
    if (selection.image().isNull()) return;

    AlphaChannel ch;
    ch.name = name;
    ch.mask = selection.image().copy();
    ch.visible = true;
    channels.push_back(std::move(ch));
}

void Document::loadChannelToSelection(int index, SelectMode mode)
{
    if (index < 0 || index >= static_cast<int>(channels.size())) return;

    QImage chMask = channels[index].mask;
    if (chMask.isNull()) return;

    // A channel saved before a document resize would otherwise load as a
    // misaligned top-left block — rescale it to the current canvas.
    if (chMask.size() != selection.image().size())
        chMask = chMask.scaled(selection.width(), selection.height(),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (chMask.format() != QImage::Format_Grayscale8)
        chMask = chMask.convertToFormat(QImage::Format_Grayscale8);

    int w = std::min(chMask.width(), selection.width());
    int h = std::min(chMask.height(), selection.height());

    QImage temp(selection.width(), selection.height(), QImage::Format_Grayscale8);
    temp.fill(0);

    for (int y = 0; y < h; ++y) {
        const uchar* src = chMask.constScanLine(y);
        uchar* dst = temp.scanLine(y);
        std::copy(src, src + w, dst);
    }

    if (mode == SelectMode::Replace) {
        selection.image() = temp;
    } else {
        // Combine with existing mask (same soft-edge-preserving semantics as
        // SelectionMask::combineMask).
        auto& dst = selection.image();
        for (int y = 0; y < selection.height(); ++y) {
            uchar* dRow = dst.scanLine(y);
            const uchar* sRow = temp.constScanLine(y);
            for (int x = 0; x < selection.width(); ++x) {
                switch (mode) {
                case SelectMode::Add:
                    dRow[x] = std::max(dRow[x], sRow[x]);
                    break;
                case SelectMode::Subtract:
                    dRow[x] = static_cast<uchar>(
                        std::max(0, static_cast<int>(dRow[x]) - static_cast<int>(sRow[x])));
                    break;
                case SelectMode::Intersect:
                    dRow[x] = std::min(dRow[x], sRow[x]);
                    break;
                default:
                    break;
                }
            }
        }
    }

    selection.setActive(true);
}

void Document::deleteChannel(int index)
{
    if (index < 0 || index >= static_cast<int>(channels.size())) return;
    channels.erase(channels.begin() + index);
}

cv::Mat Document::selectionMaskForLayer(int lw, int lh,
                                         const QTransform& layerTransform) const
{
    if (lw <= 0 || lh <= 0) return {};
    double dw = static_cast<double>(size.width());
    double dh = static_cast<double>(size.height());

    double a00 = dw * layerTransform.m11() / lw;
    double a01 = -dw * layerTransform.m21() / lh;
    double a02 = dw * 0.5 * (1.0 - layerTransform.m11() + layerTransform.m21() + layerTransform.m31());
    double a10 = -dh * layerTransform.m12() / lw;
    double a11 = dh * layerTransform.m22() / lh;
    double a12 = dh * 0.5 * (1.0 + layerTransform.m12() - layerTransform.m22() - layerTransform.m32());

    cv::Mat fullXf = (cv::Mat_<double>(2,3) << a00, a01, a02, a10, a11, a12);

    cv::Mat docMask(dh, dw, CV_8UC1,
                    const_cast<uchar*>(selection.constBits()),
                    static_cast<size_t>(selection.image().bytesPerLine()));

    cv::Mat layerMask;
    // INTER_LINEAR: the mask carries coverage values (feather/anti-alias);
    // nearest-neighbour warping turns soft edges into staircases.
    cv::warpAffine(docMask, layerMask, fullXf, cv::Size(lw, lh),
                   cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                   cv::BORDER_CONSTANT, cv::Scalar(0));
    return layerMask;
}
