/// @file
/// @brief AssetManager: `.meta` sidecars and the tables of known and loaded assets.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <toml++/toml.h>

#include <format>

namespace Meow2::Assets
{
namespace
{
constexpr std::string_view c_metaExtension = ".meta";

/// @brief Reads the `id` key of a `.meta` file.
Result<AssetId> readMetaId(std::string_view text, const std::string& metaPath)
{
    toml::parse_result parsed = toml::parse(text);
    if (!parsed)
    {
        return fail(ErrorCode::ParseError, std::format("{}: {}", metaPath, parsed.error().description()));
    }
    const std::optional<std::string_view> idText = parsed.table()["id"].value<std::string_view>();
    if (!idText)
    {
        return fail(ErrorCode::ParseError, std::format("{}: no id", metaPath));
    }
    Result<Uuid> uuid = Uuid::parse(*idText);
    if (!uuid)
    {
        return fail(ErrorCode::ParseError, std::format("{}: {}", metaPath, uuid.error().message));
    }
    Result<AssetId> id = AssetId::fromUuid(*uuid);
    if (!id)
    {
        return fail(ErrorCode::ParseError, std::format("{}: {}", metaPath, id.error().message));
    }
    return id;
}
} // namespace

AssetManager::AssetManager(FileSystem::FileSystem& files, UuidGenerator& ids, Log::Logger& log)
    : m_files(files)
    , m_ids(ids)
    , m_log(log)
{
}

Result<std::size_t> AssetManager::scan(std::string_view folder)
{
    Result<std::size_t> count = scanFolder(std::string{folder});
    if (count)
    {
        MEOW2_LOG_INFO(m_log, "Assets: {} in {}", *count, folder);
    }
    return count;
}

Result<std::size_t> AssetManager::scanFolder(const std::string& folder)
{
    Result<std::vector<FileSystem::DirectoryEntry>> entries = m_files.list(folder);
    if (!entries)
    {
        return Unexpected<Error>{entries.error()};
    }
    std::size_t count = 0;
    for (const FileSystem::DirectoryEntry& entry : *entries)
    {
        const std::string path = std::format("{}/{}", folder, entry.name);
        if (entry.isDirectory)
        {
            Result<std::size_t> nested = scanFolder(path);
            if (!nested)
            {
                return nested;
            }
            count += *nested;
        }
        else if (path.ends_with(c_metaExtension))
        {
            Result<void> registered = registerMeta(path);
            if (!registered)
            {
                return Unexpected<Error>{registered.error()};
            }
            ++count;
        }
    }
    return count;
}

Result<void> AssetManager::registerMeta(const std::string& metaPath)
{
    Result<std::string> text = m_files.readText(metaPath);
    if (!text)
    {
        return Unexpected<Error>{text.error()};
    }
    Result<AssetId> id = readMetaId(*text, metaPath);
    if (!id)
    {
        return Unexpected<Error>{id.error()};
    }
    AssetRecord record{*id, metaPath.substr(0, metaPath.size() - c_metaExtension.size()), metaPath};

    const std::unique_lock lock{m_mutex};
    const auto existing = m_records.find(*id);
    // Same path: a rescan. Another path whose .meta still exists: a copied file. Otherwise the asset was moved.
    if (existing != m_records.end() && existing->second.metaPath != metaPath &&
        m_files.exists(existing->second.metaPath))
    {
        return fail(ErrorCode::AlreadyExists,
                    std::format("{} and {} have the same ID", existing->second.metaPath, metaPath));
    }
    m_records.insert_or_assign(*id, std::move(record));
    return {};
}

Result<AssetId> AssetManager::importSource(std::string_view sourcePath)
{
    if (!m_files.exists(sourcePath))
    {
        return fail(ErrorCode::NotFound, std::string{sourcePath});
    }
    const std::string metaPath = std::format("{}{}", sourcePath, c_metaExtension);
    if (!m_files.exists(metaPath))
    {
        const AssetId id = m_ids.next<IdKind::Asset>();
        Result<void> written = m_files.writeText(metaPath, std::format("id = \"{}\"\n", id.uuid()));
        if (!written)
        {
            return Unexpected<Error>{written.error()};
        }
    }
    Result<void> registered = registerMeta(metaPath);
    if (!registered)
    {
        return Unexpected<Error>{registered.error()};
    }
    const std::optional<AssetId> id = findBySource(sourcePath);
    MEOW2_ASSERT(id.has_value(), "just registered");
    return *id;
}

std::optional<AssetRecord> AssetManager::find(AssetId id) const
{
    const std::shared_lock lock{m_mutex};
    const auto found = m_records.find(id);
    if (found == m_records.end())
    {
        return std::nullopt;
    }
    return found->second;
}

std::optional<AssetId> AssetManager::findBySource(std::string_view sourcePath) const
{
    const std::shared_lock lock{m_mutex};
    for (const auto& [id, record] : m_records)
    {
        if (record.sourcePath == sourcePath)
        {
            return id;
        }
    }
    return std::nullopt;
}

std::vector<AssetRecord> AssetManager::records() const
{
    const std::shared_lock lock{m_mutex};
    std::vector<AssetRecord> result;
    result.reserve(m_records.size());
    for (const auto& [id, record] : m_records)
    {
        result.push_back(record);
    }
    return result;
}

std::shared_ptr<void> AssetManager::findSlot(AssetId id, const void* type) const
{
    const std::shared_lock lock{m_mutex};
    const auto found = m_loaded.find(id);
    if (found == m_loaded.end())
    {
        return nullptr;
    }
    MEOW2_ASSERT(found->second.type == type, "an asset is always loaded as the same type");
    return found->second.slot;
}

std::shared_ptr<void> AssetManager::insertSlot(AssetId id, const void* type, std::shared_ptr<void> slot)
{
    const std::unique_lock lock{m_mutex};
    // If another thread finished loading the same asset first, its slot wins and ours is dropped.
    const auto [entry, inserted] = m_loaded.try_emplace(id, LoadedAsset{type, std::move(slot)});
    MEOW2_ASSERT(entry->second.type == type, "an asset is always loaded as the same type");
    return entry->second.slot;
}
} // namespace Meow2::Assets
