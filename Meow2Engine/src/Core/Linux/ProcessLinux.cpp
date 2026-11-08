/// @file
/// @brief Linux/Android implementation of Meow2::executablePath() and Meow2::systemFontDirectory().

#include "Meow2/Core/Process.h"

#include <system_error>

namespace Meow2
{
std::filesystem::path executablePath()
{
    // The kernel exposes the running executable as the symbolic link /proc/self/exe.
    std::error_code error;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error)
    {
        return {};
    }
    return path;
}

std::filesystem::path systemFontDirectory()
{
    // Fontconfig's standard root; Android has no such folder, and its apps bundle their fonts.
    return "/usr/share/fonts";
}
} // namespace Meow2
