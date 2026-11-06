#pragma once

/// @file
/// @brief AssetManager: finds assets by their stable AssetId and loads them once, into shared, typed handles.
///
/// An asset is a source file, a `.meta` sidecar next to it (`Sky.hlsl` + `Sky.hlsl.meta`) that holds its AssetId and
/// import settings, and a cooked file named by that ID. The ID stays with the `.meta`, so moving a source keeps every
/// reference to it working.
/// @code
/// Meow2::Assets::AssetManager assets{files, ids, log};           // composition root
/// assets.scan("assets");                                         // read every .meta below a folder
/// Meow2::Result<Meow2::Assets::AssetHandle<Texture>> texture = assets.load<Texture>(id);
/// std::shared_ptr<const Texture> pixels = texture->get();
/// @endcode
/// A loadable type `T` provides `static Result<T> loadAsset(AssetManager& assets, AssetId id)`. There's no asset base
/// class and no virtual calls (techstack.md §3).
/// @see techstack.md §13a, §24, docs/wiki/Assets.md

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Meow2::FileSystem
{
class FileSystem;
}

namespace Meow2::Log
{
class Logger;
}

namespace Meow2::Assets
{
/// @brief Where an asset lives. Plain data.
struct AssetRecord
{
    AssetId id;             ///< Its stable ID.
    std::string sourcePath; ///< Virtual path of the source file.
    std::string metaPath;   ///< Virtual path of its `.meta` sidecar.

    /// @brief Records are equal when all fields are.
    friend bool operator==(const AssetRecord&, const AssetRecord&) = default;
};

/// @cond
namespace Detail
{
/// @brief One loaded asset, shared by its handles, so a reload reaches all of them.
template <class T>
struct AssetSlot
{
    mutable std::mutex mutex;
    std::shared_ptr<const T> value;
    std::uint32_t version = 1;
};

/// @brief A unique address per type: identifies T without RTTI.
template <class T>
inline constexpr char c_assetType = 0;
} // namespace Detail
/// @endcond

/// @brief A shared reference to a loaded asset. Cheap to copy.
/// @tparam T The asset's type.
/// @par Thread safety
/// Any thread may use a handle. get() returns the current value; a reload replaces it for later get() calls, while
/// values already returned stay valid.
template <class T>
class AssetHandle
{
public:
    /// @brief An empty handle.
    AssetHandle() = default;

    /// @brief Whether the handle refers to an asset.
    /// @return `false` for a default-constructed handle.
    [[nodiscard]] bool isValid() const
    {
        return m_slot != nullptr;
    }

    /// @brief The asset as it is now.
    /// @return The value; keep it only as long as needed, so a reload can free the old one.
    /// @pre isValid() (asserted by the null dereference in Debug).
    [[nodiscard]] std::shared_ptr<const T> get() const
    {
        const std::scoped_lock lock{m_slot->mutex};
        return m_slot->value;
    }

    /// @brief How many times the asset has been loaded: 1, then +1 per successful reload.
    /// @return The version; compare it to notice a reload.
    [[nodiscard]] std::uint32_t version() const
    {
        const std::scoped_lock lock{m_slot->mutex};
        return m_slot->version;
    }

private:
    friend class AssetManager;
    explicit AssetHandle(std::shared_ptr<Detail::AssetSlot<T>> slot)
        : m_slot(std::move(slot))
    {
    }

    std::shared_ptr<Detail::AssetSlot<T>> m_slot;
};

/// @brief Knows where every asset is, and keeps one loaded copy of each.
///
/// @par Thread safety
/// Thread-safe: jobs may load assets at once. The tables are protected by a shared mutex that isn't held while a file
/// is read or an asset is built. Two threads loading the same asset may both build it; the first one to finish wins.
/// @par Lifetime
/// The FileSystem, UuidGenerator and Logger must outlive it. Handles and values stay valid after it's gone.
class AssetManager
{
public:
    /// @brief An empty manager.
    /// @param files Reads and writes assets. Must outlive the manager.
    /// @param ids   Makes the IDs of new assets. Must outlive the manager.
    /// @param log   Receives what scan() finds. Must outlive the manager.
    AssetManager(FileSystem::FileSystem& files, UuidGenerator& ids, Log::Logger& log);

    /// @brief Registers every `.meta` below a folder.
    ///
    /// Scanning again picks up new and moved files.
    /// @param folder Virtual path of the folder; subfolders are included.
    /// @return How many assets were found; ErrorCode::ParseError for a broken `.meta`; ErrorCode::AlreadyExists when
    ///         two `.meta` files hold the same ID (a copied file), which must never go unnoticed.
    [[nodiscard]] Result<std::size_t> scan(std::string_view folder);

    /// @brief Registers one source file, creating its `.meta` with a new ID if it has none.
    /// @param sourcePath Virtual path of the source; its mount must be writable if a `.meta` has to be created.
    /// @return The asset's ID, the existing one if it has a `.meta` already.
    [[nodiscard]] Result<AssetId> importSource(std::string_view sourcePath);

    /// @brief Looks up an asset.
    /// @param id The asset's ID.
    /// @return Its record, or `std::nullopt` if it isn't registered (normal in a packaged game: cooked files have no
    ///         sources).
    [[nodiscard]] std::optional<AssetRecord> find(AssetId id) const;

    /// @brief Looks up an asset by its source file.
    /// @param sourcePath Virtual path of the source.
    /// @return Its ID, or `std::nullopt`.
    [[nodiscard]] std::optional<AssetId> findBySource(std::string_view sourcePath) const;

    /// @brief Every registered asset.
    /// @return A copy of the records, in no particular order.
    [[nodiscard]] std::vector<AssetRecord> records() const;

    /// @brief The file system, for `loadAsset()` functions.
    /// @return The FileSystem given to the constructor.
    [[nodiscard]] FileSystem::FileSystem& files() const
    {
        return m_files;
    }

    /// @brief Returns the loaded asset, loading it if needed. Memory is counted under MemTag::Assets.
    /// @tparam T The asset's type, with a `loadAsset()` function.
    /// @param id The asset's ID.
    /// @return A handle, or whatever error `T::loadAsset()` returned.
    /// @pre An ID is always loaded as the same `T` (asserted).
    template <class T>
    [[nodiscard]] Result<AssetHandle<T>> load(AssetId id)
    {
        if (std::shared_ptr<void> existing = findSlot(id, &Detail::c_assetType<T>))
        {
            return AssetHandle<T>{std::static_pointer_cast<Detail::AssetSlot<T>>(std::move(existing))};
        }
        const Memory::MemTagScope tag{Memory::MemTag::Assets};
        Result<T> value = T::loadAsset(*this, id);
        if (!value)
        {
            return Unexpected<Error>{value.error()};
        }
        auto slot = std::make_shared<Detail::AssetSlot<T>>();
        slot->value = std::make_shared<const T>(std::move(*value));
        return AssetHandle<T>{
            std::static_pointer_cast<Detail::AssetSlot<T>>(insertSlot(id, &Detail::c_assetType<T>, std::move(slot)))};
    }

    /// @brief Loads an asset again; every handle sees the new value. On failure the old value stays.
    /// @tparam T The asset's type.
    /// @param id The asset's ID; loads it if it wasn't loaded yet.
    /// @return Nothing, or the error from `T::loadAsset()`.
    template <class T>
    [[nodiscard]] Result<void> reload(AssetId id)
    {
        std::shared_ptr<void> existing = findSlot(id, &Detail::c_assetType<T>);
        if (existing == nullptr)
        {
            Result<AssetHandle<T>> loaded = load<T>(id);
            if (!loaded)
            {
                return Unexpected<Error>{loaded.error()};
            }
            return {};
        }
        const Memory::MemTagScope tag{Memory::MemTag::Assets};
        Result<T> value = T::loadAsset(*this, id);
        if (!value)
        {
            return Unexpected<Error>{value.error()};
        }
        auto fresh = std::make_shared<const T>(std::move(*value));
        auto* slot = static_cast<Detail::AssetSlot<T>*>(existing.get());
        const std::scoped_lock lock{slot->mutex};
        slot->value = std::move(fresh);
        ++slot->version;
        return {};
    }

private:
    /// @brief A loaded asset of unknown type.
    struct LoadedAsset
    {
        const void* type = nullptr; ///< Detail::c_assetType of its type.
        std::shared_ptr<void> slot; ///< The Detail::AssetSlot.
    };

    [[nodiscard]] std::shared_ptr<void> findSlot(AssetId id, const void* type) const;
    [[nodiscard]] std::shared_ptr<void> insertSlot(AssetId id, const void* type, std::shared_ptr<void> slot);
    [[nodiscard]] Result<void> registerMeta(const std::string& metaPath);
    [[nodiscard]] Result<std::size_t> scanFolder(const std::string& folder);

    FileSystem::FileSystem& m_files;
    UuidGenerator& m_ids;
    Log::Logger& m_log;
    mutable std::shared_mutex m_mutex; ///< Guards the two tables below; never held during I/O.
    std::unordered_map<AssetId, AssetRecord> m_records;
    std::unordered_map<AssetId, LoadedAsset> m_loaded;
};
} // namespace Meow2::Assets
