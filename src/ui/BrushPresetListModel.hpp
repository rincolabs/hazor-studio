#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include "brush/BrushPreset.hpp"

class BrushPresetManager;

// List model backing the brush preset browser. It is a thin, virtualization
// friendly view over BrushPresetManager: one row per (optionally filtered)
// preset, no per-item widgets. Search is supported up front via
// filterByName() / setFilter() so a search field can be added later with no
// model refactor (the interface requirement in the spec).
class BrushPresetListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
    };

    explicit BrushPresetListModel(QObject* parent = nullptr);

    void setPresetManager(BrushPresetManager* manager);
    BrushPresetManager* presetManager() const { return m_manager; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // Preset for a visible row, or nullptr if out of range.
    const BrushPreset* presetAt(int row) const;

    // Row index of a preset by name within the current (filtered) view, or -1.
    int rowForName(const QString& name) const;

    // Search preparation — restrict visible rows to presets whose name contains
    // the (case-insensitive) needle. Empty string shows everything.
    void setFilter(const QString& needle);
    void filterByName(const QString& needle) { setFilter(needle); }
    QString filter() const { return m_filter; }

private slots:
    void rebuild();

private:
    BrushPresetManager* m_manager = nullptr;
    QString m_filter;
    QVector<int> m_visible; // indices into manager->presets()
};
