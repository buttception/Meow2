#pragma once

/// @file
/// @brief Scene: the entities and components of one world, on EnTT, with a saved EntityId for every entity.
///
/// EnTT types are part of this API by design (techstack.md §25): systems iterate `scene.registry().view<…>()`
/// directly. Scene adds what EnTT doesn't have: a stable ID for every entity, components registered up front, and
/// memory counted under MemTag::Scene.
/// @code
/// Meow2::Scene::Scene scene{ids, Meow2::Memory::globalTracker()};
/// scene.registerComponent<Health>();                         // at startup, before any job touches the scene
/// const entt::entity player = scene.create("Player");
/// scene.registry().emplace<Health>(player, 100);
/// for (auto [entity, health] : scene.registry().view<Health>().each()) { /* ... */ }
/// @endcode
/// @see techstack.md §25, docs/wiki/Scene.md

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Memory/ResourceAllocator.h>
#include <Meow2/Core/Memory/TaggedResource.h>
#include <Meow2/Core/Result.h>

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace Meow2::Scene
{
/// @brief Every entity's saved identity. Added by Scene::create(); don't add or remove it yourself. Plain data.
struct IdComponent
{
    EntityId id{}; ///< The entity's ID, unique across runs and machines.
};

/// @brief A human-readable name, e.g. for the editor's hierarchy and logs. Optional. Plain data.
struct NameComponent
{
    std::string name{}; ///< The name (UTF-8); doesn't have to be unique.
};

/// @brief One world of entities and components.
///
/// - **Two identities per entity:** `entt::entity` is the runtime handle systems use (32 bits, reused after
///   destruction); EntityId (IdComponent) is the saved identity used by scene files, prefabs, undo, the editor and
///   MCP. find() maps an EntityId back to the current handle.
/// - **Components are plain data structs.** Register each type once at startup with registerComponent(): EnTT would
///   otherwise create its storage on first use, and a first use on a job thread is a data race.
/// - **Memory:** the registry and the ID map allocate through a TaggedResource, so all of it is counted under
///   MemTag::Scene.
/// @par Thread safety
/// Not thread-safe; owned by the main thread. Creating and destroying entities and registering components assert
/// that they run on the thread that created the Scene. Jobs may read and write components of *different* entities or
/// *different* component types at the same time, as long as nothing changes structure meanwhile (techstack.md §25);
/// structural changes from jobs come with command buffers.
/// @par Lifetime
/// The UuidGenerator and the upstream memory resource must outlive the Scene.
class Scene
{
public:
    /// @brief The EnTT registry type: a standard registry whose storage allocates through the Scene's memory.
    using Registry = entt::basic_registry<entt::entity, Memory::ResourceAllocator<entt::entity>>;

    /// @brief An empty scene. Records the calling thread as its owner.
    /// @param ids      Makes the EntityIds of new entities. Must outlive the Scene.
    /// @param upstream Where the scene's memory comes from (counted under MemTag::Scene). Must outlive the Scene.
    Scene(UuidGenerator& ids, std::pmr::memory_resource& upstream);

    Scene(const Scene&) = delete;            ///< Not copyable: one world, one registry.
    Scene& operator=(const Scene&) = delete; ///< Not copyable: one world, one registry.
    Scene(Scene&&) = delete;                 ///< Not movable: its containers point at its memory resource.
    Scene& operator=(Scene&&) = delete;      ///< Not movable: its containers point at its memory resource.

    /// @brief Creates an entity with a new EntityId.
    /// @param name Optional name; with an empty one no NameComponent is added.
    /// @return The new entity's handle.
    entt::entity create(std::string_view name = {});

    /// @brief Creates an entity with a known EntityId, e.g. when loading a saved scene.
    /// @param id   The saved ID.
    /// @param name Optional name; with an empty one no NameComponent is added.
    /// @return The handle, or ErrorCode::InvalidArgument for an invalid ID, or ErrorCode::AlreadyExists if an entity
    ///         with this ID is in the scene.
    [[nodiscard]] Result<entt::entity> createWithId(EntityId id, std::string_view name = {});

    /// @brief Destroys an entity and all its components. Its handle may be reused by a later create().
    /// @param entity The entity.
    /// @pre `contains(entity)` (asserted).
    void destroy(entt::entity entity);

    /// @brief Whether a handle refers to a live entity of this scene.
    /// @param entity The handle.
    /// @return `false` for destroyed entities and `entt::null`.
    [[nodiscard]] bool contains(entt::entity entity) const;

    /// @brief The current handle of an entity, by its saved ID.
    /// @param id The ID.
    /// @return The handle, or empty if no entity has this ID.
    [[nodiscard]] std::optional<entt::entity> find(EntityId id) const;

    /// @brief An entity's saved ID.
    /// @param entity The entity.
    /// @pre `contains(entity)` (asserted).
    /// @return Its EntityId.
    [[nodiscard]] EntityId idOf(entt::entity entity) const;

    /// @brief How many entities are alive.
    /// @return The count.
    [[nodiscard]] std::size_t entityCount() const;

    /// @brief Creates the storage for component type `T` now, so jobs never trigger its creation.
    /// @tparam T A component (plain data struct).
    template <class T>
    void registerComponent()
    {
        assertOwnerThread();
        static_cast<void>(m_registry.storage<T>());
    }

    /// @brief The EnTT registry, for views and component access.
    /// @return The registry.
    /// @warning Create and destroy entities through the Scene (create(), destroy()), never with `registry().create()`
    ///          or `registry().destroy()`: those would bypass the EntityId map.
    [[nodiscard]] Registry& registry();

    /// @brief The EnTT registry, read-only.
    /// @return The registry.
    [[nodiscard]] const Registry& registry() const;

private:
    void assertOwnerThread() const;
    entt::entity createEntity(EntityId id, std::string_view name);

    UuidGenerator& m_ids;
    Memory::TaggedResource m_memory; // declared before the containers that allocate from it
    Registry m_registry;
    std::pmr::unordered_map<EntityId, entt::entity> m_entitiesById;
    std::thread::id m_owner;
};
} // namespace Meow2::Scene
