/// @file
/// @brief Windows implementation of Meow2::executablePath().

#include "Meow2/Core/Process.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace Meow2
{
std::filesystem::path executablePath()
{
    // GetModuleFileNameW truncates silently when the buffer is too small (result == size), so
    // grow until it fits. Paths can be longer than MAX_PATH with long-path support enabled.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }
        if (length < buffer.size())
        {
            buffer.resize(length);
            return std::filesystem::path{buffer};
        }
        buffer.resize(buffer.size() * 2);
    }
}
} // namespace Meow2
