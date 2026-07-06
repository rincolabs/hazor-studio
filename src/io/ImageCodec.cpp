#include "ImageCodec.hpp"

#include <QBuffer>
#include <QFileInfo>
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QDebug>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstring>

namespace {

QStringList sortedUnique(QStringList formats)
{
    for (QString& format : formats)
        format = normalizeImageExtension(format);
    formats.removeAll(QString());
    formats.removeDuplicates();
    formats.sort(Qt::CaseInsensitive);
    return formats;
}

QStringList sortedUniquePatterns(QStringList patterns)
{
    patterns.removeAll(QString());
    patterns.removeDuplicates();
    patterns.sort(Qt::CaseInsensitive);
    return patterns;
}

QStringList qtFormats(const QList<QByteArray>& rawFormats)
{
    QStringList formats;
    for (const QByteArray& format : rawFormats)
        formats << QString::fromLatin1(format).toLower();
    return sortedUnique(formats);
}

int bytesPerPixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::RGBA8: return 4;
    case PixelFormat::RGBA16: return 8;
    case PixelFormat::RGBA32F: return 16;
    case PixelFormat::Gray8: return 1;
    case PixelFormat::Gray16: return 2;
    case PixelFormat::Gray32F: return 4;
    }
    return 0;
}

bool hasExpectedPayload(const DocumentImage& image)
{
    if (image.width <= 0 || image.height <= 0)
        return false;
    const int bpp = bytesPerPixel(image.pixelFormat);
    if (bpp <= 0)
        return false;
    const qsizetype expected = static_cast<qsizetype>(image.width)
        * static_cast<qsizetype>(image.height)
        * static_cast<qsizetype>(bpp);
    return image.pixels.size() >= expected;
}

QImage flattenAlphaForOpaqueFormat(QImage image)
{
    if (image.isNull() || !image.hasAlphaChannel())
        return image.convertToFormat(QImage::Format_RGB32);

    QImage flattened(image.size(), QImage::Format_RGB32);
    flattened.fill(Qt::white);
    QPainter painter(&flattened);
    painter.drawImage(0, 0, image);
    return flattened;
}

std::vector<int> openCvWriteParams(const QString& extension,
                                   const ImageSaveOptions& options)
{
    std::vector<int> params;
    const QString ext = normalizeImageExtension(extension);

    if ((ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        && options.quality >= 0) {
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(std::clamp(options.quality, 0, 100));
    } else if (ext == QLatin1String("png") && options.compressionLevel >= 0) {
        params.push_back(cv::IMWRITE_PNG_COMPRESSION);
        params.push_back(std::clamp(options.compressionLevel, 0, 9));
    } else if (ext == QLatin1String("webp") && options.quality >= 0) {
        params.push_back(cv::IMWRITE_WEBP_QUALITY);
        params.push_back(std::clamp(options.quality, 1, 100));
    }

    return params;
}

QStringList openCvCandidateReadFormats()
{
    return sortedUnique({
        QStringLiteral("bmp"), QStringLiteral("dib"),
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("jp2"),
        QStringLiteral("png"),
        QStringLiteral("webp"),
        QStringLiteral("avif"),
        QStringLiteral("pbm"), QStringLiteral("pgm"), QStringLiteral("ppm"),
        QStringLiteral("pxm"), QStringLiteral("pnm"),
        QStringLiteral("pfm"),
        QStringLiteral("sr"), QStringLiteral("ras"),
        QStringLiteral("tif"), QStringLiteral("tiff"),
        QStringLiteral("exr"),
        QStringLiteral("hdr"), QStringLiteral("pic"),
        QStringLiteral("gif")
    });
}

QStringList openCvCandidateWriteFormats()
{
    return sortedUnique({
        QStringLiteral("bmp"),
        QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("jp2"),
        QStringLiteral("png"),
        QStringLiteral("webp"),
        QStringLiteral("avif"),
        QStringLiteral("pbm"), QStringLiteral("pgm"), QStringLiteral("ppm"),
        QStringLiteral("pxm"), QStringLiteral("pnm"),
        QStringLiteral("pfm"),
        QStringLiteral("tif"), QStringLiteral("tiff"),
        QStringLiteral("exr"),
        QStringLiteral("hdr")
    });
}

class QtImageCodec final : public ImageCodec {
public:
    QString name() const override { return QStringLiteral("Qt Image Codec"); }

    QStringList readableFormats() const override
    {
        return qtFormats(QImageReader::supportedImageFormats());
    }

    QStringList writableFormats() const override
    {
        return qtFormats(QImageWriter::supportedImageFormats());
    }

    bool canRead(const QString& path) const override
    {
        QImageReader reader(path);
        return reader.canRead();
    }

    bool canWrite(const QString& extension) const override
    {
        return writableFormats().contains(normalizeImageExtension(extension));
    }

    ImageLoadResult read(const QString& path) override
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);

        QImage input = reader.read();
        if (input.isNull()) {
            return ImageLoadResult::error(reader.errorString());
        }

        return convertQImageToDocumentImage(input, path, reader.format());
    }

    bool write(const QString& path,
               const DocumentImage& image,
               const ImageSaveOptions& options,
               QString* errorMessage) override
    {
        QImage output = convertDocumentImageToQImage(image);
        if (output.isNull()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not convert internal image to QImage.");
            return false;
        }

        const QString extension = QFileInfo(path).suffix().toLower();
        if (!options.preserveAlpha || !imageFormatSupportsAlpha(extension))
            output = flattenAlphaForOpaqueFormat(output);
        // Only tag the output with a colour space when embedding is requested, so
        // the "Don't embed profile" export mode actually ships untagged pixels.
        if (options.embedColorProfile) {
            if (image.colorProfile.isValid())
                output.setColorSpace(image.colorProfile.toQColorSpace());
            else if (!image.iccProfile.isEmpty())
                output.setColorSpace(QColorSpace::fromIccProfile(image.iccProfile));
        } else {
            output.setColorSpace(QColorSpace());
        }

        QImageWriter writer(path);
        const bool isLossyFormat = (extension == QLatin1String("jpg")
            || extension == QLatin1String("jpeg")
            || extension == QLatin1String("webp"));
        if (isLossyFormat && options.quality >= 0) {
            writer.setQuality(options.quality);
        } else if (extension == QLatin1String("png") && options.compressionLevel >= 0) {
            // Qt's PNG plugin compresses via the quality option, and calling
            // setCompression() actually disables compression. Translate the
            // requested 0..9 level into the matching quality instead.
            writer.setQuality(qtPngQualityForCompressionLevel(options.compressionLevel));
        } else if (options.compressionLevel >= 0) {
            writer.setCompression(options.compressionLevel);
        }
        if (options.progressive)
            writer.setProgressiveScanWrite(true);

        const bool ok = writer.write(output);
        if (!ok && errorMessage)
            *errorMessage = writer.errorString();
        return ok;
    }
};

class OpenCvImageCodec final : public ImageCodec {
public:
    QString name() const override { return QStringLiteral("OpenCV Image Codec"); }

    QStringList readableFormats() const override
    {
        return openCvCandidateReadFormats();
    }

    QStringList writableFormats() const override
    {
        QStringList out;
        for (const QString& ext : openCvCandidateWriteFormats()) {
            try {
                if (cv::haveImageWriter(QStringLiteral("test.%1").arg(ext).toStdString()))
                    out << ext;
            } catch (const cv::Exception&) {
            }
        }
        return sortedUnique(out);
    }

    bool canRead(const QString& path) const override
    {
        try {
            return cv::haveImageReader(path.toStdString());
        } catch (const cv::Exception&) {
        }

        return readableFormats().contains(normalizeImageExtension(QFileInfo(path).suffix()));
    }

    bool canWrite(const QString& extension) const override
    {
        const QString normalized = normalizeImageExtension(extension);
        try {
            return cv::haveImageWriter(QStringLiteral("test.%1").arg(normalized).toStdString());
        } catch (const cv::Exception&) {
            return writableFormats().contains(normalized);
        }
    }

    ImageLoadResult read(const QString& path) override
    {
        cv::Mat input = cv::imread(path.toStdString(), cv::IMREAD_UNCHANGED);
        if (input.empty())
            return ImageLoadResult::error(QStringLiteral("OpenCV could not decode this image."));

        return convertCvMat(input, path);
    }

    bool write(const QString& path,
               const DocumentImage& image,
               const ImageSaveOptions& options,
               QString* errorMessage) override
    {
        QImage qimage = convertDocumentImageToQImage(image);
        if (qimage.isNull()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not convert internal image to cv::Mat.");
            return false;
        }

        const QString extension = QFileInfo(path).suffix().toLower();
        if (!options.preserveAlpha || !imageFormatSupportsAlpha(extension))
            qimage = flattenAlphaForOpaqueFormat(qimage);

        QImage rgba = qimage.convertToFormat(QImage::Format_RGBA8888);
        cv::Mat wrapped(rgba.height(), rgba.width(), CV_8UC4,
                        const_cast<uchar*>(rgba.constBits()),
                        static_cast<size_t>(rgba.bytesPerLine()));
        cv::Mat output;
        if (imageFormatSupportsAlpha(extension))
            cv::cvtColor(wrapped, output, cv::COLOR_RGBA2BGRA);
        else
            cv::cvtColor(wrapped, output, cv::COLOR_RGBA2BGR);

        try {
            const auto params = openCvWriteParams(extension, options);
            const bool ok = cv::imwrite(path.toStdString(), output, params);
            if (!ok && errorMessage)
                *errorMessage = QStringLiteral("OpenCV could not encode this image.");
            return ok;
        } catch (const cv::Exception& e) {
            if (errorMessage)
                *errorMessage = QString::fromStdString(e.what());
            return false;
        }
    }

private:
    static ImageLoadResult convertCvMat(const cv::Mat& input, const QString& sourcePath)
    {
        if (input.empty())
            return ImageLoadResult::error(QStringLiteral("Empty cv::Mat."));

        const int channels = input.channels();
        const int depth = input.depth();

        cv::Mat rgba;
        if (channels == 1) {
            cv::cvtColor(input, rgba, cv::COLOR_GRAY2RGBA);
        } else if (channels == 3) {
            cv::cvtColor(input, rgba, cv::COLOR_BGR2RGBA);
        } else if (channels == 4) {
            cv::cvtColor(input, rgba, cv::COLOR_BGRA2RGBA);
        } else {
            return ImageLoadResult::error(
                QStringLiteral("Unsupported channel count: %1").arg(channels));
        }

        if (!rgba.isContinuous())
            rgba = rgba.clone();

        DocumentImage result;
        result.width = rgba.cols;
        result.height = rgba.rows;
        result.sourcePath = sourcePath;
        result.sourceFormat = QFileInfo(sourcePath).suffix().toLower();
        result.hasAlpha = channels == 4;
        result.isPremultiplied = false;

        if (depth == CV_8U) {
            result.pixelFormat = PixelFormat::RGBA8;
        } else if (depth == CV_16U) {
            result.pixelFormat = PixelFormat::RGBA16;
        } else if (depth == CV_32F) {
            result.pixelFormat = PixelFormat::RGBA32F;
            result.isLinear = true;
        } else {
            return ImageLoadResult::error(
                QStringLiteral("Unsupported image depth: %1").arg(depth));
        }

        result.pixels = QByteArray(reinterpret_cast<const char*>(rgba.data),
                                   static_cast<qsizetype>(rgba.total() * rgba.elemSize()));
        result.colorProfile = ColorProfile::invalid();
        result.colorProfile.setSource(ColorProfileSource::Missing);
        result.metadata.insert(
            QStringLiteral("iccWarning"),
            QStringLiteral("Loaded through OpenCV fallback. Embedded ICC profile was not preserved."));

        return ImageLoadResult::success(std::move(result));
    }
};

} // namespace

ImageLoadResult ImageLoadResult::success(DocumentImage image)
{
    ImageLoadResult result;
    result.ok = true;
    result.image = std::move(image);
    return result;
}

ImageLoadResult ImageLoadResult::error(const QString& message)
{
    ImageLoadResult result;
    result.ok = false;
    result.errorMessage = message.isEmpty()
        ? QStringLiteral("Unsupported or invalid image file.")
        : message;
    return result;
}

QString normalizeImageExtension(const QString& extension)
{
    QString normalized = extension.trimmed().toLower();
    while (normalized.startsWith(QLatin1Char('.')))
        normalized.remove(0, 1);
    if (normalized == QLatin1String("jpe"))
        return QStringLiteral("jpg");
    return normalized;
}

QString pixelFormatName(PixelFormat format)
{
    switch (format) {
    case PixelFormat::RGBA8: return QStringLiteral("RGBA8");
    case PixelFormat::RGBA16: return QStringLiteral("RGBA16");
    case PixelFormat::RGBA32F: return QStringLiteral("RGBA32F");
    case PixelFormat::Gray8: return QStringLiteral("Gray8");
    case PixelFormat::Gray16: return QStringLiteral("Gray16");
    case PixelFormat::Gray32F: return QStringLiteral("Gray32F");
    }
    return QStringLiteral("Unknown");
}

int qtPngQualityForCompressionLevel(int level)
{
    // Qt maps PNG quality (0..100) to a zlib level of (100 - quality) / 11.
    // Inverting that, quality = 100 - 11 * level yields exactly the requested
    // level: level 0 -> quality 100 (no compression), level 9 -> quality 1
    // (maximum compression).
    return 100 - 11 * std::clamp(level, 0, 9);
}

namespace {

// Converts an 8-bit BGR/BGRA cv::Mat into an RGBA8888 QImage (deep copy).
QImage cvMat8ToQImage(const cv::Mat& mat)
{
    if (mat.empty() || mat.depth() != CV_8U)
        return {};
    cv::Mat rgba;
    if (mat.channels() == 4)
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
    else if (mat.channels() == 3)
        cv::cvtColor(mat, rgba, cv::COLOR_BGR2RGBA);
    else if (mat.channels() == 1)
        cv::cvtColor(mat, rgba, cv::COLOR_GRAY2RGBA);
    else
        return {};
    QImage img(rgba.cols, rgba.rows, QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.rows; ++y)
        std::memcpy(img.scanLine(y), rgba.ptr(y), static_cast<size_t>(rgba.cols) * 4);
    return img;
}

} // namespace

qint64 estimateEncodedSize(const QImage& source,
                           const QString& extension,
                           const ImageSaveOptions& options,
                           QImage* roundTrip)
{
    if (source.isNull())
        return -1;

    const QString ext = normalizeImageExtension(extension);
    QImage image = source;
    if (!options.preserveAlpha || !imageFormatSupportsAlpha(ext))
        image = flattenAlphaForOpaqueFormat(image);

    // Prefer Qt's in-memory writer whenever this build can encode the format,
    // applying the exact same option translation the real export uses.
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, ext.toLatin1());
        if (writer.canWrite()) {
            const bool isLossyFormat = (ext == QLatin1String("jpg")
                || ext == QLatin1String("jpeg")
                || ext == QLatin1String("webp"));
            if (isLossyFormat && options.quality >= 0)
                writer.setQuality(options.quality);
            else if (ext == QLatin1String("png") && options.compressionLevel >= 0)
                writer.setQuality(qtPngQualityForCompressionLevel(options.compressionLevel));
            if (options.progressive
                && (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")))
                writer.setProgressiveScanWrite(true);
            if (writer.write(image)) {
                if (roundTrip)
                    *roundTrip = QImage::fromData(buffer.data(), ext.toLatin1().constData());
                return buffer.size();
            }
        }
    }

    // Fall back to OpenCV for formats Qt can't encode (webp, tiff, jp2, ...).
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat wrapped(rgba.height(), rgba.width(), CV_8UC4,
                    const_cast<uchar*>(rgba.constBits()),
                    static_cast<size_t>(rgba.bytesPerLine()));
    cv::Mat output;
    if (imageFormatSupportsAlpha(ext))
        cv::cvtColor(wrapped, output, cv::COLOR_RGBA2BGRA);
    else
        cv::cvtColor(wrapped, output, cv::COLOR_RGBA2BGR);

    try {
        std::vector<uchar> encoded;
        if (cv::imencode(QStringLiteral(".%1").arg(ext).toStdString(),
                         output, encoded, openCvWriteParams(ext, options))) {
            if (roundTrip) {
                cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_UNCHANGED);
                *roundTrip = cvMat8ToQImage(decoded);
            }
            return static_cast<qint64>(encoded.size());
        }
    } catch (const cv::Exception&) {
    }
    return -1;
}

bool imageFormatSupportsAlpha(const QString& extension)
{
    const QString ext = normalizeImageExtension(extension);
    return ext == QLatin1String("png")
        || ext == QLatin1String("webp")
        || ext == QLatin1String("tif")
        || ext == QLatin1String("tiff")
        || ext == QLatin1String("exr")
        || ext == QLatin1String("tga")
        || ext == QLatin1String("avif");
}

ImageLoadResult convertQImageToDocumentImage(const QImage& input,
                                             const QString& sourcePath,
                                             const QByteArray& sourceFormat)
{
    if (input.isNull())
        return ImageLoadResult::error(QStringLiteral("Invalid QImage."));

    DocumentImage result;
    result.width = input.width();
    result.height = input.height();
    result.sourcePath = sourcePath;
    result.sourceFormat = QString::fromLatin1(sourceFormat).toLower();
    if (result.sourceFormat.isEmpty())
        result.sourceFormat = QFileInfo(sourcePath).suffix().toLower();
    result.hasAlpha = input.hasAlphaChannel();
    result.isPremultiplied = false;
    if (input.colorSpace().isValid()) {
        result.colorProfile = ColorProfile::fromQColorSpace(input.colorSpace());
        result.colorProfile.setSource(ColorProfileSource::EmbeddedInFile);
        result.iccProfile = input.colorSpace().iccProfile();
    } else {
        result.colorProfile = ColorProfile::invalid();
        result.colorProfile.setSource(ColorProfileSource::Missing);
    }

    QImage normalized;
    if (input.depth() > 32) {
        normalized = input.convertToFormat(QImage::Format_RGBA64);
        result.pixelFormat = PixelFormat::RGBA16;
    } else {
        normalized = input.convertToFormat(QImage::Format_RGBA8888);
        result.pixelFormat = PixelFormat::RGBA8;
    }

    if (normalized.isNull())
        return ImageLoadResult::error(QStringLiteral("Could not normalize image pixels."));

    result.pixels = QByteArray(reinterpret_cast<const char*>(normalized.constBits()),
                               normalized.sizeInBytes());

    return ImageLoadResult::success(std::move(result));
}

DocumentImage convertQImageToDocumentImageValue(const QImage& input,
                                                const QString& sourcePath,
                                                const QString& sourceFormat)
{
    ImageLoadResult result = convertQImageToDocumentImage(
        input, sourcePath, sourceFormat.toLatin1());
    return result.image;
}

QImage convertDocumentImageToQImage(const DocumentImage& image)
{
    if (!hasExpectedPayload(image))
        return {};

    if (image.pixelFormat == PixelFormat::RGBA8) {
        QImage wrapped(reinterpret_cast<const uchar*>(image.pixels.constData()),
                       image.width, image.height, image.width * 4,
                       QImage::Format_RGBA8888);
        QImage copy = wrapped.copy();
        if (image.colorProfile.isValid())
            copy.setColorSpace(image.colorProfile.toQColorSpace());
        else if (!image.iccProfile.isEmpty())
            copy.setColorSpace(QColorSpace::fromIccProfile(image.iccProfile));
        return copy;
    }

    if (image.pixelFormat == PixelFormat::RGBA16) {
        QImage wrapped(reinterpret_cast<const uchar*>(image.pixels.constData()),
                       image.width, image.height, image.width * 8,
                       QImage::Format_RGBA64);
        // TODO: keep RGBA16 end-to-end once layer storage and rendering support it.
        QImage converted = wrapped.convertToFormat(QImage::Format_RGBA8888);
        if (image.colorProfile.isValid())
            converted.setColorSpace(image.colorProfile.toQColorSpace());
        else if (!image.iccProfile.isEmpty())
            converted.setColorSpace(QColorSpace::fromIccProfile(image.iccProfile));
        return converted;
    }

    if (image.pixelFormat == PixelFormat::RGBA32F) {
        // TODO: keep RGBA32F end-to-end once layer storage and rendering support it.
        QImage output(image.width, image.height, QImage::Format_RGBA8888);
        const float* src = reinterpret_cast<const float*>(image.pixels.constData());
        for (int y = 0; y < image.height; ++y) {
            auto* dst = reinterpret_cast<uchar*>(output.scanLine(y));
            for (int x = 0; x < image.width; ++x) {
                const int si = (y * image.width + x) * 4;
                const int di = x * 4;
                dst[di + 0] = static_cast<uchar>(std::clamp(src[si + 0], 0.0f, 1.0f) * 255.0f);
                dst[di + 1] = static_cast<uchar>(std::clamp(src[si + 1], 0.0f, 1.0f) * 255.0f);
                dst[di + 2] = static_cast<uchar>(std::clamp(src[si + 2], 0.0f, 1.0f) * 255.0f);
                dst[di + 3] = static_cast<uchar>(std::clamp(src[si + 3], 0.0f, 1.0f) * 255.0f);
            }
        }
        if (image.colorProfile.isValid())
            output.setColorSpace(image.colorProfile.toQColorSpace());
        else if (!image.iccProfile.isEmpty())
            output.setColorSpace(QColorSpace::fromIccProfile(image.iccProfile));
        return output;
    }

    return {};
}

void ImageCodecRegistry::registerCodec(std::unique_ptr<ImageCodec> codec)
{
    if (codec)
        m_codecs.push_back(std::move(codec));
}

ImageCodec* ImageCodecRegistry::findReader(const QString& path)
{
    for (const auto& codec : m_codecs) {
        if (codec->canRead(path))
            return codec.get();
    }
    return nullptr;
}

ImageCodec* ImageCodecRegistry::findWriter(const QString& extension)
{
    for (const auto& codec : m_codecs) {
        if (codec->canWrite(extension))
            return codec.get();
    }
    return nullptr;
}

ImageLoadResult ImageCodecRegistry::readImage(const QString& path)
{
    QStringList errors;

    for (const auto& codec : m_codecs) {
        if (!codec->canRead(path))
            continue;

        ImageLoadResult result = codec->read(path);
        if (result.ok)
            return result;

        errors << QStringLiteral("%1: %2").arg(codec->name(), result.errorMessage);
    }

    if (!errors.isEmpty())
        return ImageLoadResult::error(errors.join(QStringLiteral("\n")));

    return ImageLoadResult::error(
        QStringLiteral("This file format is not supported by the current Qt/OpenCV build."));
}

QStringList ImageCodecRegistry::allReadableFormats() const
{
    QStringList formats;
    for (const auto& codec : m_codecs)
        formats << codec->readableFormats();
    return sortedUnique(formats);
}

QStringList ImageCodecRegistry::allWritableFormats() const
{
    QStringList formats;
    for (const auto& codec : m_codecs)
        formats << codec->writableFormats();
    return sortedUnique(formats);
}

QString ImageCodecRegistry::openFileFilter() const
{
    QStringList allPatterns;

    for (const auto& codec : m_codecs) {
        for (const QString& format : codec->readableFormats()) {
            allPatterns << QStringLiteral("*.%1").arg(format);
            allPatterns << QStringLiteral("*.%1").arg(format.toUpper());
        }
    }

    allPatterns = sortedUniquePatterns(allPatterns);

    QStringList filters;
    if (!allPatterns.isEmpty()) {
        filters << QStringLiteral("All supported images (%1)")
            .arg(allPatterns.join(QLatin1Char(' ')));
    }
    filters << QStringLiteral("All files (*)");
    return filters.join(QStringLiteral(";;"));
}

QString ImageCodecRegistry::saveFileFilter() const
{
    QStringList allPatterns;
    QStringList codecGroups;

    for (const auto& codec : m_codecs) {
        QStringList patterns;
        for (const QString& format : codec->writableFormats()) {
            patterns << QStringLiteral("*.%1").arg(format);
            patterns << QStringLiteral("*.%1").arg(format.toUpper());
            allPatterns << QStringLiteral("*.%1").arg(format);
            allPatterns << QStringLiteral("*.%1").arg(format.toUpper());
        }

        patterns = sortedUniquePatterns(patterns);
        if (!patterns.isEmpty()) {
            codecGroups << QStringLiteral("%1 (%2)")
                .arg(codec->name(), patterns.join(QLatin1Char(' ')));
        }
    }

    allPatterns = sortedUniquePatterns(allPatterns);

    QStringList filters;
    if (!allPatterns.isEmpty()) {
        filters << QStringLiteral("All supported images (%1)")
            .arg(allPatterns.join(QLatin1Char(' ')));
    }
    filters << codecGroups;
    filters << QStringLiteral("All files (*)");
    return filters.join(QStringLiteral(";;"));
}

QString ImageCodecRegistry::saveFilterForFormat(const QString& format) const
{
    const QString normalized = normalizeImageExtension(format);
    if (normalized.isEmpty())
        return saveFileFilter();

    // Find the writer that owns this format and list every extension it maps to
    // the same handler (e.g. jpg + jpeg), so the save dialog stays locked to a
    // single format entry while still accepting either spelling.
    for (const auto& codec : m_codecs) {
        if (!codec->canWrite(normalized))
            continue;

        QStringList patterns;
        for (const QString& fmt : codec->writableFormats()) {
            if (normalizeImageExtension(fmt) != normalized)
                continue;
            patterns << QStringLiteral("*.%1").arg(fmt);
            patterns << QStringLiteral("*.%1").arg(fmt.toUpper());
        }

        if (patterns.isEmpty()) {
            patterns << QStringLiteral("*.%1").arg(normalized);
            patterns << QStringLiteral("*.%1").arg(normalized.toUpper());
        }

        patterns = sortedUniquePatterns(patterns);
        return QStringLiteral("%1 (%2)")
            .arg(normalized.toUpper(), patterns.join(QLatin1Char(' ')));
    }

    // No writer claimed it; fall back to a plausible single-format filter.
    return QStringLiteral("%1 (*.%2 *.%3)")
        .arg(normalized.toUpper(), normalized, normalized.toUpper());
}

void registerDefaultImageCodecs(ImageCodecRegistry& registry)
{
    registry.registerCodec(std::make_unique<QtImageCodec>());
    registry.registerCodec(std::make_unique<OpenCvImageCodec>());
}

ImageCodecRegistry& imageCodecRegistry()
{
    static ImageCodecRegistry registry = [] {
        ImageCodecRegistry result;
        registerDefaultImageCodecs(result);
        return result;
    }();
    return registry;
}
