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
    // RTLD_NODELETE: the code stays mapped after dlclose. C++ libraries such as DXC keep global objects that can't
    // survive being unloaded and reloaded, and unloading them only saves address space.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (handle == nullptr)
    {
        return fail(ErrorCode::NotFound, std::format("{}: {}", path.string(), dlerror()));
    }
    return SharedLibrary{handle};
}

Result<SharedLibrary> SharedLibrary::findLoaded(std::string_view name)
{
    // RTLD_NOLOAD: only finds what's already there.
    void* handle = dlopen(std::string{name}.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr)
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
