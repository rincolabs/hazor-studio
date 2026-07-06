#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QHBoxLayout;

class TitleBar : public QWidget {
    Q_OBJECT

public:
    // Shared height of the custom title bar strip. Also used by MainWindow to
    // reserve the top content margin so the title bar overlay doesn't cover the
    // toolbar/docks. Tall enough to host the main menu bar comfortably.
    static constexpr int kBarHeight = 30;

    explicit TitleBar(QWidget* parent = nullptr);

    // Hosts the application's main menu bar inside the title bar, placed between
    // the app icon and the window controls. The menu keeps its own actions,
    // submenus and shortcuts — only its visual location changes. Takes ownership
    // by reparenting.
    void setMenuBar(QWidget* menuBar);

    void updateMaximizeButton(bool maximized);

signals:
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    // Makes the empty area of the hosted menu bar drag/maximize the window,
    // since the menu fills the bar and would otherwise swallow those events.
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    QHBoxLayout* m_layout = nullptr;
    QLabel* m_iconLabel = nullptr;
    QWidget* m_menuBar = nullptr;
    QPushButton* m_minBtn = nullptr;
    QPushButton* m_maxBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
};
