/// @file
/// @brief The process's console window (internal).

#pragma once

namespace Meow2
{
/// @brief Makes the console show UTF-8 text as UTF-8.
///
/// Log lines are UTF-8 (`→`, `°`, `…`). A Windows console decodes output with the system's OEM code page (437 on
/// English Windows) unless told otherwise, so `→` came out as `ΓåÆ`. Linux and Android terminals are UTF-8 already:
/// nothing to do there.
/// @note Changes a process-wide setting of the OS console; call it from the composition root's thread, before logging
///       to the console starts.
void enableUtf8ConsoleOutput();
} // namespace Meow2
