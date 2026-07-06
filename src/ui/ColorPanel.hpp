#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

class QPushButton;

class ColorPanel : public QWidget {
    Q_OBJECT

public:
    explicit ColorPanel(QWidget* parent = nullptr);

    void setForegroundColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setRecentColors(const QVector<QColor>& colors);

signals:
    void foregroundColorRequested();
    void backgroundColorRequested();
    void foregroundColorChanged(const QColor& color);
    void backgroundColorChanged(const QColor& color);
    void swapColorsRequested();
    void resetColorsRequested();

private:
    void applyTheme();
    static QString colorButtonStyle(const QColor& color, bool active = false);
    QPushButton* makeColorButton(const QSize& size, const QString& tooltip);

    QPushButton* m_foregroundBtn = nullptr;
    QPushButton* m_backgroundBtn = nullptr;
    QPushButton* m_swapBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QVector<QPushButton*> m_recentButtons;
    QVector<QColor> m_recentColors;
    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;
};
