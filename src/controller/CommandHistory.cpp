#include "CommandHistory.hpp"

namespace {

// Groups several already-executed commands into one undo/redo step. Redo replays
// children in order; undo reverses them. Kept local to the history so callers
// only ever see a single committed Command for an auto-grouped gesture.
class MacroCommand : public Command {
public:
    explicit MacroCommand(QString name, std::vector<std::unique_ptr<Command>> cmds)
        : m_name(std::move(name)), m_commands(std::move(cmds)) {}

    void execute() override {
        for (auto& c : m_commands) c->execute();
    }
    void undo() override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
            (*it)->undo();
    }
    QString name() const override { return m_name; }

private:
    QString m_name;
    std::vector<std::unique_ptr<Command>> m_commands;
};

} // namespace

void CommandHistory::beginMacro(const QString& name)
{
    if (m_macroDepth == 0)
        m_macroName = name;
    ++m_macroDepth;
}

void CommandHistory::endMacro()
{
    if (m_macroDepth == 0) return;       // unbalanced / no-op safety
    if (--m_macroDepth > 0) return;      // still inside an outer macro

    auto cmds = std::move(m_macroCommands);
    m_macroCommands.clear();
    const QString name = m_macroName;
    m_macroName.clear();

    if (cmds.empty())
        return;   // nothing happened inside the group

    // Wrap as one entry even for a single child so the history reads as the
    // whole gesture (e.g. "Fill"), not the raw underlying step.
    push(std::make_unique<MacroCommand>(name, std::move(cmds)));
}

void CommandHistory::push(std::unique_ptr<Command> cmd)
{
    // Inside an open macro, accumulate instead of committing a discrete entry.
    if (m_macroDepth > 0) {
        m_macroCommands.push_back(std::move(cmd));
        return;
    }

    // Discard redo history
    m_commands.resize(m_current + 1);

    // Enforce max steps: drop oldest entries first
    if (static_cast<int>(m_commands.size()) >= m_maxSteps) {
        int excess = static_cast<int>(m_commands.size()) - m_maxSteps + 1;
        m_commands.erase(m_commands.begin(), m_commands.begin() + excess);
        m_current -= excess;
    }

    m_commands.push_back(std::move(cmd));
    ++m_current;

    if (m_changeCb) m_changeCb();
}

bool CommandHistory::canUndo() const
{
    return m_current >= 0 && !m_commands.empty();
}

bool CommandHistory::canRedo() const
{
    return m_current + 1 < static_cast<int>(m_commands.size());
}

void CommandHistory::undo()
{
    if (!canUndo()) return;
    m_commands[m_current]->undo();
    --m_current;
    if (m_changeCb) m_changeCb();
}

void CommandHistory::redo()
{
    if (!canRedo()) return;
    ++m_current;
    m_commands[m_current]->execute();
    if (m_changeCb) m_changeCb();
}

void CommandHistory::clear()
{
    m_commands.clear();
    m_current = -1;
    // Drop any half-open macro so a reset never leaks parked commands.
    m_macroDepth = 0;
    m_macroName.clear();
    m_macroCommands.clear();
    if (m_changeCb) m_changeCb();
}

const Command* CommandHistory::commandAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_commands.size()))
        return nullptr;
    return m_commands[index].get();
}
