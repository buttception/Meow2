/// @file
/// @brief Linux/Android implementation of Meow2::isDebuggerAttached().

#include "Meow2/Core/Debugger.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

namespace Meow2
{
bool isDebuggerAttached()
{
    // The kernel reports the PID of the process tracing us (gdb, lldb, strace) as
    // "TracerPid:\t<pid>" in /proc/self/status; 0 means nobody is attached.
    constexpr std::string_view c_tracerKey = "TracerPid:";

    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
    {
        if (line.starts_with(c_tracerKey))
        {
            return std::strtol(line.c_str() + c_tracerKey.size(), nullptr, 10) != 0;
        }
    }
    return false;
}
} // namespace Meow2
