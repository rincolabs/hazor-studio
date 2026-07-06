#pragma once

#include <QObject>
#include <QColor>
#include <QVector>
#include <QString>

class ColorEngine : public QObject {
    Q_OBJECT

public:
    explicit ColorEngine(QObject* parent = nullptr);

    QColor foregroundColor() const { return m_foreground; }
    QColor backgroundColor() const { return m_background; }
    QVector<QColor> recentColors() const { return m_recentColors; }
    QVector<QColor> swatches() const { return m_swatches; }

    void setForegroundColor(const QColor& color, bool addToRecent = true);
    void setBackgroundColor(const QColor& color, bool addToRecent = false);
    void swapForegroundBackground();
    void resetDefaultColors();
    void addRecentColor(const QColor& color);
    void addSwatchColor(const QColor& color);
    void removeSwatchColor(int index);

    QString toHex(const QColor& color, bool includeAlpha = true) const;
    QColor fromHex(const QString& hex) const;
    QColor fromRgb(int r, int g, int b, int a = 255) const;
    QColor fromHsv(float h, float s, float v, float a = 1.0f) const;
    QColor fromHsl(float h, float s, float l, float a = 1.0f) const;
    QColor fromCmyk(float c, float m, float y, float k, float a = 1.0f) const;

    void load();
    void save() const;

signals:
    void foregroundColorChanged(const QColor& color);
    void backgroundColorChanged(const QColor& color);
    void recentColorsChanged();
    void swatchesChanged();

private:
    QColor sanitize(const QColor& color) const;

    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;
    QVector<QColor> m_recentColors;
    QVector<QColor> m_swatches;
    int m_recentLimit = 24;
    int m_swatchLimit = 48;
};
