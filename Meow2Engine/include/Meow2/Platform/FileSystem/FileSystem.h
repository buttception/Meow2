#pragma once

/// @file
/// @brief FileSystem: files by virtual path (`assets/textures/cat.png`), with the composition root deciding what each
///        mount (`assets`, `save`, …) is: a folder now, a game package or Android's assets later.
///
/// @code
/// Meow2::FileSystem::FileSystem files{engineLog};
/// files.mountDirectory("assets", projectFolder / "assets");                                   // read-only
/// files.mountDirectory("save", userFolder, Meow2::FileSystem::MountAccess::ReadWrite);
///
/// Meow2::Result<std::string> shader = files.readText("assets/shaders/basic.hlsl");
/// files.writeFile("save/slot1.bin", bytes);                                                 // atomic
/// @endcode
/// @see techstack.md §1, §3, docs/wiki/FileSystem.md

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::FileSystem
{
/// @brief Whether files under a mount may be written.
enum class MountAccess : std::uint8_t
{
    ReadOnly, ///< Reading only; writeFile() fails with ErrorCode::AccessDenied. The default (game data).
    ReadWrite ///< Reading and writing (saves, settings, editor projects).
};

/// @brief One entry of a directory listing. Plain data.
struct DirectoryEntry
{
    std::string name{};       ///< The entry's name (UTF-8), without the directory.
    bool isDirectory = false; ///< `true` for a folder, `false` for a file.

    /// @brief Entries are equal when name and kind are.
    friend bool operator==(const DirectoryEntry&, const DirectoryEntry&) = default;
};

/// @brief Reads and writes files by virtual path; each path's first component names a mount.
///
/// **Virtual paths** look like `assets/textures/cat.png`: a mount name, then a path inside it, separated by `/`, in
/// UTF-8, with the exact case of the files (Linux and Android are case-sensitive). Rejected with
/// ErrorCode::InvalidArgument: an empty path, a leading `/`, `\`, `:`, empty components (`a//b`), and `.` or `..`,
/// so no path can leave its mount. A path whose mount isn't mounted is ErrorCode::NotFound.
///
/// **Mounts** are given by the composition root: today native folders (mountDirectory()); game packages (M11) and
/// Android assets (M13) will be further kinds of mount with the same paths, so game code doesn't change.
/// @par Thread safety
/// Thread-safe. Reads, writes and queries may run on any thread at once (jobs load assets); they look up the mount
/// under a shared lock and do the file I/O without holding it. mountDirectory() and unmount() take the lock
/// exclusively. Two threads writing the same file at once is still a race on the file itself (last rename wins).
/// @par Lifetime
/// The Logger must outlive the FileSystem.
class FileSystem
{
public:
    /// @brief A file system with nothing mounted.
    /// @param log Where mounts and problems are logged. Must outlive the FileSystem.
    explicit FileSystem(Log::Logger& log);

    /// @brief Unmounts everything.
    ~FileSystem();

    FileSystem(const FileSystem&) = delete;            ///< Not copyable: one mount table.
    FileSystem& operator=(const FileSystem&) = delete; ///< Not copyable: one mount table.
    FileSystem(FileSystem&&) = delete;                 ///< Not movable: shared by reference.
    FileSystem& operator=(FileSystem&&) = delete;      ///< Not movable: shared by reference.

    /// @brief Makes a native folder available as `<name>/…`.
    /// @param name      The mount name: letters, digits, `_` and `-`.
    /// @param directory The folder; must exist.
    /// @param access    Whether writeFile() may write there.
    /// @return Nothing, or ErrorCode::InvalidArgument for a bad name, ErrorCode::NotFound if the folder doesn't
    ///         exist, ErrorCode::AlreadyExists if the name is taken.
    [[nodiscard]] Result<void> mountDirectory(std::string_view name, const std::filesystem::path& directory,
                                              MountAccess access = MountAccess::ReadOnly);

    /// @brief Removes a mount. Paths under it are ErrorCode::NotFound afterwards.
    /// @param name The mount name.
    /// @return `true` if it was mounted.
    bool unmount(std::string_view name);

    /// @brief Whether a mount exists.
    /// @param name The mount name.
    /// @return `true` if mounted.
    [[nodiscard]] bool isMounted(std::string_view name) const;

    /// @brief Reads a whole file.
    /// @param path A virtual path.
    /// @return The file's bytes, or ErrorCode::InvalidArgument (bad path), ErrorCode::NotFound (no such mount or
    ///         file), ErrorCode::AccessDenied or ErrorCode::IoError.
    [[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view path) const;

    /// @brief Reads a whole file as text (bytes as they are; no newline or encoding conversion).
    /// @param path A virtual path.
    /// @return The text, or the errors of readFile().
    [[nodiscard]] Result<std::string> readText(std::string_view path) const;

    /// @brief Writes a whole file, creating its folders. Atomic: a temporary file is written and renamed over the
    ///        old one, so a crash mid-write leaves the old file intact.
    /// @param path  A virtual path on a MountAccess::ReadWrite mount.
    /// @param bytes The new contents.
    /// @return Nothing, or ErrorCode::InvalidArgument, ErrorCode::NotFound (no such mount), ErrorCode::AccessDenied
    ///         (read-only mount, or the OS refused), ErrorCode::IoError.
    [[nodiscard]] Result<void> writeFile(std::string_view path, std::span<const std::byte> bytes);

    /// @brief Writes text; see writeFile().
    /// @param path A virtual path on a MountAccess::ReadWrite mount.
    /// @param text The new contents (written as they are).
    /// @return As writeFile().
    [[nodiscard]] Result<void> writeText(std::string_view path, std::string_view text);

    /// @brief Whether a file or folder exists at a virtual path.
    /// @param path A virtual path.
    /// @return `false` for invalid paths and unknown mounts too.
    [[nodiscard]] bool exists(std::string_view path) const;

    /// @brief A file's size.
    /// @param path A virtual path.
    /// @return Bytes, or ErrorCode::InvalidArgument or ErrorCode::NotFound (also for a folder).
    [[nodiscard]] Result<std::uint64_t> fileSize(std::string_view path) const;

    /// @brief The entries of a folder, sorted by name.
    /// @param path A virtual folder path; a mount's name alone lists its root.
    /// @return The entries, or ErrorCode::InvalidArgument or ErrorCode::NotFound (no such folder, or it's a file).
    [[nodiscard]] Result<std::vector<DirectoryEntry>> list(std::string_view path) const;

    /// @brief The native path a virtual path maps to, e.g. for the editor or external tools.
    /// @param path A virtual path (the file doesn't have to exist).
    /// @return The native path, or ErrorCode::InvalidArgument or ErrorCode::NotFound (no such mount).
    [[nodiscard]] Result<std::filesystem::path> nativePath(std::string_view path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::FileSystem
