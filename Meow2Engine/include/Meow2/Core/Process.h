#pragma once

/// @file
/// @brief Facts about the running process (its executable's location).
/// @par Thread safety
/// Thread-safe: the functions only read process state.

#include <filesystem>

namespace Meow2
{
/// @brief Full path of the running executable, e.g. `F:/Meow2/build/vs2022/bin/Debug/Meow2Renderer.exe`.
///
/// Windows uses `GetModuleFileNameW`; Linux and Android read the `/proc/self/exe` link.
/// @return The absolute path, or an empty path if it can't be determined.
[[nodiscard]] std::filesystem::path executablePath();
} // namespace Meow2
