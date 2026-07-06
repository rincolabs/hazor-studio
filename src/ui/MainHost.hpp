#pragma once

#include <QWidget>
#include <QString>

class TitleBar;
class MainWindow;

class MainHost : public QWidget {
    Q_OBJECT

public:
    explicit MainHost(QWidget* parent = nullptr);

protected:
    bool event(QEvent* e) override;

private slots:
    void onMinimize();
    void onMaximize();
    void onClose();

private:
    void onWindowStateChanged(Qt::WindowStates state);

    TitleBar* m_titleBar = nullptr;
    MainWindow* m_mainWindow = nullptr;
    bool m_useNativeTitleBar = false;
};
