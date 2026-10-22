#pragma once

#include <QImage>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <cstdint>

class QDialog;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class CanvasView;
class ChannelItemDelegate;
class ImageController;

// The Channels panel.
//
// It shows the ACTIVE document's channels and nothing of its own: the rows are
// derived from Document::channels and Document::channelState on every refresh,
// so switching tabs shows the new document's channels with its own visibility
// and edit-target state, and closing one leaves no dangling buffer behind.
//
// Every command it offers runs through ImageController, which is what gives
// them undo/redo and keeps the panel from being a second channel system.
class ChannelsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ChannelsPanel(QWidget* parent = nullptr);

    void setController(ImageController* controller);
    // The canvas supplies the document composite the RGB and R/G/B thumbnails
    // are made from. Without it those rows simply have no thumbnail; the extra
    // channels, which own their pixels, are unaffected.
    void setCanvas(CanvasView* canvas);
    void refresh();

signals:
    // Split Channels / Merge Channels need to create and consume documents
    // (tabs), which only MainWindow can do.
    void splitChannelsRequested();
    void mergeChannelsRequested();
    // Duplicate Channel to another document, likewise.
    void duplicateChannelRequested(int channelIndex);
    // Select ▸ Save/Load Selection, so the panel's buttons and the menu items
    // open the same dialogs.
    void saveSelectionDialogRequested();
    void loadSelectionDialogRequested();

private:
    // What a row stands for. Only Extra rows map to a Document::channels entry;
    // Composite and Component are views of the layer stack, and QuickMask is
    // the live selection.
    enum class RowKind { Composite = 0, Component = 1, Extra = 2, QuickMask = 3 };

    void buildUi();
    void applyTheme();
    void updateActionBar();
    void rebuildRows();

    QTreeWidgetItem* itemAt(int row) const;
    RowKind rowKind(const QTreeWidgetItem* item) const;
    int rowPayload(const QTreeWidgetItem* item) const;
    int selectedChannelIndex() const;   // -1 unless an Extra row is current

    void onRowClicked(QTreeWidgetItem* item, Qt::KeyboardModifiers mods);
    void onEyeToggled(QTreeWidgetItem* item);
    void onThumbnailActivated(QTreeWidgetItem* item, Qt::KeyboardModifiers mods);
    void onThumbnailDoubleClicked(QTreeWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    void showPanelOptions(const QPoint& globalPos);

    // Commands
    void newAlphaChannel(bool withDialog);
    void newSpotChannel();
    void duplicateCurrentChannel();
    void deleteCurrentChannel();
    void openChannelOptions(int channelIndex);
    // Re-reads the channel at `channelIndex` into `out`, false when it is gone.
    // The channel dialogs are modeless, so what they edit can change under them.
    bool captureChannelSettings(int channelIndex, struct ChannelSettings& out) const;
    void renameCurrentChannel();
    void mergeCurrentSpotChannel();
    void loadCurrentAsSelection(Qt::KeyboardModifiers mods);
    void loadCurrentAsSelectionFor(QTreeWidgetItem* item,
                                   Qt::KeyboardModifiers mods);
    void saveSelectionAsChannel();

    bool eventFilter(QObject* watched, QEvent* event) override;

    // Thumbnail caches. Rebuilt only when their source actually changed: the
    // panel refreshes on imageChanged(), which fires far more often than the
    // pixels behind these do, and a full-resolution rescale per refresh was
    // the one expensive thing in here.
    QImage m_compositeThumb;          // RGB row, downscaled once per composite
    // QImage::cacheKey() of the composite the two caches below were built
    // from — see rebuildRows() for why this is not a generation counter.
    qint64 m_compositeThumbKey = 0;
    int m_compositeThumbSize = -1;
    QImage m_componentThumbs[3];
    // Signature of the last rebuild: rows are only recreated when something
    // they display changed.
    QString m_rowSignature;

    // QPointer, not a raw pointer: the controller belongs to a document tab and
    // dies with it, while this panel outlives every tab. setController() has to
    // disconnect from whatever it last held, so a stale pointer here is not
    // merely stale — it is a disconnect() through freed memory. The guard is
    // structural; MainWindow::closeTabAt detaching the panel is the first line
    // of defence, this is what makes forgetting it harmless.
    QPointer<ImageController> m_controller;
    // The open channel dialog (new / options, alpha or spot). Modeless, because
    // it hosts an ink/overlay colour picker whose eyedropper samples the canvas;
    // one at a time, since the panel's buttons stay live while it is open.
    QPointer<QDialog> m_channelDlg;
    QPointer<CanvasView> m_canvas;   // same reasoning as m_controller
    QTreeWidget* m_tree = nullptr;
    ChannelItemDelegate* m_delegate = nullptr;

    QWidget* m_actionBar = nullptr;
    QPushButton* m_loadSelectionBtn = nullptr;
    QPushButton* m_saveSelectionBtn = nullptr;
    QPushButton* m_newChannelBtn = nullptr;
    QPushButton* m_deleteChannelBtn = nullptr;
    QPushButton* m_optionsBtn = nullptr;

    bool m_updating = false;
};
