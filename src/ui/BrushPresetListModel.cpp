#include "BrushPresetListModel.hpp"
#include "brush/BrushPresetManager.hpp"

BrushPresetListModel::BrushPresetListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void BrushPresetListModel::setPresetManager(BrushPresetManager* manager)
{
    if (m_manager == manager)
        return;
    if (m_manager)
        m_manager->disconnect(this);
    m_manager = manager;
    if (m_manager)
        connect(m_manager, &BrushPresetManager::presetsChanged,
                this, &BrushPresetListModel::rebuild);
    rebuild();
}

void BrushPresetListModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    if (m_manager) {
        const auto& presets = m_manager->presets();
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            if (m_filter.isEmpty()
                || presets[i].name.contains(m_filter, Qt::CaseInsensitive)) {
                m_visible.push_back(i);
            }
        }
    }
    endResetModel();
}

int BrushPresetListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visible.size();
}

QVariant BrushPresetListModel::data(const QModelIndex& index, int role) const
{
    const BrushPreset* p = presetAt(index.row());
    if (!p)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return p->name;
    case Qt::ToolTipRole:
        return QStringLiteral("%1 (%2 px)").arg(p->name).arg(static_cast<int>(p->settings.size));
    default:
        return {};
    }
}

const BrushPreset* BrushPresetListModel::presetAt(int row) const
{
    if (!m_manager || row < 0 || row >= m_visible.size())
        return nullptr;
    const auto& presets = m_manager->presets();
    const int idx = m_visible[row];
    if (idx < 0 || idx >= static_cast<int>(presets.size()))
        return nullptr;
    return &presets[idx];
}

int BrushPresetListModel::rowForName(const QString& name) const
{
    for (int row = 0; row < m_visible.size(); ++row) {
        const BrushPreset* p = presetAt(row);
        if (p && p->name == name)
            return row;
    }
    return -1;
}

void BrushPresetListModel::setFilter(const QString& needle)
{
    const QString trimmed = needle.trimmed();
    if (trimmed == m_filter)
        return;
    m_filter = trimmed;
    rebuild();
}
