/// @file
/// @brief FileSystem (FileSystem.h): virtual paths over mounted native folders.

#include "Meow2/Platform/FileSystem/FileSystem.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <utility>
#include <variant>

namespace Meow2::FileSystem
{
namespace
{
/// @brief A native folder mounted under a name.
struct DirectoryMount
{
    std::filesystem::path root; ///< The folder.
};

/// @brief What a mount reads from. A variant, not a virtual interface (techstack.md §3): packages (M11) and Android
///        assets (M12) become further alternatives.
using MountSource = std::variant<DirectoryMount>;

/// @brief One entry of the mount table.
struct Mount
{
    std::string name;   ///< The first component of virtual paths under it.
    MountAccess access; ///< Whether it may be written.
    MountSource source; ///< Where its files are.
};

/// @brief A virtual path split into its mount and the rest.
struct VirtualPath
{
    std::string_view mount;    ///< The mount name.
    std::string_view relative; ///< The path inside the mount; empty for the mount's root.
};

/// @brief A virtual path resolved to a native one.
struct Resolved
{
    std::filesystem::path native; ///< Where the file is (or would be).
    MountAccess access;           ///< The mount's access.
};

bool isNameCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool isValidMountName(std::string_view name)
{
    return !name.empty() && std::ranges::all_of(name, isNameCharacter);
}

/// @brief Checks the path rules (FileSystem.h) and splits it at the first `/`.
Result<VirtualPath> parse(std::string_view path)
{
    const auto invalid = [path](std::string_view why)
    {
        return fail(ErrorCode::InvalidArgument, std::format("bad virtual path '{}': {}", path, why));
    };
    if (path.empty())
    {
        return invalid("empty");
    }
    if (path.find('\\') != std::string_view::npos)
    {
        return invalid("use '/' as the separator");
    }
    if (path.find(':') != std::string_view::npos)
    {
        return invalid("':' isn't allowed (drive letters, Windows streams)");
    }
    if (path.front() == '/')
    {
        return invalid("it starts with '/'; it must start with a mount name");
    }

    // Every component must be a real name: no "", "." or "..", so a path can never leave its mount.
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view component = path.substr(start, end - start);
        if (component.empty() && end < path.size())
        {
            return invalid("empty component ('//')");
        }
        if (component == "." || component == "..")
        {
            return invalid("'.' and '..' aren't allowed");
        }
        start = end + 1;
    }
    if (path.back() == '/')
    {
        return invalid("it ends with '/'");
    }

    const std::size_t slash = path.find('/');
    const VirtualPath parsed{.mount = path.substr(0, slash),
                             .relative = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1)};
    if (!isValidMountName(parsed.mount))
    {
        return invalid("the mount name may only have letters, digits, '_' and '-'");
    }
    return parsed;
}

/// @brief A UTF-8 string as a native path (on Windows, std::filesystem would otherwise read it as the ANSI code page).
std::filesystem::path fromUtf8(std::string_view text)
{
    return std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

/// @brief A native path's last component as UTF-8.
std::string fileNameUtf8(const std::filesystem::path& path)
{
    const std::u8string name = path.filename().u8string();
    return {reinterpret_cast<const char*>(name.data()), name.size()};
}

/// @brief An OS error as an engine error.
Error toError(const std::error_code& error, std::string_view what)
{
    const std::string message = std::format("{}: {}", what, error.message());
    if (error == std::errc::no_such_file_or_directory)
    {
        return Error{ErrorCode::NotFound, message};
    }
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
    {
        return Error{ErrorCode::AccessDenied, message};
    }
    return Error{ErrorCode::IoError, message};
}
} // namespace

/// @brief The mount table and the lock that guards it.
struct FileSystem::Impl
{
    /// @brief Looks a virtual path up in the mount table (shared lock).
    Result<Resolved> resolve(std::string_view path) const
    {
        const Result<VirtualPath> parsed = parse(path);
        if (!parsed)
        {
            return Unexpected<Error>(parsed.error());
        }
        const std::shared_lock lock{mutex};
        const auto mount = std::ranges::find(mounts, parsed->mount, &Mount::name);
        if (mount == mounts.end())
        {
            return fail(ErrorCode::NotFound, std::format("'{}': nothing is mounted as '{}'", path, parsed->mount));
        }
        return std::visit(
            [&](const DirectoryMount& directory) -> Result<Resolved>
            {
                return Resolved{.native = parsed->relative.empty() ? directory.root
                                                                   : directory.root / fromUtf8(parsed->relative),
                                .access = mount->access};
            },
            mount->source);
    }

    Log::Logger* log = nullptr;      ///< Never null.
    mutable std::shared_mutex mutex; ///< Guards `mounts`; never held during file I/O.
    std::vector<Mount> mounts;       ///< A handful of entries: a linear search is fastest.
};

FileSystem::FileSystem(Log::Logger& log)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->log = &log;
}

FileSystem::~FileSystem() = default;

Result<void> FileSystem::mountDirectory(std::string_view name, const std::filesystem::path& directory,
                                        MountAccess access)
{
    if (!isValidMountName(name))
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("mount name '{}' may only have letters, digits, '_' and '-'", name));
    }
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return fail(ErrorCode::NotFound, std::format("can't mount '{}': {} isn't a folder", name,
                                                     reinterpret_cast<const char*>(directory.u8string().c_str())));
    }
    std::filesystem::path root = std::filesystem::absolute(directory, error);
    if (error)
    {
        root = directory;
    }

    const std::unique_lock lock{m_impl->mutex};
    if (std::ranges::find(m_impl->mounts, name, &Mount::name) != m_impl->mounts.end())
    {
        return fail(ErrorCode::AlreadyExists, std::format("'{}' is already mounted", name));
    }
    m_impl->mounts.push_back({.name = std::string{name}, .access = access, .source = DirectoryMount{.root = root}});
    MEOW2_LOG_INFO(*m_impl->log, "FileSystem: '{}' → {} ({})", name,
                   reinterpret_cast<const char*>(root.u8string().c_str()),
                   access == MountAccess::ReadWrite ? "read-write" : "read-only");
    return {};
}

bool FileSystem::unmount(std::string_view name)
{
    const std::unique_lock lock{m_impl->mutex};
    return std::erase_if(m_impl->mounts, [name](const Mount& mount) { return mount.name == name; }) > 0;
}

bool FileSystem::isMounted(std::string_view name) const
{
    const std::shared_lock lock{m_impl->mutex};
    return std::ranges::find(m_impl->mounts, name, &Mount::name) != m_impl->mounts.end();
}

Result<std::vector<std::byte>> FileSystem::readFile(std::string_view path) const
{
    const Result<Resolved> resolved = m_impl->resolve(path);
    if (!resolved)
    {
        return Unexpected<Error>(resolved.error());
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved->native, error))
    {
        return fail(error ? toError(error, path).code : ErrorCode::NotFound, std::format("'{}' isn't a file", path));
    }
    const std::uintmax_t size = std::filesystem::file_size(resolved->native, error);
    if (error)
    {
        return Unexpected<Error>(toError(error, path));
    }

    std::ifstream stream{resolved->native, std::ios::binary};
    if (!stream)
    {
        return fail(ErrorCode::AccessDenied, std::format("'{}' can't be opened", path));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<std::uintmax_t>(stream.gcount()) != size)
    {
        return fail(ErrorCode::IoError, std::format("'{}': read {} of {} bytes", path, stream.gcount(), size));
    }
    return bytes;
}

Result<std::string> FileSystem::readText(std::string_view path) const
{
    Result<std::vector<std::byte>> bytes = readFile(path);
    if (!bytes)
    {
        return Unexpected<Error>(bytes.error());
    }
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

Result<void> FileSystem::writeFile(std::string_view path, std::span<const std::byte> bytes)
{
    const Result<Resolved> resolved = m_impl->resolve(path);
    if (!resolved)
    {
        return Unexpected<Error>(resolved.error());
    }
    if (resolved->access != MountAccess::ReadWrite)
    {
        return fail(ErrorCode::AccessDenied, std::format("'{}': the mount is read-only", path));
    }
    std::error_code error;
    std::filesystem::create_directories(resolved->native.parent_path(), error);
    if (error)
    {
        return Unexpected<Error>(toError(error, path));
    }

    // Write a temporary file and rename it over the old one: a crash or a full disk mid-write then leaves the old
    // file intact instead of a half-written one (the same as Config::save()).
    std::filesystem::path temporary = resolved->native;
    temporary += ".tmp";
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream)
        {
            stream.close();
            std::filesystem::remove(temporary, error);
            return fail(ErrorCode::IoError, std::format("'{}' couldn't be written", path));
        }
    }
    std::filesystem::rename(temporary, resolved->native, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Unexpected<Error>(toError(error, path));
    }
    return {};
}

Result<void> FileSystem::writeText(std::string_view path, std::string_view text)
{
    return writeFile(path, std::as_bytes(std::span{text.data(), text.size()}));
}

bool FileSystem::exists(std::string_view path) const
{
    const Result<Resolved> resolved = m_impl->resolve(path);
    std::error_code error;
    return resolved && std::filesystem::exists(resolved->native, error);
}

Result<std::uint64_t> FileSystem::fileSize(std::string_view path) const
{
    const Result<Resolved> resolved = m_impl->resolve(path);
    if (!resolved)
    {
        return Unexpected<Error>(resolved.error());
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved->native, error))
    {
        return fail(ErrorCode::NotFound, std::format("'{}' isn't a file", path));
    }
    const std::uintmax_t size = std::filesystem::file_size(resolved->native, error);
    if (error)
    {
        return Unexpected<Error>(toError(error, path));
    }
    return static_cast<std::uint64_t>(size);
}

Result<std::vector<DirectoryEntry>> FileSystem::list(std::string_view path) const
{
    const Result<Resolved> resolved = m_impl->resolve(path);
    if (!resolved)
    {
        return Unexpected<Error>(resolved.error());
    }
    std::error_code error;
    if (!std::filesystem::is_directory(resolved->native, error))
    {
        return fail(ErrorCode::NotFound, std::format("'{}' isn't a folder", path));
    }
    std::vector<DirectoryEntry> entries;
    for (std::filesystem::directory_iterator it{resolved->native, error}, end; !error && it != end; it.increment(error))
    {
        std::error_code typeError;
        entries.push_back({.name = fileNameUtf8(it->path()), .isDirectory = it->is_directory(typeError)});
    }
    if (error)
    {
        return Unexpected<Error>(toError(error, path));
    }
    std::ranges::sort(entries, {}, &DirectoryEntry::name);
    return entries;
}

Result<std::filesystem::path> FileSystem::nativePath(std::string_view path) const
{
    const Result<Resolved> resolved = m_impl->resolve(path);
    if (!resolved)
    {
        return Unexpected<Error>(resolved.error());
    }
    return resolved->native;
}
} // namespace Meow2::FileSystem
