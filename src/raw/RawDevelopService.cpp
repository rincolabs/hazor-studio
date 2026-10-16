#include "raw/RawDevelopService.hpp"

#include "raw/RawEngine.hpp"
#include "raw/RawSession.hpp"
#include "core/LayerTreeNode.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <random>

RawDevelopService::Entry::~Entry()
{
    // Mirrors AsyncStyleBaker's teardown contract: never destroy a watcher
    // whose future is still running (release() only moves an Entry out of
    // m_entries once idle — see release() below).
    if (watcher.isRunning())
        watcher.waitForFinished();
    if (!tempPath.isEmpty())
        QFile::remove(tempPath);
}

RawDevelopService::RawDevelopService(QObject* parent) : QObject(parent) {}
RawDevelopService::~RawDevelopService() = default;

RawDevelopService::Entry& RawDevelopService::entryFor(LayerTreeNode* node)
{
    auto it = m_entries.find(node);
    if (it != m_entries.end())
        return *it->second;

    auto entry = std::make_unique<Entry>();
    QObject::connect(&entry->watcher, &QFutureWatcher<std::shared_ptr<Task>>::finished,
                     this, [this, node]() { onFinished(node); });
    auto [insertedIt, inserted] = m_entries.emplace(node, std::move(entry));
    return *insertedIt->second;
}

QString RawDevelopService::materializeEmbedded(const RawSource& source)
{
    const QString suffix = QFileInfo(source.path).suffix();
    const QString name = QStringLiteral("hazor-raw-%1%2")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
        .arg(suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);
    const QString path = QDir(QDir::tempPath()).filePath(name);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    const qint64 written = file.write(source.bytes);
    file.close();
    if (written != source.bytes.size()) {
        QFile::remove(path);
        return {};
    }
    return path;
}

// The single named point where a RawImageBuffer's 16-bit/float pixels become
// the compositor's 8-bit Format_RGBA8888 (Fase 4.2). `buffer` is never
// mutated or truncated upstream of this call — see RawDevelopService::Task::
// fullBuffer, which keeps the un-truncated buffer around for a future
// higher-depth path. Dithering is a simple per-pixel random offset: good
// enough to break up banding in flat skies, not a contouring-free guarantee.
QImage RawDevelopService::convertToRgba8888(const RawImageBuffer& buffer, bool dither)
{
    if (buffer.isEmpty())
        return {};

    QImage image(buffer.width, buffer.height, QImage::Format_RGBA8888);

    if (buffer.format == RawPixelFormat::RGB8) {
        for (int y = 0; y < buffer.height; ++y) {
            uchar* row = image.scanLine(y);
            const std::uint8_t* src = &buffer.pixelsU8[std::size_t(y) * buffer.width * 3];
            for (int x = 0; x < buffer.width; ++x) {
                row[x * 4 + 0] = src[x * 3 + 0];
                row[x * 4 + 1] = src[x * 3 + 1];
                row[x * 4 + 2] = src[x * 3 + 2];
                row[x * 4 + 3] = 255;
            }
        }
        return image;
    }

    std::mt19937 rng(0); // Deterministic: same input always dithers the same way.
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    const float toByte = 255.0f / 65535.0f;

    for (int y = 0; y < buffer.height; ++y) {
        uchar* row = image.scanLine(y);
        const float* src = &buffer.pixelsFloat[std::size_t(y) * buffer.width * 3];
        for (int x = 0; x < buffer.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                float v = src[x * 3 + c] * toByte;
                if (dither)
                    v += noise(rng);
                row[x * 4 + c] = static_cast<uchar>(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}

bool RawDevelopService::requestDevelop(LayerTreeNode* node,
                                       std::shared_ptr<const RawSource> source,
                                       RawStorageMode mode,
                                       const RawDevelopParams& params)
{
    if (!node || !source)
        return false;

    Entry& e = entryFor(node);
    if (e.dispatched)
        return false; // caller re-requests once developFinished() fires

    e.dispatched = true;

    // Captured by value: the task must never touch the live node, the
    // Document or `this` — only `source` (immutable, shared), a copy of
    // `params` and whatever the entry had cached so far.
    RawSession* cachedSession = e.session.get();
    QString cachedTempPath = e.tempPath;
    RawDevelopParams paramsCopy = params;

    auto future = QtConcurrent::run(
        [source, mode, cachedSession, cachedTempPath, paramsCopy]() -> std::shared_ptr<Task> {
            auto task = std::make_shared<Task>();

            RawSession* session = cachedSession;
            if (!session) {
                QString path = source->path;
                if (mode == RawStorageMode::Embedded) {
                    QString tempPath = cachedTempPath;
                    if (tempPath.isEmpty())
                        tempPath = RawDevelopService::materializeEmbedded(*source);
                    if (tempPath.isEmpty()) {
                        task->result.errorMessage =
                            QStringLiteral("Could not materialize the embedded RAW data.");
                        return task;
                    }
                    path = tempPath;
                    task->openedTempPath = tempPath;
                }
                if (path.isEmpty()) {
                    task->result.errorMessage = QStringLiteral("RAW source has no readable path.");
                    return task;
                }

                std::string errorMessage;
                task->openedSession = RawEngine::openFile(path.toStdString(), &errorMessage);
                if (!task->openedSession) {
                    task->result.errorMessage = QString::fromStdString(errorMessage);
                    return task;
                }
                session = task->openedSession.get();
            }

            RawImageBuffer buffer = session->renderFull(paramsCopy, nullptr, nullptr);
            if (buffer.isEmpty()) {
                task->result.errorMessage = QStringLiteral("RAW develop failed.");
                return task;
            }

            task->fullBuffer = buffer;
            task->result.ok = true;
            task->result.image = RawDevelopService::convertToRgba8888(buffer, /*dither=*/false);
            task->result.developedSize = QSize(buffer.width, buffer.height);
            task->result.geometry = buffer.geometry;
            return task;
        });

    e.watcher.setFuture(future);
    return true;
}

void RawDevelopService::onFinished(LayerTreeNode* node)
{
    auto it = m_entries.find(node);
    if (it == m_entries.end())
        return;
    Entry& e = *it->second;
    std::shared_ptr<Task> task = e.watcher.result();
    e.dispatched = false;

    if (!task) {
        RawDevelopResult failure;
        failure.errorMessage = QStringLiteral("RAW develop was cancelled.");
        emit developFinished(node, failure);
        return;
    }

    if (task->openedSession) {
        e.session = std::move(task->openedSession);
        e.tempPath = task->openedTempPath;
    }
    if (task->result.ok)
        e.lastFullBuffer = std::move(task->fullBuffer);

    emit developFinished(node, task->result);
}

bool RawDevelopService::isBusy(const LayerTreeNode* node) const
{
    auto it = m_entries.find(node);
    return it != m_entries.end() && it->second->dispatched;
}

void RawDevelopService::release(const LayerTreeNode* node)
{
    auto it = m_entries.find(node);
    if (it == m_entries.end())
        return;
    // Entry::~Entry() waits for a running watcher before deleting the temp
    // file — safe to erase unconditionally, unlike StyleBakePool (which
    // defers deletion of a still-running baker to avoid blocking the UI
    // thread). A RAW re-reveal is already a deliberate, infrequent action,
    // not a per-frame one, so release() blocking briefly here is acceptable.
    m_entries.erase(it);
}
