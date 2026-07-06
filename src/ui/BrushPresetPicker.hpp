#pragma once

#include <QToolButton>
#include "brush/BrushPreset.hpp"
#include "brush/BrushPresetManager.hpp"

class QMenu;
class QLabel;

class BrushPresetPicker : public QToolButton {
    Q_OBJECT

public:
    explicit BrushPresetPicker(QWidget* parent = nullptr);

    void setPresetManager(BrushPresetManager* manager);
    void setCurrentPreset(const BrushPreset& preset);
    void rebuildGrid();
    QPixmap renderThumbnail(const BrushPreset& preset);

    // When enabled, clicking the picker no longer opens its own internal
    // selection grid; instead it emits panelRequested() so the host can open
    // (or focus) the dedicated Brush Panel. The picker keeps rendering the
    // active preset thumbnail and staying in sync. See feature-brush-picker.
    void setOpensExternalPanel(bool on);

signals:
    void presetSelected(const BrushPreset& preset);
    void savePresetRequested();
    void panelRequested();
    void importBrushesRequested();

private:
    void rebuildExternalMenu();
    QWidget* buildThumbnailWidget(const BrushPreset& preset);

    BrushPresetManager* m_manager = nullptr;
    QMenu* m_popup = nullptr;
    QString m_currentPresetName;
    bool m_loading = false;
    bool m_opensExternalPanel = false;
};
