/// @file
/// @brief Linux/Android implementation of Meow2::executablePath().

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
} // namespace Meow2
