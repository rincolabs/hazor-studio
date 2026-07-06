#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QHash>

// Small, dependency-light helpers shared by the Krita adapters: zlib inflate
// (for compressed PNG text chunks) and a read-only ZIP reader (for .bundle
// resource packages). The ZIP reader is deliberately minimal — it parses the
// end-of-central-directory + central directory and inflates STORE/DEFLATE
// entries — and is hardened against the malformed-archive risks called out in
// the spec: path traversal, oversized entries, and bogus offsets.

namespace BrushImportArchive {

// Inflate a zlib stream (RFC 1950 header). Returns empty on failure. The output
// is capped at maxOut bytes to bound memory for hostile inputs.
QByteArray inflateZlib(const QByteArray& in,
                       qint64 maxOut = qint64(64) * 1024 * 1024);

// Inflate a raw DEFLATE stream (no zlib header, windowBits -15), as stored in
// ZIP entries. Capped at maxOut bytes.
QByteArray inflateRaw(const QByteArray& in, qint64 expectedSize,
                      qint64 maxOut = qint64(64) * 1024 * 1024);

class ZipReader {
public:
    explicit ZipReader(const QByteArray& archive);

    bool isValid() const { return m_valid; }
    QStringList entryNames() const;
    bool contains(const QString& name) const { return m_entries.contains(name); }

    // Decompressed bytes for an entry, or empty if missing/too large/corrupt.
    QByteArray read(const QString& name) const;

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    const QByteArray m_data;
    QHash<QString, Entry> m_entries;
    bool m_valid = false;

    void parse();
};

// Reject names that escape the archive root (absolute paths, "..", drive specs).
bool isSafeEntryName(const QString& name);

} // namespace BrushImportArchive
