/// @file
/// @brief SharedLibrary on Windows: LoadLibraryExW.

#include <Meow2/Core/SharedLibrary.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <format>
#include <utility>

namespace Meow2
{
Result<SharedLibrary> SharedLibrary::open(const std::filesystem::path& path)
{
    // LOAD_WITH_ALTERED_SEARCH_PATH: the library's own dependencies are looked for next to it, not next to the exe.
    HMODULE handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (handle == nullptr)
    {
        return fail(ErrorCode::NotFound, std::format("{}: error {}", path.string(), GetLastError()));
    }
    return SharedLibrary{handle};
}

Result<SharedLibrary> SharedLibrary::findLoaded(std::string_view name)
{
    HMODULE handle = nullptr;
    // No flags: unlike GetModuleHandle, this adds a reference, which the destructor's FreeLibrary gives back.
    if (GetModuleHandleExA(0, std::string{name}.c_str(), &handle) == 0)
    {
        return fail(ErrorCode::NotFound, std::string{name});
    }
    return SharedLibrary{handle};
}

SharedLibrary::SharedLibrary(void* handle) noexcept
    : m_handle(handle)
{
}

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr))
{
}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept
{
    if (this != &other)
    {
        if (m_handle != nullptr)
        {
            FreeLibrary(static_cast<HMODULE>(m_handle));
        }
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

SharedLibrary::~SharedLibrary()
{
    if (m_handle != nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(m_handle));
    }
}

void* SharedLibrary::symbol(const std::string& name) const
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name.c_str()));
}
} // namespace Meow2
