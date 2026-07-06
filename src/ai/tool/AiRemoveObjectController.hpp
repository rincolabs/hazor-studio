#pragma once

#include "ai/AiRemoveTypes.hpp"
#include "ai/inpaint/AiInpaintTypes.hpp"

#include <QObject>
#include <QStringList>

class ImageController;
class CanvasView;

class AiRemoveObjectController : public QObject {
    Q_OBJECT
public:
    AiRemoveObjectController(ImageController* controller, CanvasView* canvas,
                             QObject* parent = nullptr);

    void setOptions(const AiRemoveOptions& options) { m_options = options; }
    AiRemoveOptions options() const { return m_options; }

    QStringList installedInpaintingModels() const;
    QString modelDisplayName(const QString& id) const;
    QString resolvedModelId() const;
    bool isReady(QString* reason = nullptr) const;

    void removeObject(const QImage& lassoMask);
    void cancel();
    void notifyDocumentClosing();

signals:
    void runningChanged(bool running);
    void progressChanged(float progress, QString message);
    void statusChanged(const QString& message);
    void error(const QString& message);
    void finished();
    void availabilityChanged();

private:
    quint64 currentDocumentId() const;
    bool buildSnapshot(QImage& image, quint64& revision) const;
    void handleStatus(quint64 jobId, const QString& status);
    void handleResult(const AiInpaintResult& result);
    void setRunning(bool running);

    ImageController* m_controller = nullptr;
    CanvasView* m_canvas = nullptr;
    AiRemoveOptions m_options;
    quint64 m_pendingJobId = 0;
    quint64 m_sourceRevision = 0;
    bool m_running = false;
};
