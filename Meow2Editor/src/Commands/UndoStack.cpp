/// @file
/// @brief UndoStack (UndoStack.h).

#include "Commands/UndoStack.h"

#include <Meow2/Core/Assert.h>

#include <algorithm>
#include <utility>

namespace Meow2::Editor
{
UndoStack::UndoStack(std::size_t limit)
    : m_limit(std::max<std::size_t>(limit, 1))
{
}

void UndoStack::push(UndoStep step)
{
    MEOW2_ASSERT(step.undo && step.redo, "an undo step needs both closures");
    // The redo steps can't be reached any more; if the saved state was among them, neither can it.
    if (m_clean && *m_clean > m_position)
    {
        m_clean.reset();
    }
    m_steps.erase(m_steps.begin() + static_cast<std::ptrdiff_t>(m_position), m_steps.end());
    m_steps.push_back(std::move(step));
    m_position = m_steps.size();
    trim();
    ++m_revision;
}

bool UndoStack::undo()
{
    if (!canUndo())
    {
        return false;
    }
    --m_position;
    m_steps[m_position].undo();
    ++m_revision;
    return true;
}

bool UndoStack::redo()
{
    if (!canRedo())
    {
        return false;
    }
    m_steps[m_position].redo();
    ++m_position;
    ++m_revision;
    return true;
}

bool UndoStack::canUndo() const
{
    return m_position > 0;
}

bool UndoStack::canRedo() const
{
    return m_position < m_steps.size();
}

QStringList UndoStack::undoLabels() const
{
    QStringList labels;
    for (std::size_t i = m_position; i > 0; --i)
    {
        labels.push_back(m_steps[i - 1].label);
    }
    return labels;
}

QStringList UndoStack::redoLabels() const
{
    QStringList labels;
    for (std::size_t i = m_position; i < m_steps.size(); ++i)
    {
        labels.push_back(m_steps[i].label);
    }
    return labels;
}

void UndoStack::setLimit(std::size_t limit)
{
    m_limit = std::max<std::size_t>(limit, 1);
    trim();
    ++m_revision;
}

void UndoStack::markClean()
{
    m_clean = m_position;
    ++m_revision;
}

bool UndoStack::isClean() const
{
    return m_clean == m_position;
}

void UndoStack::clear()
{
    m_steps.clear();
    m_position = 0;
    m_clean = 0;
    ++m_revision;
}

void UndoStack::trim()
{
    while (m_steps.size() > m_limit)
    {
        // Drop from the side that's further away: done steps first (the oldest), and only drop redo steps when
        // the position is at the very start (a limit lowered right after undoing everything).
        if (m_position > 0)
        {
            m_steps.pop_front();
            --m_position;
            if (m_clean)
            {
                if (*m_clean == 0)
                {
                    m_clean.reset(); // the saved state was before the dropped step
                }
                else
                {
                    --*m_clean;
                }
            }
        }
        else
        {
            m_steps.pop_back();
            if (m_clean && *m_clean > m_steps.size())
            {
                m_clean.reset();
            }
        }
    }
}
} // namespace Meow2::Editor
