#pragma once

#include <QWidget>
#include <QPixmap>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <QString>

class SplashScreen : public QWidget {
    Q_OBJECT

public:
    explicit SplashScreen(QWidget* parent = nullptr);

    void setMinimumDuration(int ms);
    void setProgress(int percent, const QString& message);
    void finish();
    void setVersion(const QString& version);

signals:
    void finished();
    void firstPainted();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override {}
    void keyPressEvent(QKeyEvent*) override {}

private:
    int m_progress = 0;
    int m_minDurationMs = 1000;
    QElapsedTimer m_elapsed;
    QString m_message;
    QString m_version;
    bool m_finished = false;
    bool m_firstPainted = false;
    QPixmap m_logo;
};
