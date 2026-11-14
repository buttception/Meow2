#pragma once

/// @file
/// @brief UndoStack: the editor's one undo/redo history.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <QString>
#include <QStringList>

namespace Meow2::Editor
{
/// @brief What one command did, as two closures: one takes the change back, the other makes it again. Plain data.
///
/// A command's handler returns it (CommandOutcome::undoStep). The closures must capture everything they need by
/// value (the old and the new value, an `EntityId`, ...), never pointers that a later step could invalidate.
struct UndoStep
{
    QString label{};              ///< What the user sees in Edit → Undo `<label>`, e.g. "Set editor.undoLimit".
    std::function<void()> undo{}; ///< Takes the change back. Must not fail: the change was already made once.
    std::function<void()> redo{}; ///< Makes the change again, exactly as the command first did.
};

/// @brief The editor's history: every recorded step, in order, with a position that undo and redo move.
///
/// Steps before the position can be undone, steps after it redone. Pushing a step after an undo drops the redo
/// steps, as in every editor. The history keeps at most limit() steps; the oldest go first.
///
/// A **clean mark** remembers the position where the document was last saved, so the editor can show unsaved
/// changes: isClean() is true exactly when undo/redo brought the history back to that position. If the marked step
/// is dropped (by the limit, or by a push after an undo), the saved state can't be reached any more and isClean()
/// stays false until the next markClean().
/// @par Thread safety
/// Not thread-safe; the editor's main thread only, like every command.
class UndoStack
{
public:
    /// @brief The default for `editor.undoLimit`.
    static constexpr std::size_t c_defaultLimit = 200;

    /// @brief An empty, clean history.
    /// @param limit The most steps it keeps; at least 1.
    explicit UndoStack(std::size_t limit = c_defaultLimit);

    /// @brief Records a step that was just done. Drops every redo step, then the oldest steps beyond the limit.
    /// @param step The step; its closures must be set.
    void push(UndoStep step);

    /// @brief Undoes the step before the position and moves the position back.
    /// @return `false` (and does nothing) when there's nothing to undo.
    bool undo();

    /// @brief Redoes the step after the position and moves the position forward.
    /// @return `false` (and does nothing) when there's nothing to redo.
    bool redo();

    /// @brief Whether undo() would do something.
    /// @return `true` if a step can be undone.
    [[nodiscard]] bool canUndo() const;

    /// @brief Whether redo() would do something.
    /// @return `true` if a step can be redone.
    [[nodiscard]] bool canRedo() const;

    /// @brief The labels of the steps that can be undone, the next one to undo first.
    /// @return The labels.
    [[nodiscard]] QStringList undoLabels() const;

    /// @brief The labels of the steps that can be redone, the next one to redo first.
    /// @return The labels.
    [[nodiscard]] QStringList redoLabels() const;

    /// @brief How many steps it keeps.
    /// @return The limit.
    [[nodiscard]] std::size_t limit() const
    {
        return m_limit;
    }

    /// @brief Changes the limit, dropping the oldest steps if there are more.
    /// @param limit The most steps it keeps; at least 1.
    void setLimit(std::size_t limit);

    /// @brief Marks the current position as saved.
    void markClean();

    /// @brief Whether the history is at the position last marked clean (a new history is clean).
    /// @return `true` if nothing changed since the last markClean().
    [[nodiscard]] bool isClean() const;

    /// @brief Drops every step and marks the empty history clean (e.g. after opening another scene).
    void clear();

    /// @brief A number that changes whenever the history does (push, undo, redo, limit, clean mark, clear), so the UI
    ///        can refresh its Undo/Redo actions only when needed.
    /// @return The revision; starts at 0.
    [[nodiscard]] std::uint64_t revision() const
    {
        return m_revision;
    }

private:
    /// @brief Drops the oldest steps until at most m_limit remain, moving the position and the clean mark with them.
    void trim();

    std::deque<UndoStep> m_steps;           ///< Every step, oldest first.
    std::size_t m_position = 0;             ///< Steps [0, position) are done; [position, size) can be redone.
    std::size_t m_limit;                    ///< The most steps kept.
    std::optional<std::size_t> m_clean = 0; ///< The position last marked clean; unset once it can't be reached.
    std::uint64_t m_revision = 0;           ///< See revision().
};
} // namespace Meow2::Editor
