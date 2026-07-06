#pragma once

#include "GradientTypes.hpp"

#include <QObject>
#include <QVector>

class GradientPresetManager : public QObject {
    Q_OBJECT

public:
    explicit GradientPresetManager(QObject* parent = nullptr);

    const QVector<GradientDefinition>& presets() const { return m_presets; }
    GradientDefinition currentGradient() const;
    void setCurrentGradient(const GradientDefinition& definition);

    void setSessionColors(const QColor& foreground, const QColor& background);
    bool loadPresets();
    bool savePresets() const;
    bool loadFromFile(const QString& path);
    bool saveToFile(const QString& path) const;
    void resetToDefaults();
    void addPreset(const GradientDefinition& definition);
    void removePreset(int index);

    static QVector<GradientDefinition> defaultPresets(
        const QColor& foreground = Qt::black,
        const QColor& background = Qt::white);

signals:
    void presetsChanged();
    void currentGradientChanged(const GradientDefinition& definition);

private:
    QString presetFilePath() const;

    QVector<GradientDefinition> m_presets;
    GradientDefinition m_current;
    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;
};

