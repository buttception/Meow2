/// @file
/// @brief The CPU check's message on Linux: stderr (a message box would need a windowing library this early).

#include <cstdio>

namespace Meow2
{
/// @brief Shows the "this CPU can't run the app" message.
/// @param message The text.
void showCpuCheckFailure(const char* message) noexcept
{
    std::fputs(message, stderr);
    std::fputs("\n", stderr);
}
} // namespace Meow2
