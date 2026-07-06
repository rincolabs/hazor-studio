#pragma once

#include <QObject>
#include <QImage>
#include <QSize>
#include <QRect>
#include <QVariantMap>
#include <QThreadPool>
#include <atomic>
#include <string>
#include <memory>

class Layer;
namespace core { class Tile; }

namespace processing {

class PreviewRenderer : public QObject {
    Q_OBJECT

public:
    explicit PreviewRenderer(QObject* parent = nullptr);
    ~PreviewRenderer() override;

    // Generate a low-res preview by downscaling then applying filter.
    // targetSize: max dimension for preview (default 512).
    // If layer is large (>targetSize), downscales first for speed.
    void generatePreview(const QImage& source,
                         const std::string& toolName,
                         const QVariantMap& params,
                         QSize targetSize = QSize(512, 512));

    // Cancel any ongoing progressive generation.
    void cancel();
    bool isBusy() const { return m_busy; }

signals:
    void previewReady(const QImage& image);

private:
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_busy{false};
    QThreadPool m_pool;
};

} // namespace processing
