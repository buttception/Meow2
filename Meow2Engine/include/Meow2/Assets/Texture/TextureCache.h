/// @file
/// @brief TextureCache: cooks textures once and keeps the result as `.m2texture` files, so later loads skip BC7.

#pragma once

#include <Meow2/Assets/Texture/CookedTexture.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <string>

namespace Meow2::FileSystem
{
class FileSystem;
}

namespace Meow2::Assets
{
/// @brief Cooks images through a folder of `.m2texture` files named by textureCacheKey().
///
/// A hit reads the file instead of cooking. A miss cooks, then writes the file (atomically), so the next run, or the
/// same image in another model, is a read. A file that can't be read or written is logged and the texture cooked
/// anyway: the cache only saves time.
/// @par Thread safety
/// Thread-safe: it keeps no state of its own, and the FileSystem is thread-safe. Two threads cooking the same image
/// at once both cook it and write the same file; the last rename wins, with identical contents.
/// @par Lifetime
/// The FileSystem and the Logger must outlive it.
class TextureCache
{
public:
    /// @brief A cache in a folder of the virtual file system.
    /// @param files  Reads and writes the files.
    /// @param folder A virtual folder on a MountAccess::ReadWrite mount, e.g. `cache/textures`; created on the first
    ///               write.
    /// @param log    Receives cache problems.
    TextureCache(FileSystem::FileSystem& files, std::string folder, Log::Logger& log);

    /// @brief The cooked form of an image: from the cache, or cooked now and saved.
    /// @param image    The image, RGBA8.
    /// @param kind     What it holds.
    /// @param compress Whether to compress to BC7 or BC5.
    /// @param jobs     Runs compression in parallel; null cooks on the calling thread.
    /// @return The texture, or cookTexture()'s error.
    [[nodiscard]] Result<CookedTexture> cook(const Image& image, TextureKind kind, bool compress,
                                             Jobs::JobSystem* jobs = nullptr);

    /// @brief The virtual path the cooked form of an image is kept at.
    /// @param image    The image.
    /// @param kind     What it holds.
    /// @param compress Whether it's compressed.
    /// @return `<folder>/<key>.m2texture`.
    [[nodiscard]] std::string pathOf(const Image& image, TextureKind kind, bool compress) const;

private:
    FileSystem::FileSystem& m_files; ///< Where the files are.
    std::string m_folder;            ///< Virtual folder of the files.
    Log::Logger& m_log;              ///< Receives problems.
};
} // namespace Meow2::Assets
