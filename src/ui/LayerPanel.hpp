#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <QHash>
#include <QImage>
#include <QFutureWatcher>
#include <cstdint>
#include <memory>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QComboBox;
class QButtonGroup;

class ScrubbableValueInput;
class ImageController;
class LayerItemDelegate;

class LayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void setController(ImageController* ctrl);
    void refresh();
    void focusLayerList();

signals:
    void activeLayerChanged(int index);
    void addLayerRequested();
    void removeLayerRequested(int index);
    void grayscaleViewRequested(bool enabled);
    void layerStylesRequested(int index);
    void aiUpscaleRequested(int index);
    // Solid Color fill layer: the "New Adjustment Layer ▸ Solid Color" entry was
    // chosen (MainWindow runs the picker-first creation flow).
    void solidColorRequested();
    // The colour thumbnail of a Solid Color fill layer was double-clicked
    // (MainWindow selects it and opens the colour picker).
    void solidColorEditRequested(int index);

private slots:
    void onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onVisibilityToggled(int layerIndex);
    void onLockToggled(int layerIndex, int lockFlag);
    void onBlendModeChanged(int index);
    void onOpacityChanged(int value);
    void onLockButtonClicked(int id);

private:
    enum class ActiveTarget {
        None,
        Pixels,
        Mask,
        Adjustment,
        Effects
    };

    void updateTopBarFromActiveLayer();
    void applyTheme();
    QVector<QTreeWidgetItem*> visibleLayerRows() const;
    int currentVisibleRow(const QVector<QTreeWidgetItem*>& rows) const;
    bool setCurrentVisibleRow(int row, bool extendSelection);
    bool moveCurrentVisibleRow(int delta, bool extendSelection);
    bool syncTreeSelectionToDocument();
    void updateActiveTargetFromCurrent();
    bool eventFilter(QObject* obj, QEvent* event) override;

    // Off-thread thumbnail baking. A Single-Layer-Mode (clipped-adjustment) or
    // styled layer thumbnail goes through computeEffectedImage() — a full-res CPU
    // bake. Doing it synchronously on settle froze the UI; instead LayerPanel
    // snapshots the tree (COW) and bakes the dirty effected thumbnails on a worker
    // thread, applying the results when ready. The delegate never bakes these.
    struct ThumbBatch {
        quint64 token = 0;
        QHash<int, QImage> thumbs; // flatIndex -> thumbnail
    };
    void scheduleThumbnailBuild();
    void onThumbnailsReady();

    ImageController* m_controller = nullptr;

    QComboBox* m_kindCombo = nullptr;
    QComboBox* m_blendCombo = nullptr;
    ScrubbableValueInput* m_opacitySlider = nullptr;
    QButtonGroup* m_lockGroup = nullptr;

    QWidget* m_topBar = nullptr;
    QWidget* m_blendRow = nullptr;
    QWidget* m_lockRow = nullptr;
    QWidget* m_actionBar = nullptr;
    QTreeWidget* m_tree = nullptr;
    LayerItemDelegate* m_delegate = nullptr;

    QPushButton* m_addBtn = nullptr;
    QPushButton* m_groupBtn = nullptr;
    QPushButton* m_adjustBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_dupBtn = nullptr;
    QPushButton* m_maskBtn = nullptr;

    bool m_updating = false;
    // While an adjustment slider/curve is being dragged, skip the per-frame
    // refresh() (tree rebuild + thumbnail recompute). The layer thumbnail of a
    // Single-Layer-Mode layer goes through computeEffectedImage() — a full-res
    // CPU bake — and rebuilding it every live frame on the GUI thread stalls the
    // canvas drag. The structure can't change mid-drag, so the panel just holds
    // its last frame; one refresh() runs when the live edit ends.
    bool m_adjustmentLiveEdit = false;
    ActiveTarget m_activeTarget = ActiveTarget::None;

    QFutureWatcher<std::shared_ptr<ThumbBatch>> m_thumbWatcher;
    quint64 m_thumbToken = 0;
    bool m_thumbRescheduleNeeded = false;
};
