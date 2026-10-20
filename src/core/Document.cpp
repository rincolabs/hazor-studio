#include "Document.hpp"

#include <algorithm>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

int Document::colorChannelCount() const
{
    // Only the modes the editor actually represents. Anything else falls back
    // to 3 rather than inventing a component count the panel cannot show.
    if (colorMode == QStringLiteral("Grayscale"))
        return 1;
    if (colorMode == QStringLiteral("CMYK Color"))
        return 4;
    return 3;
}

DocumentChannel* Document::channelById(uint64_t id)
{
    for (auto& ch : channels) {
        if (ch.id == id)
            return &ch;
    }
    return nullptr;
}

const DocumentChannel* Document::channelById(uint64_t id) const
{
    for (const auto& ch : channels) {
        if (ch.id == id)
            return &ch;
    }
    return nullptr;
}

int Document::channelIndexById(uint64_t id) const
{
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        if (channels[static_cast<size_t>(i)].id == id)
            return i;
    }
    return -1;
}

QString Document::uniqueChannelName(ChannelKind kind) const
{
    const QString prefix = kind == ChannelKind::Spot
        ? QStringLiteral("Spot Color") : QStringLiteral("Alpha");
    for (int n = 1; n < 10000; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(prefix).arg(n);
        bool taken = false;
        for (const auto& ch : channels) {
            if (ch.name.compare(candidate, Qt::CaseInsensitive) == 0) {
                taken = true;
                break;
            }
        }
        if (!taken)
            return candidate;
    }
    return prefix;
}

int Document::addChannel(DocumentChannel channel, int index)
{
    if (!canAddChannel())
        return -1;

    if (channel.id == 0)
        channel.id = allocateChannelId();
    else
        m_lastChannelId = std::max(m_lastChannelId, channel.id);

    if (channel.name.isEmpty())
        channel.name = uniqueChannelName(channel.kind);

    if (channel.data.isNull() || channel.data.size() != size) {
        QImage sized = DocumentChannel::makeNeutralBuffer(size, channel.kind);
        if (!channel.data.isNull()) {
            // Keep whatever the incoming buffer covered, anchored top-left,
            // instead of stretching it: a channel is document-space data and
            // resampling it here would silently distort an imported mask.
            QImage src = channel.data.format() == QImage::Format_Grayscale8
                ? channel.data
                : channel.data.convertToFormat(QImage::Format_Grayscale8);
            const int w = std::min(src.width(), sized.width());
            const int h = std::min(src.height(), sized.height());
            for (int y = 0; y < h; ++y)
                std::copy(src.constScanLine(y), src.constScanLine(y) + w,
                          sized.scanLine(y));
        }
        channel.data = sized;
    }
    channel.thumbDirty = true;

    const int count = static_cast<int>(channels.size());
    const int at = (index < 0 || index > count) ? count : index;
    channels.insert(channels.begin() + at, std::move(channel));
    invalidateChannelView();
    return at;
}

void Document::deleteChannel(int index)
{
    if (index < 0 || index >= static_cast<int>(channels.size())) return;
    const uint64_t id = channels[static_cast<size_t>(index)].id;
    channels.erase(channels.begin() + index);

    // Drop the edit arming with the channel, and fall back to the composite so
    // the tools never keep writing to a buffer that no longer exists.
    auto& armed = channelState.selectedChannels;
    armed.erase(std::remove(armed.begin(), armed.end(), id), armed.end());
    if (armed.empty() && !channelState.anyColorSelected())
        channelState.selectComposite();
    invalidateChannelView();
}

void Document::ensureChannelBuffers()
{
    for (auto& ch : channels) {
        if (ch.data.size() == size && ch.data.format() == QImage::Format_Grayscale8)
            continue;
        QImage sized = DocumentChannel::makeNeutralBuffer(size, ch.kind);
        if (!ch.data.isNull()) {
            QImage src = ch.data.format() == QImage::Format_Grayscale8
                ? ch.data
                : ch.data.convertToFormat(QImage::Format_Grayscale8);
            const int w = std::min(src.width(), sized.width());
            const int h = std::min(src.height(), sized.height());
            for (int y = 0; y < h; ++y)
                std::copy(src.constScanLine(y), src.constScanLine(y) + w,
                          sized.scanLine(y));
        }
        ch.data = sized;
        ch.thumbDirty = true;
    }
}

QImage Document::channelCoverageMask(int index) const
{
    if (index < 0 || index >= static_cast<int>(channels.size()))
        return {};
    const DocumentChannel& ch = channels[static_cast<size_t>(index)];
    if (ch.data.isNull())
        return {};

    QImage src = ch.data.format() == QImage::Format_Grayscale8
        ? ch.data
        : ch.data.convertToFormat(QImage::Format_Grayscale8);
    if (ch.isAlpha())
        return src;

    // Spot: invert once, here, so every consumer downstream reads presence.
    QImage out(src.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const uchar* in = src.constScanLine(y);
        uchar* dst = out.scanLine(y);
        for (int x = 0; x < src.width(); ++x)
            dst[x] = static_cast<uchar>(255 - in[x]);
    }
    return out;
}

void Document::saveSelectionToChannel(const QString& name)
{
    if (selection.image().isNull()) return;

    DocumentChannel ch;
    ch.kind = ChannelKind::Alpha;
    ch.name = name;
    // A straight copy of the selection mask: 255 = selected, and every
    // intermediate value (anti-aliasing, feather) carries over untouched. A
    // threshold here is what would turn a soft selection into a hard one.
    ch.data = selection.image().format() == QImage::Format_Grayscale8
        ? selection.image().copy()
        : selection.image().convertToFormat(QImage::Format_Grayscale8);
    ch.visible = false;
    addChannel(std::move(ch));
}

void Document::loadChannelToSelection(int index, SelectMode mode)
{
    if (index < 0 || index >= static_cast<int>(channels.size())) return;

    // Presence, not raw bytes: a Spot's black (full ink) must load as a
    // SELECTED region, not as an empty one.
    QImage coverage = channelCoverageMask(index);
    if (coverage.isNull()) return;

    if (selection.image().isNull() || selection.image().size() != size)
        selection.resize(size.width(), size.height());

    // Channel buffers are kept at canvas size by ensureChannelBuffers() and by
    // every geometry operation, so this is a straight copy at full resolution —
    // no rescaling, which used to distort a channel after a crop.
    QImage temp(selection.width(), selection.height(), QImage::Format_Grayscale8);
    temp.fill(0);
    const int w = std::min(coverage.width(), temp.width());
    const int h = std::min(coverage.height(), temp.height());
    for (int y = 0; y < h; ++y)
        std::copy(coverage.constScanLine(y), coverage.constScanLine(y) + w,
                  temp.scanLine(y));

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

    selection.setActive(!selection.isEmpty());
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
