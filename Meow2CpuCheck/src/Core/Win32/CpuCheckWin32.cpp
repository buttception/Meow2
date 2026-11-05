/// @file
/// @brief The CPU check's message on Windows: stderr for console builds, and a message box, because Release builds
///        have no console (GUI subsystem) and the user would otherwise see nothing at all.

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace Meow2
{
/// @brief Shows the "this CPU can't run the app" message.
/// @param message The text.
void showCpuCheckFailure(const char* message) noexcept
{
    std::fputs(message, stderr);
    std::fputs("\n", stderr);
    MessageBoxA(nullptr, message, "Unsupported processor", MB_OK | MB_ICONERROR);
}
} // namespace Meow2
