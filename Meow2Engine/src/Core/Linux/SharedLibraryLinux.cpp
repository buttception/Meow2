/// @file
/// @brief SharedLibrary on Linux and Android: dlopen.

#include <Meow2/Core/SharedLibrary.h>

#include <dlfcn.h>

#include <format>
#include <utility>

namespace Meow2
{
Result<SharedLibrary> SharedLibrary::open(const std::filesystem::path& path)
{
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        return fail(ErrorCode::NotFound, std::format("{}: {}", path.string(), dlerror()));
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
            dlclose(m_handle);
        }
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

SharedLibrary::~SharedLibrary()
{
    if (m_handle != nullptr)
    {
        dlclose(m_handle);
    }
}

void* SharedLibrary::symbol(const std::string& name) const
{
    return dlsym(m_handle, name.c_str());
}
} // namespace Meow2
