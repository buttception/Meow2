#pragma once

/// @file
/// @brief The editor's built-in commands: undo/redo, settings, log and history queries.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorSettings.h"
#include "Commands/UndoStack.h"

#include <Meow2/Core/Log/ConsoleBuffer.h>
#include <Meow2/Core/Result.h>

namespace Meow2::Editor
{
/// @brief What the built-in commands work on. Plain data (references), the composition root fills it.
struct EditorCommandsDeps
{
    EditorCommandRegistry& commands;   ///< Where they're registered.
    UndoStack& history;                ///< What `edit.undo`/`edit.redo` move; `history.get` reads.
    EditorSettings& settings;          ///< What `settings.*` read and write.
    const Log::ConsoleBuffer& console; ///< What `log.lines` reads.
};

/// @brief Registers the built-in commands (docs/wiki/Editor.md lists them):
/// - actions `edit.undo`, `edit.redo` (enabled only when there's a step), `settings.set {key, value}`;
/// - queries `settings.list`, `history.get`, `log.lines {since, levels, text, limit}`.
///
/// `settings.set` isn't recorded in the history: a preference isn't part of the document, and Ctrl+Z in a scene
/// shouldn't change one. Setting `editor.undoLimit` applies to the history at once.
/// @param deps What they work on; everything in it must outlive the registered commands' use.
/// @return Nothing, or the registry's error if a name was already taken.
Result<void> registerEditorCommands(const EditorCommandsDeps& deps);
} // namespace Meow2::Editor
