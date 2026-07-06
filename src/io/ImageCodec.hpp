#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <vector>

#include "color/ColorProfile.hpp"

enum class PixelFormat {
    RGBA8,
    RGBA16,
    RGBA32F,
    Gray8,
    Gray16,
    Gray32F
};

struct DocumentImage {
    int width = 0;
    int height = 0;

    PixelFormat pixelFormat = PixelFormat::RGBA8;

    QByteArray pixels;

    bool hasAlpha = false;
    bool isPremultiplied = false;
    bool isLinear = false;

    QByteArray iccProfile;
    ColorProfile colorProfile;
    QVariantMap metadata;

    QString sourcePath;
    QString sourceFormat;
};

struct ImageLoadResult {
    bool ok = false;
    QString errorMessage;
    DocumentImage image;

    static ImageLoadResult success(DocumentImage image);
    static ImageLoadResult error(const QString& message);
};

struct ImageSaveOptions {
    int quality = 90;

    bool preserveAlpha = true;
    bool embedColorProfile = true;
    bool embedMetadata = true;

    int compressionLevel = -1;
    int bitDepth = 8;

    bool lossless = false;
    bool progressive = false;
};

class ImageCodec {
public:
    virtual ~ImageCodec() = default;

    virtual QString name() const = 0;
    virtual QStringList readableFormats() const = 0;
    virtual QStringList writableFormats() const = 0;

    virtual bool canRead(const QString& path) const = 0;
    virtual bool canWrite(const QString& extension) const = 0;

    virtual ImageLoadResult read(const QString& path) = 0;

    virtual bool write(const QString& path,
                       const DocumentImage& image,
                       const ImageSaveOptions& options,
                       QString* errorMessage) = 0;
};

class ImageCodecRegistry {
public:
    void registerCodec(std::unique_ptr<ImageCodec> codec);

    ImageCodec* findReader(const QString& path);
    ImageCodec* findWriter(const QString& extension);
    ImageLoadResult readImage(const QString& path);

    QStringList allReadableFormats() const;
    QStringList allWritableFormats() const;

    QString openFileFilter() const;
    QString saveFileFilter() const;

    // A single-entry save filter locked to one format (all extensions that map
    // to the same writer, e.g. "*.jpg *.jpeg"). Used to open the save dialog
    // pre-locked to the format the user picked in the export dialog, so the
    // extension is applied automatically and cannot be changed there.
    QString saveFilterForFormat(const QString& format) const;

private:
    std::vector<std::unique_ptr<ImageCodec>> m_codecs;
};

QString normalizeImageExtension(const QString& extension);
QString pixelFormatName(PixelFormat format);
bool imageFormatSupportsAlpha(const QString& extension);

// Qt's PNG handler does not honour QImageWriter::setCompression() the way one
// would expect: passing any CompressionRatio value disables zlib compression
// entirely (producing an essentially uncompressed file). The real compression
// level (0..9) is driven through the *quality* option instead, mapped roughly
// as zlibLevel = (100 - quality) / 11. This helper converts a 0..9 PNG
// compression level into the quality value Qt needs to actually apply it.
int qtPngQualityForCompressionLevel(int level);

// Encodes `image` to `extension` entirely in memory and returns the resulting
// byte count, or -1 if the format cannot be encoded. Mirrors the real export
// path (Qt first, OpenCV fallback for formats Qt can't write such as webp/tiff)
// so the export dialog can show an accurate size estimate for every format.
//
// When `roundTrip` is non-null it receives the image decoded back from the
// encoded bytes (using the same backend that encoded them). For lossy formats
// this is what the exported file will actually look like, so the preview can
// reflect the chosen quality.
qint64 estimateEncodedSize(const QImage& image,
                           const QString& extension,
                           const ImageSaveOptions& options,
                           QImage* roundTrip = nullptr);

ImageLoadResult convertQImageToDocumentImage(const QImage& input,
                                             const QString& sourcePath,
                                             const QByteArray& sourceFormat);
QImage convertDocumentImageToQImage(const DocumentImage& image);
DocumentImage convertQImageToDocumentImageValue(const QImage& input,
                                                const QString& sourcePath,
                                                const QString& sourceFormat);

void registerDefaultImageCodecs(ImageCodecRegistry& registry);
ImageCodecRegistry& imageCodecRegistry();
