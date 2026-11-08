/// @file
/// @brief Windows implementation of Meow2::executablePath() and Meow2::systemFontDirectory().

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

std::filesystem::path systemFontDirectory()
{
    // Usually C:\Windows, but not always: Windows can be installed on another drive or folder.
    std::wstring buffer(MAX_PATH, L'\0');
    const UINT length = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path{buffer} / L"Fonts";
}
} // namespace Meow2
