#pragma once

#include <QObject>
#include <QVector>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QAction;
class QShortcut;
class QKeyEvent;

class ShortcutManager : public QObject
{
    Q_OBJECT
public:
    enum class ShortcutScope {
        Global,
        GlobalSafe,
        Canvas,
        Tool,
        ToolOption,
        Modal,
        TextEditing,
        LayerPanel,
        LayerPanelOrCanvas,
        LayerPanelOrMoveTool,
        MaskEdit,
        MaskTarget
    };

    struct ActionDef {
        QString id;
        QString category;
        QString label;
        QKeySequence defaultSeq;
        QVector<QKeySequence> defaultAlternatives;
        ShortcutScope scope = ShortcutScope::Global;
        QString aliasOf;
        bool customizable = true;
    };

    struct DispatchContext {
        bool textInputFocused = false;
        bool inlineRenameActive = false;
        bool popupActive = false;
        bool textCanvasEditingActive = false;
        bool modalActive = false;
        bool canvasFocused = false;
        bool layerPanelFocused = false;
        bool toolActive = false;
        bool moveToolActive = false;
        bool maskEditActive = false;
        bool maskTargetActive = false;
    };

    static ShortcutManager* instance();

    const QVector<ActionDef>& actions() const { return m_actions; }
    QStringList categories() const;
    QVector<ActionDef> actionsInCategory(const QString& category) const;

    bool hasAction(const QString& id) const;
    QString canonicalId(const QString& id) const;
    ActionDef actionDefinition(const QString& id) const;
    ShortcutScope actionScope(const QString& id) const;
    QString actionLabel(const QString& id) const;

    QKeySequence currentShortcut(const QString& id) const;
    QVector<QKeySequence> currentShortcuts(const QString& id) const;
    QKeySequence defaultShortcut(const QString& id) const;
    QVector<QKeySequence> defaultShortcuts(const QString& id) const;
    bool isModified(const QString& id) const;
    QString shortcutDisplayText(const QString& id) const;
    QString shortcutTooltipText(const QString& id, const QString& baseText = {}) const;

    bool setShortcut(const QString& id, const QKeySequence& seq, bool silent = false);
    void resetToDefault(const QString& id);
    void resetAll();
    QString findConflict(const QKeySequence& seq, const QString& exceptId = {}) const;
    QString findConflict(const QKeySequence& seq, const QString& exceptId,
                         const DispatchContext& context) const;

    bool matchesShortcut(const QString& id, const QKeySequence& seq) const;
    bool actionEnabledInContext(const QString& id, const DispatchContext& context) const;
    QString actionForSequence(const QKeySequence& seq, const DispatchContext& context) const;
    QStringList actionsForSequence(const QKeySequence& seq, const DispatchContext& context) const;
    bool dispatchShortcut(const QKeySequence& seq, const DispatchContext& context);
    // Invokes a registered callback by id (alias-resolved), regardless of key
    // context. Lets menu items / buttons reuse the exact logic a shortcut runs.
    bool trigger(const QString& id);
    static QKeySequence eventSequence(const QKeyEvent* event);

    void registerAlias(const QString& aliasId, const QString& targetId);
    void registerAction(const QString& id, QAction* action);
    void registerShortcut(const QString& id, QShortcut* sc);
    void registerCallback(const QString& id, std::function<void()> callback);

    void loadSettings();
    void saveSettings();

signals:
    void shortcutsChanged();

private:
    ShortcutManager(QObject* parent = nullptr);
    void buildDefaults();
    void applyShortcutBindings(const QString& id);
    int contextPriorityForScope(ShortcutScope scope, const DispatchContext& context) const;
    static QList<QKeySequence> toShortcutList(const QVector<QKeySequence>& seqs);

    QVector<ActionDef> m_actions;
    QHash<QString, int> m_actionIndex;   // id → index
    QHash<QString, QString> m_aliases;
    QHash<QString, QKeySequence> m_custom;
    QHash<QString, QAction*> m_boundActions;
    QHash<QString, QShortcut*> m_boundShortcuts;
    QHash<QString, std::function<void()>> m_callbacks;
    QHash<QString, QMetaObject::Connection> m_connections;
};
