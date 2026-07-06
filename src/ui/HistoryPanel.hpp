#pragma once

#include <QWidget>
#include <QStringList>

class QListWidget;
class QPushButton;
class QLabel;

class HistoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    void refresh(const QStringList& stateNames, int currentIndex);
    void clear();

signals:
    void jumpRequested(int targetIndex);
    void undoRequested();
    void redoRequested();
    void clearRequested();

private:
    void setupUi();
    void applyTheme();
    void applyItemStyles();
    static QString formatDisplayName(const QString& rawName);

    QListWidget* m_listWidget = nullptr;
    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_redoBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_countLabel = nullptr;
    int m_currentIndex = -1;
};
