#include "BrushImportArchive.hpp"

#include <zlib.h>
#include <QtGlobal>
#include <cstring>

namespace BrushImportArchive {

namespace {
// Little-endian readers for the ZIP structures (ZIP is always little-endian).
quint16 rd16(const QByteArray& d, int off) {
    if (off < 0 || off + 2 > d.size()) return 0;
    return quint16(quint8(d[off])) | (quint16(quint8(d[off + 1])) << 8);
}
quint32 rd32(const QByteArray& d, int off) {
    if (off < 0 || off + 4 > d.size()) return 0;
    return quint32(quint8(d[off])) | (quint32(quint8(d[off + 1])) << 8)
         | (quint32(quint8(d[off + 2])) << 16) | (quint32(quint8(d[off + 3])) << 24);
}

QByteArray inflateImpl(const QByteArray& in, int windowBits, qint64 expectedSize, qint64 maxOut)
{
    if (in.isEmpty()) return {};
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, windowBits) != Z_OK)
        return {};

    QByteArray out;
    qint64 reserve = expectedSize > 0 ? qMin(expectedSize, maxOut)
                                      : qMin<qint64>(in.size() * 4, maxOut);
    if (reserve < 16384) reserve = 16384;
    out.reserve(int(reserve));

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    zs.avail_in = uInt(in.size());

    char buf[32768];
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&zs);
            return {};
        }
        const int produced = int(sizeof(buf) - zs.avail_out);
        if (produced > 0) {
            if (out.size() + produced > maxOut) { inflateEnd(&zs); return {}; }
            out.append(buf, produced);
        }
        if (ret == Z_BUF_ERROR && produced == 0)
            break; // no progress possible
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return out;
}
} // namespace

QByteArray inflateZlib(const QByteArray& in, qint64 maxOut)
{
    return inflateImpl(in, 15 /*zlib header*/, 0, maxOut);
}

QByteArray inflateRaw(const QByteArray& in, qint64 expectedSize, qint64 maxOut)
{
    return inflateImpl(in, -15 /*raw deflate*/, expectedSize, maxOut);
}

bool isSafeEntryName(const QString& name)
{
    if (name.isEmpty()) return false;
    if (name.startsWith(QLatin1Char('/')) || name.startsWith(QLatin1Char('\\')))
        return false;
    if (name.contains(QLatin1Char(':')))           // drive letters
        return false;
    if (name.contains(QStringLiteral("..")))       // traversal
        return false;
    return true;
}

ZipReader::ZipReader(const QByteArray& archive)
    : m_data(archive)
{
    parse();
}

void ZipReader::parse()
{
    // Locate the End Of Central Directory record by scanning backwards for its
    // signature (PK\x05\x06). The comment field can be up to 64 KiB.
    static const char eocdSig[4] = { 'P', 'K', 0x05, 0x06 };
    const int n = m_data.size();
    if (n < 22) return;
    int eocd = -1;
    const int minPos = qMax(0, n - 22 - 65536);
    for (int i = n - 22; i >= minPos; --i) {
        if (m_data[i] == eocdSig[0] && m_data[i + 1] == eocdSig[1]
            && m_data[i + 2] == eocdSig[2] && m_data[i + 3] == eocdSig[3]) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return;

    const quint16 totalEntries = rd16(m_data, eocd + 10);
    const quint32 cdSize = rd32(m_data, eocd + 12);
    const quint32 cdOffset = rd32(m_data, eocd + 16);
    if (qint64(cdOffset) + cdSize > n) return;

    int pos = int(cdOffset);
    const int cdEnd = int(cdOffset + cdSize);
    int parsed = 0;
    while (pos + 46 <= cdEnd && parsed < totalEntries && parsed < 100000) {
        if (rd32(m_data, pos) != 0x02014b50) break; // central dir signature
        const quint16 method = rd16(m_data, pos + 10);
        const quint32 compSize = rd32(m_data, pos + 20);
        const quint32 uncompSize = rd32(m_data, pos + 24);
        const quint16 nameLen = rd16(m_data, pos + 28);
        const quint16 extraLen = rd16(m_data, pos + 30);
        const quint16 commentLen = rd16(m_data, pos + 32);
        const quint32 localOffset = rd32(m_data, pos + 42);

        if (pos + 46 + nameLen > cdEnd) break;
        const QString name = QString::fromUtf8(m_data.constData() + pos + 46, nameLen);

        if (!name.endsWith(QLatin1Char('/')) && isSafeEntryName(name)) {
            Entry e;
            e.method = method;
            e.compressedSize = compSize;
            e.uncompressedSize = uncompSize;
            e.localHeaderOffset = localOffset;
            m_entries.insert(name, e);
        }
        pos += 46 + nameLen + extraLen + commentLen;
        ++parsed;
    }
    m_valid = !m_entries.isEmpty();
}

QStringList ZipReader::entryNames() const
{
    return m_entries.keys();
}

QByteArray ZipReader::read(const QString& name) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return {};
    const Entry& e = *it;
    constexpr quint32 kMaxEntry = 128u * 1024u * 1024u; // bound hostile entries
    if (e.uncompressedSize > kMaxEntry)
        return {};

    // Re-read the local file header to find the actual data start (its name /
    // extra lengths can differ from the central directory record).
    const int lh = int(e.localHeaderOffset);
    if (lh < 0 || lh + 30 > m_data.size()) return {};
    if (rd32(m_data, lh) != 0x04034b50) return {};
    const quint16 nameLen = rd16(m_data, lh + 26);
    const quint16 extraLen = rd16(m_data, lh + 28);
    const int dataStart = lh + 30 + nameLen + extraLen;
    if (dataStart < 0 || qint64(dataStart) + e.compressedSize > m_data.size())
        return {};

    const QByteArray comp = m_data.mid(dataStart, int(e.compressedSize));
    if (e.method == 0)            // STORE
        return comp;
    if (e.method == 8)            // DEFLATE
        return inflateRaw(comp, e.uncompressedSize);
    return {};                    // unsupported method
}

} // namespace BrushImportArchive
