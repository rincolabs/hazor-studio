#include "ColorEngine.hpp"

#include <QSettings>
#include <QtGlobal>

ColorEngine::ColorEngine(QObject* parent)
    : QObject(parent)
{
    m_swatches = {
        Qt::black, Qt::white, Qt::red, Qt::green, Qt::blue, Qt::yellow,
        QColor("#FF00FF"), QColor("#00FFFF"), QColor("#808080"), QColor("#FFA500")
    };
    load();
}

QColor ColorEngine::sanitize(const QColor& color) const
{
    return color.isValid() ? color : QColor(Qt::black);
}

void ColorEngine::setForegroundColor(const QColor& color, bool addToRecent)
{
    QColor c = sanitize(color);
    if (m_foreground == c) {
        if (addToRecent)
            addRecentColor(c);
        return;
    }
    m_foreground = c;
    if (addToRecent)
        addRecentColor(c);
    emit foregroundColorChanged(m_foreground);
}

void ColorEngine::setBackgroundColor(const QColor& color, bool addToRecent)
{
    QColor c = sanitize(color);
    if (m_background == c) return;
    m_background = c;
    if (addToRecent)
        addRecentColor(c);
    emit backgroundColorChanged(m_background);
}

void ColorEngine::swapForegroundBackground()
{
    qSwap(m_foreground, m_background);
    emit foregroundColorChanged(m_foreground);
    emit backgroundColorChanged(m_background);
}

void ColorEngine::resetDefaultColors()
{
    m_foreground = Qt::black;
    m_background = Qt::white;
    emit foregroundColorChanged(m_foreground);
    emit backgroundColorChanged(m_background);
}

void ColorEngine::addRecentColor(const QColor& color)
{
    QColor c = sanitize(color);
    if (!m_recentColors.isEmpty() && m_recentColors.front() == c)
        return;
    m_recentColors.removeAll(c);
    m_recentColors.prepend(c);
    while (m_recentColors.size() > m_recentLimit)
        m_recentColors.removeLast();
    emit recentColorsChanged();
}

void ColorEngine::addSwatchColor(const QColor& color)
{
    QColor c = sanitize(color);
    if (m_swatches.contains(c))
        return;
    m_swatches.push_back(c);
    while (m_swatches.size() > m_swatchLimit)
        m_swatches.removeFirst();
    emit swatchesChanged();
}

void ColorEngine::removeSwatchColor(int index)
{
    if (index < 0 || index >= m_swatches.size()) return;
    m_swatches.remove(index);
    emit swatchesChanged();
}

QString ColorEngine::toHex(const QColor& color, bool includeAlpha) const
{
    return sanitize(color).name(includeAlpha ? QColor::HexArgb : QColor::HexRgb).toUpper();
}

QColor ColorEngine::fromHex(const QString& hex) const
{
    return sanitize(QColor(hex.trimmed()));
}

QColor ColorEngine::fromRgb(int r, int g, int b, int a) const
{
    return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), qBound(0, a, 255));
}

QColor ColorEngine::fromHsv(float h, float s, float v, float a) const
{
    return QColor::fromHsvF(qBound(0.0f, h, 1.0f), qBound(0.0f, s, 1.0f),
                            qBound(0.0f, v, 1.0f), qBound(0.0f, a, 1.0f));
}

QColor ColorEngine::fromHsl(float h, float s, float l, float a) const
{
    return QColor::fromHslF(qBound(0.0f, h, 1.0f), qBound(0.0f, s, 1.0f),
                            qBound(0.0f, l, 1.0f), qBound(0.0f, a, 1.0f));
}

QColor ColorEngine::fromCmyk(float c, float m, float y, float k, float a) const
{
    return QColor::fromCmykF(qBound(0.0f, c, 1.0f), qBound(0.0f, m, 1.0f),
                             qBound(0.0f, y, 1.0f), qBound(0.0f, k, 1.0f),
                             qBound(0.0f, a, 1.0f));
}

void ColorEngine::load()
{
    QSettings settings;
    const QColor fg = QColor(settings.value("color/foreground", QColor(Qt::black)).toString());
    const QColor bg = QColor(settings.value("color/background", QColor(Qt::white)).toString());
    m_foreground = fg.isValid() ? fg : QColor(Qt::black);
    m_background = bg.isValid() ? bg : QColor(Qt::white);

    QVector<QColor> loadedRecent;
    const auto recent = settings.value("color/recent").toStringList();
    for (const QString& s : recent) {
        QColor c(s);
        if (c.isValid()) loadedRecent.push_back(c);
    }
    if (!loadedRecent.isEmpty())
        m_recentColors = loadedRecent;

    QVector<QColor> loadedSwatches;
    const auto swatches = settings.value("color/swatches").toStringList();
    for (const QString& s : swatches) {
        QColor c(s);
        if (c.isValid()) loadedSwatches.push_back(c);
    }
    if (!loadedSwatches.isEmpty())
        m_swatches = loadedSwatches;
}

void ColorEngine::save() const
{
    QSettings settings;
    settings.setValue("color/foreground", m_foreground.name(QColor::HexArgb));
    settings.setValue("color/background", m_background.name(QColor::HexArgb));
    QStringList recent;
    for (const QColor& c : m_recentColors) recent.push_back(c.name(QColor::HexArgb));
    settings.setValue("color/recent", recent);
    QStringList swatches;
    for (const QColor& c : m_swatches) swatches.push_back(c.name(QColor::HexArgb));
    settings.setValue("color/swatches", swatches);
}
