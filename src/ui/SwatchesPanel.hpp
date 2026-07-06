#pragma once

#include <QListWidget>
#include <QString>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QListWidgetItem;
class QPushButton;
class QLabel;
class QDropEvent;
class QResizeEvent;
class ColorEngine;

// Color grid for a single collection. A thin QListWidget subclass that surfaces
// drag-reorder as a signal and sizes itself to its content: its height is the
// number of swatch rows plus one spare row (so there's always room to drop/add
// a color), capped at a row count the panel supplies based on available space.
class SwatchGrid : public QListWidget {
    Q_OBJECT
public:
    explicit SwatchGrid(QWidget* parent = nullptr);

    void setMaxContentRows(int rows);    // cap fed by the panel (available space)
    void refreshContentHeight();         // recompute fixed height from count + width

signals:
    void reordered();

protected:
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int m_maxContentRows = 6;
};

// Swatches panel: a preset tree (folders + collections) on top,
// a responsive color grid for the selected collection below, and a bottom bar
// (New Palette / New Folder / Delete). Backed entirely by SwatchManager — the
// panel only renders and dispatches; it never owns color data.
class SwatchesPanel : public QWidget {
    Q_OBJECT

public:
    explicit SwatchesPanel(ColorEngine* engine, QWidget* parent = nullptr);

private slots:
    void rebuildTree();
    void onTreeSelectionChanged();
    void onTreeItemExpanded(QTreeWidgetItem* item);
    void onTreeItemCollapsed(QTreeWidgetItem* item);
    void onTreeContextMenu(const QPoint& pos);
    void refreshColors();
    void onColorsChanged(const QString& collectionId);
    void onGridContextMenu(const QPoint& pos);
    void onColorClicked(QListWidgetItem* item);
    void onColorDoubleClicked(QListWidgetItem* item);
    void onGridReordered();
    void onNewCollection();
    void onNewFolder();
    void onDeleteSelected();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyTheme();
    void updateGridCap();   // tell the grid how many rows fit before the tree min
    void addTreeNodesRecursive(const class SwatchNode* node, QTreeWidgetItem* parentItem);
    QString selectedNodeId() const;
    QString parentForNewNode() const;       // where New Palette/Folder should land
    void selectNodeById(const QString& id);
    void editColorItem(QListWidgetItem* item);
    void renameColorItem(QListWidgetItem* item);
    void addColorToCurrent(const QColor& color, const QString& name = QString());

    ColorEngine* m_engine = nullptr;
    QTreeWidget* m_tree = nullptr;
    SwatchGrid*  m_grid = nullptr;
    QLabel*      m_emptyHint = nullptr;
    QWidget*     m_actionBar = nullptr;
    QPushButton* m_newCollectionBtn = nullptr;
    QPushButton* m_newFolderBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    QString m_currentCollectionId;   // collection whose colors the grid shows
    QString m_arrowDownPath;         // runtime-rotated arrow for open branches
    bool m_buildingTree = false;
};
