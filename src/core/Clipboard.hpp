#pragma once

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <memory>
#include "LayerTreeNode.hpp"

enum class ClipboardType {
    None,
    Pixels,
    Layer,
    Group
};

struct ClipboardData {
    ClipboardType type = ClipboardType::None;

    QImage pixels;
    QPointF docPosition;
    QSize sourceDocSize;

    std::unique_ptr<LayerTreeNode> node;

    QString name;
};

// Custom MIME format written to the SYSTEM clipboard alongside the raster on
// every in-app copy. Its payload is the token of the internal ClipboardData it
// mirrors, so paste can tell whether the system clipboard still points at our
// copy (→ rich internal paste: layer/transform/mask metadata) or was replaced
// by another application (→ the internal cache is stale and must be ignored).
inline constexpr const char* kInternalClipboardMime =
    "application/x-hazor-clipboard";

class ClipboardManager {
public:
    static ClipboardManager& instance();

    void setData(ClipboardData data, QString token = QString());
    const ClipboardData& data() const;
    bool hasData() const;
    void clear();

    // Token of the internal copy last published to the system clipboard. Empty
    // when the internal data was never published (or was cleared).
    const QString& token() const;

private:
    ClipboardManager() = default;
    ~ClipboardManager() = default;
    ClipboardManager(const ClipboardManager&) = delete;
    ClipboardManager& operator=(const ClipboardManager&) = delete;

    ClipboardData m_data;
    QString m_token;
};
