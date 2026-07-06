#pragma once

#include <QWidget>
#include <QIcon>
#include <QString>
#include <QVector>

class QToolButton;
class ColorEngine;
class ColorSwapWidget;
class QButtonGroup;
class QMenu;

class ToolsPanel : public QWidget {
    Q_OBJECT

public:
    // Skew is a UI-only sub-tool of the Move button (flyout): it reuses the Move
    // canvas tool and only swaps the options-bar page to expose Distort/Perspective.
    enum ToolId { Move = 0, Brush = 1, Eraser = 2, Select = 3, Zoom = 4, Hand = 5, Text = 6, Crop = 7, FillBucket = 8, Eyedropper = 9, Shape = 10, CloneStamp = 11, Gradient = 12, Skew = 13, AiSelect = 14, AiRemove = 15 };

    static constexpr int kPanelWidth = 46;

    explicit ToolsPanel(ColorEngine* colorEngine, QWidget* parent = nullptr);

    void setActiveTool(int tool);
    void setActiveSubTool(int tool, int subTool);
    int activeSubToolForTool(int tool) const;
    QVector<int> activeSubToolsForTool(int tool) const;

signals:
    void toolSelected(int tool);
    void toolGroupActivated(int tool, const QString& groupId, int activeSubTool, const QVector<int>& subTools);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ToolDef {
        int id = -1;
        QString name;
        QString shortcut;
        QIcon icon;
        QString shortcutId;
    };

    struct ToolGroup {
        QString id;
        int hostTool = -1;
        int activeSubTool = -1;
        QVector<ToolDef> tools;
        QToolButton* button = nullptr;
        QMenu* menu = nullptr;
    };

    void activateToolButton(int buttonId);
    void activateToolGroup(int groupIndex);
    void selectSubTool(int groupIndex, int subTool);
    void showGroupMenu(int groupIndex);
    void applyTheme();
    void refreshToolGroupMenuStyles();
    void refreshGroupButton(int groupIndex);
    int groupIndexForTool(int tool) const;
    int groupIndexForSubTool(int tool, int subTool) const;
    QVector<int> subToolsForGroup(int groupIndex) const;

    QVector<QToolButton*> m_buttons;
    QVector<ToolGroup> m_toolGroups;
    QVector<int> m_activeGroupForTool;
    QButtonGroup* m_group = nullptr;
    ColorSwapWidget* m_colorSwapWidget = nullptr;
};
