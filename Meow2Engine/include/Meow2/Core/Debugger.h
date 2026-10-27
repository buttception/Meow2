#pragma once

/// @file
/// @brief Querying the debugger: is the program running under one?
/// @par Thread safety
/// Thread-safe: the functions here only read process state.

namespace Meow2
{
/// @brief Returns whether a debugger is attached to this process right now.
///
/// Used by asserts to choose between stopping in the debugger and aborting. Windows uses `IsDebuggerPresent()`;
/// Linux and Android read `TracerPid` from `/proc/self/status` (non-zero while `gdb`, `lldb` or `strace` is attached).
/// @return `true` if a debugger is attached. `false` if none is, or if it can't be determined.
/// @note A debugger can attach or detach at any time, so don't cache the result.
bool isDebuggerAttached();
} // namespace Meow2
