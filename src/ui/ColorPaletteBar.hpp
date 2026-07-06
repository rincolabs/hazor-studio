#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QScrollArea;
class AppComboBox;

// Fixed horizontal palette strip below the canvas. It is a live view of one
// SwatchManager collection, chosen through the combo on the left; selecting a
// swatch sets the foreground/background color. Edits made anywhere (the
// Swatches panel, etc.) propagate here automatically through SwatchManager
// signals, and vice-versa.
class ColorPaletteBar : public QWidget {
    Q_OBJECT

public:
    explicit ColorPaletteBar(QWidget* parent = nullptr);

signals:
    void foregroundColorSelected(const QColor& color);
    void backgroundColorSelected(const QColor& color);

private slots:
    void refreshCollections();
    void onCollectionChosen(int index);
    void onCollectionColorsChanged(const QString& collectionId);

private:
    void rebuildButtons();
    void applyTheme();
    void setCurrentCollection(const QString& id, bool persist = true);
    static QString swatchStyle(const QColor& color);
    static QString tooltipForColor(const QColor& color);

    AppComboBox* m_combo = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_strip = nullptr;
    QHBoxLayout* m_stripLayout = nullptr;
    QString m_currentCollectionId;
    bool m_updatingCombo = false;
};
