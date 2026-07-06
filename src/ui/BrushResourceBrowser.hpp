#pragma once

#include <QWidget>
#include <QImage>
#include <QString>
#include <QVector>

class QLineEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;

// Which kind of brush resource this browser lists. The visual layout is shared;
// the mode only toggles the large side preview and the wording.
enum class BrushResourceBrowserMode {
    BrushTexture,   // grid + large side preview + search + import/remove
    BrushTip        // grid + search + import/remove (no side preview, no tag)
};

// Reusable resource grid for the Brush Editor (textures and tips). It owns only
// the visual list/search/import/preview chrome; the host feeds it items and reacts
// to the selected/import/remove signals. Selection state is mirrored in the grid
// and (BrushTexture mode) a real-size scrollable preview with a "name (w x h)"
// label, matching the original Texture page.
class BrushResourceBrowser : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString id;
        QString name;
        QImage image;   // thumbnail source (also shown in the side preview)
    };

    explicit BrushResourceBrowser(BrushResourceBrowserMode mode,
                                  QWidget* parent = nullptr);

    // Replace the listed resources; the current selection is preserved if it is
    // still present. Does not emit.
    void setItems(const QVector<Item>& items);

    // Select an id without emitting (no-op if absent).
    void setSelectedId(const QString& id);
    QString selectedId() const { return m_selectedId; }

signals:
    void selected(const QString& id);
    void importRequested();
    void removeRequested(const QString& id);

private:
    void rebuildGrid();
    void updatePreview();
    const Item* itemForId(const QString& id) const;

    BrushResourceBrowserMode m_mode;
    QVector<Item> m_items;
    QString m_selectedId;
    bool m_loading = false;

    QLabel* m_nameLabel = nullptr;        // BrushTexture only
    QLineEdit* m_search = nullptr;
    QListWidget* m_grid = nullptr;
    QScrollArea* m_previewScroll = nullptr;  // BrushTexture only
    QLabel* m_preview = nullptr;             // BrushTexture only
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
};
