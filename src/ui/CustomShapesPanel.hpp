#pragma once

#include "shape/ShapeIconLibrary.hpp"

#include <QWidget>

class QLabel;
class QListView;
class QLineEdit;
class QProgressBar;
class QTimer;
class ShapeIconDelegate;
class ShapeIconListModel;

class CustomShapesPanel : public QWidget {
    Q_OBJECT

public:
    explicit CustomShapesPanel(QWidget* parent = nullptr);

    void reloadIcons();
    void setIcons(QList<ShapeIconInfo> icons, int layoutWidthHint = 0);
    void setSelectedIcon(const QString& id);

signals:
    void iconSelected(const ShapeIconInfo& icon);
    void iconsPrepared();

private:
    void applyTheme();
    void scheduleFilter();
    void startAsyncFilter();
    void showSearchLoading();
    void hideSearchLoading();
    void applySelectionToView();

    ShapeIconLibrary m_library;
    QString m_selectedId;
    int m_searchRevision = 0;
    bool m_initialPreparationPending = false;

    QLineEdit* m_search = nullptr;
    QListView* m_view = nullptr;
    ShapeIconListModel* m_model = nullptr;
    ShapeIconDelegate* m_delegate = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QProgressBar* m_searchLoadingBar = nullptr;
    QTimer* m_searchDebounceTimer = nullptr;
    QTimer* m_searchLoadingTimer = nullptr;
};
