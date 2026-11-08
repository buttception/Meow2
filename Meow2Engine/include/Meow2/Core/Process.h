#pragma once

/// @file
/// @brief Facts about the running process and the system it runs on (its executable's location, the fonts folder).
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

/// @brief The folder the operating system keeps its fonts in, for apps that use a system font (Consolas).
///
/// Windows asks for the Windows folder (`GetWindowsDirectoryW`) and adds `Fonts`; Linux returns `/usr/share/fonts`,
/// whose fonts sit in subfolders.
/// @return The folder, or an empty path if it can't be determined.
[[nodiscard]] std::filesystem::path systemFontDirectory();
} // namespace Meow2
