#include "GimpBrushDecoder.hpp"
#include "BrushImportTypes.hpp"

#include <QtGlobal>

namespace {

// GIMP v2 brush magic number ('GIMP', big-endian) and the on-disk header sizes.
constexpr quint32 kGimpV2Magic = (quint32('G') << 24) | (quint32('I') << 16)
                               | (quint32('M') << 8) | quint32('P');
constexpr int kGbrV1HeaderSize = 20; // header_size..bytes
constexpr int kGbrV2HeaderSize = 28; // + magic_number + spacing

bool readU32(const QByteArray& d, qint64 off, quint32& out)
{
    if (off < 0 || off + 4 > d.size())
        return false;
    out = (quint32(quint8(d[int(off)])) << 24) | (quint32(quint8(d[int(off) + 1])) << 16)
        | (quint32(quint8(d[int(off) + 2])) << 8) | quint32(quint8(d[int(off) + 3]));
    return true;
}

bool validDims(quint32 w, quint32 h)
{
    return w >= 1 && h >= 1
        && w <= quint32(BrushImportLimits::MaxBrushTipDimension)
        && h <= quint32(BrushImportLimits::MaxBrushTipDimension);
}

// Decode one GBR record starting at `pos`. Appends a frame on success, advances
// `pos` past the record, and (on the first frame) fills name/spacing. Mirrors
// KisGbrBrush::init() with every access bounds-checked.
bool decodeGbrFrame(const QByteArray& d, qint64& pos, GimpBrushDecoder::Result& out, bool first)
{
    quint32 headerSize = 0, version = 0, width = 0, height = 0, bytes = 0;
    if (!readU32(d, pos + 0, headerSize) || !readU32(d, pos + 4, version)
        || !readU32(d, pos + 8, width) || !readU32(d, pos + 12, height)
        || !readU32(d, pos + 16, bytes))
        return false;

    if (version != 1) {
        quint32 magic = 0;
        if (!readU32(d, pos + 20, magic) || magic != kGimpV2Magic)
            return false;
    }
    if (!validDims(width, height) || (bytes != 1 && bytes != 4))
        return false;

    const int structSize = (version == 1) ? kGbrV1HeaderSize : kGbrV2HeaderSize;
    if (qint64(headerSize) < structSize)
        return false;
    const qint64 headerStart = pos;
    if (headerStart + headerSize > d.size())
        return false;

    const qint64 pixelCount = qint64(width) * qint64(height);
    const qint64 dataBytes = pixelCount * bytes;
    const qint64 pixelStart = headerStart + headerSize;
    if (pixelStart + dataBytes > d.size())
        return false;

    if (first) {
        // Name: between the struct and header_size, NUL-terminated.
        const qint64 nameStart = headerStart + structSize;
        const int nameLen = int(headerSize) - structSize - 1; // drop the terminator
        if (nameLen > 0 && nameStart + nameLen <= d.size()) {
            out.name = (version == 1)
                ? QString::fromLatin1(d.constData() + nameStart, nameLen).trimmed()
                : QString::fromUtf8(d.constData() + nameStart, nameLen).trimmed();
        }
        if (version != 1) {
            quint32 spacing = 0;
            if (readU32(d, headerStart + 24, spacing) && spacing <= 1000)
                out.spacingPercent = float(spacing);
        }
    }

    QImage image(int(width), int(height), QImage::Format_ARGB32);
    const uchar* src = reinterpret_cast<const uchar*>(d.constData()) + pixelStart;
    if (bytes == 1) {
        // Mask: Krita stores 255 - byte as the grayscale value; alpha is opaque so
        // the engine's luminance-to-coverage path reproduces the original mask.
        for (quint32 y = 0; y < height; ++y) {
            QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(int(y)));
            const uchar* row = src + qint64(y) * width;
            for (quint32 x = 0; x < width; ++x) {
                const int g = 255 - int(row[x]);
                dst[x] = qRgba(g, g, g, 255);
            }
        }
        out.frames.append({ image, /*colored=*/false });
    } else {
        // RGBA colour stamp, stored R,G,B,A.
        for (quint32 y = 0; y < height; ++y) {
            QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(int(y)));
            const uchar* row = src + qint64(y) * width * 4;
            for (quint32 x = 0; x < width; ++x) {
                const uchar* p = row + qint64(x) * 4;
                dst[x] = qRgba(p[0], p[1], p[2], p[3]);
            }
        }
        out.frames.append({ image, /*colored=*/!image.allGray() });
    }

    pos = pixelStart + dataBytes;
    return true;
}

// Read one newline-terminated line as UTF-8, advancing `pos` past the newline.
QString readLine(const QByteArray& d, qint64& pos)
{
    const qint64 start = pos;
    while (pos < d.size() && d[int(pos)] != '\n')
        ++pos;
    const QString line = QString::fromUtf8(d.constData() + start, int(pos - start));
    if (pos < d.size())
        ++pos; // skip the newline
    return line;
}

} // namespace

namespace GimpBrushDecoder {

bool looksLikeGimpBrush(const QByteArray& bytes)
{
    if (bytes.size() < kGbrV1HeaderSize)
        return false;

    // GBR v2+: 'GIMP' magic at offset 20.
    quint32 version = 0, magic = 0;
    if (readU32(bytes, 4, version)) {
        if (version != 1 && readU32(bytes, 20, magic) && magic == kGimpV2Magic)
            return true;
        // GBR v1: plausible header_size and dimensions, no magic.
        quint32 headerSize = 0, width = 0, height = 0, bpp = 0;
        if (version == 1 && readU32(bytes, 0, headerSize) && readU32(bytes, 8, width)
            && readU32(bytes, 12, height) && readU32(bytes, 16, bpp)
            && headerSize >= quint32(kGbrV1HeaderSize) && validDims(width, height)
            && (bpp == 1 || bpp == 4))
            return true;
    }

    // GIH: a text name line, then "<count> <parasite>". Sniff the second line.
    qint64 pos = 0;
    (void)readLine(bytes, pos);
    if (pos <= 0 || pos >= bytes.size())
        return false;
    const QString l2 = readLine(bytes, pos);
    bool ok = false;
    const int count = l2.left(l2.indexOf(QLatin1Char(' '))).toInt(&ok);
    return ok && count >= 1 && count <= BrushImportLimits::MaxBrushesPerFile;
}

Result decode(const QByteArray& bytes)
{
    Result out;
    if (bytes.size() > BrushImportLimits::MaxFileSize)
        return out;

    // Try a single GBR at offset 0.
    {
        qint64 pos = 0;
        if (decodeGbrFrame(bytes, pos, out, /*first=*/true)) {
            out.valid = true;
            return out;
        }
    }

    // Otherwise treat it as a GIH: name line, "<count> <parasite>", then frames.
    out = Result{};
    qint64 pos = 0;
    const QString nameLine = readLine(bytes, pos);
    if (pos <= 0 || pos >= bytes.size())
        return out;
    const QString paramLine = readLine(bytes, pos);
    bool ok = false;
    const int spaceIdx = paramLine.indexOf(QLatin1Char(' '));
    const int count = paramLine.left(spaceIdx).toInt(&ok);
    if (!ok || count < 1 || count > BrushImportLimits::MaxBrushesPerFile)
        return out;

    out.isPipe = true;
    out.name = nameLine.trimmed();
    if (spaceIdx >= 0) {
        // Parasite, e.g. "ncells:8 ... selection:random". Pull the selection mode.
        const QString parasite = paramLine.mid(spaceIdx + 1);
        for (const QString& token : parasite.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (token.startsWith(QLatin1String("selection:")))
                out.parasiteSelection = token.mid(QStringLiteral("selection:").size());
        }
    }

    for (int i = 0; i < count && pos < bytes.size(); ++i) {
        if (!decodeGbrFrame(bytes, pos, out, /*first=*/(i == 0)))
            break;
    }
    out.valid = !out.frames.isEmpty();
    if (out.valid && !nameLine.trimmed().isEmpty())
        out.name = nameLine.trimmed(); // prefer the pipe name over the first frame's
    return out;
}

} // namespace GimpBrushDecoder
