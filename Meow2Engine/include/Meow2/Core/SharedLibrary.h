#pragma once

/// @file
/// @brief SharedLibrary: a DLL or `.so` loaded while the program runs, for optional tools such as the shader compiler.

#include <Meow2/Core/Result.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Meow2
{
/// @brief A loaded shared library; unloaded when destroyed.
///
/// For things an app may run without: a program linked against a DLL doesn't start if the DLL is missing, while this
/// returns an error the app can handle.
/// @par Thread safety
/// Immutable after creation: symbol() may be called from any thread.
/// @par Lifetime
/// Function pointers from symbol() are valid only while the SharedLibrary lives.
class SharedLibrary
{
public:
    /// @brief Loads a library.
    /// @param path Full path of the library file.
    /// @return The library, or ErrorCode::NotFound with the OS's message.
    [[nodiscard]] static Result<SharedLibrary> open(const std::filesystem::path& path);

    /// @brief Finds a library that's already in this process, without loading anything.
    /// @param name The file name, e.g. `renderdoc.dll`.
    /// @return The library, kept loaded until this object goes; ErrorCode::NotFound if it isn't in the process.
    [[nodiscard]] static Result<SharedLibrary> findLoaded(std::string_view name);

    /// @brief Takes over the loaded library.
    /// @param other Left empty.
    SharedLibrary(SharedLibrary&& other) noexcept;

    /// @brief Unloads the current library and takes over another.
    /// @param other Left empty.
    /// @return This object.
    SharedLibrary& operator=(SharedLibrary&& other) noexcept;

    SharedLibrary(const SharedLibrary&) = delete;            ///< Not copyable: owns the loaded library.
    SharedLibrary& operator=(const SharedLibrary&) = delete; ///< Not copyable: owns the loaded library.

    /// @brief Unloads the library.
    ~SharedLibrary();

    /// @brief Looks up an exported symbol.
    /// @param name The symbol's exact name.
    /// @return Its address, or `nullptr` if the library doesn't export it.
    [[nodiscard]] void* symbol(const std::string& name) const;

    /// @brief Leaves the library loaded for the rest of the process, for libraries that can't be unloaded safely.
    void keepLoaded()
    {
        m_handle = nullptr;
    }

private:
    explicit SharedLibrary(void* handle) noexcept;

    void* m_handle = nullptr; ///< `HMODULE` on Windows, the `dlopen` handle elsewhere.
};
} // namespace Meow2
