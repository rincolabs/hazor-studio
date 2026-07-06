#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <QString>
#include <QDebug>

class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString name() const = 0;
};

class CommandHistory {
public:
    using ChangeCallback = std::function<void()>;

    void setMaxSteps(int maxSteps) { m_maxSteps = maxSteps; }
    int maxSteps() const { return m_maxSteps; }

    void push(std::unique_ptr<Command> cmd);
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void clear();

    // ── Grouped undo (macro) ────────────────────────────────────────────────
    // Between beginMacro()/endMacro() every push() is accumulated instead of
    // landing as its own history entry; endMacro() commits them as ONE entry
    // named `name`. Lets a compound gesture (e.g. auto-creating a pixel layer
    // and then painting on it) undo/redo as a single step. Nesting is flattened:
    // an inner beginMacro keeps appending to the already-open group. Calls to
    // pushed commands are NOT re-executed here — they are recorded post-execution,
    // exactly like a normal push(), so the committed group only drives undo/redo.
    void beginMacro(const QString& name);
    void endMacro();
    bool inMacro() const { return m_macroDepth > 0; }
    int size() const { return static_cast<int>(m_commands.size()); }
    int currentIndex() const { return m_current; }
    const Command* commandAt(int index) const;

    void setChangeCallback(ChangeCallback cb) { m_changeCb = std::move(cb); }

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    int m_current = -1;
    int m_maxSteps = 500;
    ChangeCallback m_changeCb;

    // Open-macro accumulation. Commands pushed while m_macroDepth > 0 are parked
    // in m_macroCommands and committed as a single grouped entry by endMacro().
    int m_macroDepth = 0;
    QString m_macroName;
    std::vector<std::unique_ptr<Command>> m_macroCommands;
};
