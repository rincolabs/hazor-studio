#pragma once

#include "core/LayerEffect.hpp"

#include <QDialog>
#include <QVector>
#include <vector>

class QFormLayout;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;
class QWidget;
class AppCheckBox;

class LayerStylesDialog : public QDialog {
    Q_OBJECT

public:
    explicit LayerStylesDialog(const std::vector<LayerEffect>& effects,
                               QWidget* parent = nullptr);

    std::vector<LayerEffect> styles() const;

signals:
    void stylesChanged();

private:
    struct StyleDef {
        QString type;
        QString label;
    };

    void buildUi();
    void rebuildProperties(int row);
    void updateEffectRowStyles();
    void emitPreview();
    int indexForType(const QString& type) const;
    LayerEffect& effectAt(int row);
    const LayerEffect& effectAt(int row) const;

    void addColorRow(QFormLayout* form, int row, const QString& label,
                     const QString& key);
    void addIntRow(QFormLayout* form, int row, const QString& label,
                   const QString& key, int min, int max, int step = 1);
    void addDoubleRow(QFormLayout* form, int row, const QString& label,
                      const QString& key, double min, double max,
                      double step = 0.05, int decimals = 2);
    void addBlendRow(QFormLayout* form, int row);
    void addPositionRow(QFormLayout* form, int row);

    QVector<StyleDef> m_defs;
    QVector<LayerEffect> m_effects;
    QListWidget* m_list = nullptr;
    QVector<QWidget*> m_effectRowWidgets;
    QVector<AppCheckBox*> m_effectChecks;
    QWidget* m_propertyHost = nullptr;
    QVBoxLayout* m_propertyLayout = nullptr;
    bool m_updating = false;
};
