#pragma once

/// @file
/// @brief Scene: the entities and components of one world, on EnTT, with a saved EntityId for every entity that
///        needs one, and per-thread command buffers for structural changes from jobs.
///
/// EnTT types are part of this API by design (techstack.md §25): systems iterate `scene.registry().view<…>()`
/// directly. Scene adds what EnTT doesn't have: stable IDs, components registered up front, command buffers, and
/// memory counted under MemTag::Scene.
/// @code
/// Meow2::Scene::Scene scene{ids, Meow2::Memory::globalTracker(), jobs.threadCount()};
/// scene.registerComponent<Health>();                         // at startup, before any job touches the scene
/// const entt::entity player = scene.create("Player");        // saved: has an EntityId
/// scene.registry().emplace<Health>(player, 100);
///
/// // in a job: no structural change directly, record it
/// scene.commands(jobs.threadIndex()).spawnTransient(Bullet{}, Velocity{});
/// // on the main thread, once per frame:
/// scene.applyCommands();
/// @endcode
/// @see techstack.md §25, docs/wiki/Scene.md

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Memory/ResourceAllocator.h>
#include <Meow2/Core/Memory/TaggedResource.h>
#include <Meow2/Core/Result.h>

#include <entt/container/dense_map.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Meow2::Scene
{
/// @brief A saved entity's identity. Added by Scene::create() and SceneCommands::spawn(). Treat it as read-only:
///        removing it makes the entity transient; changing it in place isn't tracked. Plain data.
struct IdComponent
{
    EntityId id{}; ///< The entity's ID, unique across runs and machines.
};

/// @brief A human-readable name, e.g. for the editor's hierarchy and logs. Optional. Plain data.
struct NameComponent
{
    std::string name{}; ///< The name (UTF-8); doesn't have to be unique.
};

class SceneCommands;

/// @brief One world of entities and components.
///
/// - **Two identities:** `entt::entity` is the runtime handle systems use (32 bits, reused after destruction).
///   EntityId (IdComponent) is the saved identity used by scene files, prefabs, undo, the editor and MCP; find() maps
///   it back to the current handle. **Transient** entities (createTransient(), e.g. bullets and effects) have no
///   EntityId: they're never saved or referenced, and cost nothing extra.
/// - **The ID map is always right:** EnTT signals on IdComponent keep it in sync, so it doesn't matter whether an
///   entity is destroyed through destroy(), `registry().destroy()` or a command buffer.
/// - **Components are plain data structs.** Register each type once at startup with registerComponent(): EnTT would
///   otherwise create its storage on first use, and a first use on a job thread is a data race.
/// - **Command buffers:** jobs record structural changes (create, destroy, add/remove components) in their thread's
///   SceneCommands; applyCommands() performs them on the owner thread at a fixed point in the frame.
/// - **Memory:** the registry and the ID map allocate through a TaggedResource, so all of it is counted under
///   MemTag::Scene.
/// @par Thread safety
/// Not thread-safe; owned by the thread that created it (the main thread). Structural calls assert that. Jobs may read
/// and write components of *different* entities or *different* component types at the same time while nothing
/// changes structure, and record structural changes with commands(). See techstack.md §25.
/// @par Lifetime
/// The UuidGenerator and the upstream memory resource must outlive the Scene.
class Scene
{
public:
    /// @brief The EnTT registry type: a standard registry whose storage allocates through the Scene's memory.
    using Registry = entt::basic_registry<entt::entity, Memory::ResourceAllocator<entt::entity>>;

    /// @brief An empty scene. Records the calling thread as its owner.
    /// @param ids         Makes the EntityIds of new entities (thread-safe, so jobs can spawn). Must outlive the Scene.
    /// @param upstream    Where the scene's memory comes from (counted under MemTag::Scene). Must outlive the Scene.
    /// @param threadCount How many command buffers to keep: the JobSystem's threadCount(), so every job thread has one.
    /// @pre `threadCount > 0` (asserted).
    Scene(UuidGenerator& ids, std::pmr::memory_resource& upstream, std::uint32_t threadCount = 1);

    /// @brief Destroys every entity. Commands not applied yet are dropped.
    ~Scene();

    Scene(const Scene&) = delete;            ///< Not copyable: one world, one registry.
    Scene& operator=(const Scene&) = delete; ///< Not copyable: one world, one registry.
    Scene(Scene&&) = delete;                 ///< Not movable: its containers and signals point at it.
    Scene& operator=(Scene&&) = delete;      ///< Not movable: its containers and signals point at it.

    /// @brief Creates a saved entity with a new EntityId.
    /// @param name Optional name; with an empty one no NameComponent is added.
    /// @return The new entity's handle.
    entt::entity create(std::string_view name = {});

    /// @brief Creates a saved entity with a known EntityId, e.g. when loading a scene.
    /// @param id   The saved ID.
    /// @param name Optional name; with an empty one no NameComponent is added.
    /// @return The handle, or ErrorCode::InvalidArgument for an invalid ID, or ErrorCode::AlreadyExists if an entity
    ///         with this ID is in the scene.
    [[nodiscard]] Result<entt::entity> createWithId(EntityId id, std::string_view name = {});

    /// @brief Creates an entity without an EntityId: never saved or referenced by ID (bullets, particles, effects).
    /// @param name Optional name; with an empty one no NameComponent is added.
    /// @return The new entity's handle.
    entt::entity createTransient(std::string_view name = {});

    /// @brief Destroys an entity and all its components. Its handle may be reused by a later create().
    /// @param entity The entity.
    /// @pre `contains(entity)` (asserted).
    void destroy(entt::entity entity);

    /// @brief Whether a handle refers to a live entity of this scene.
    /// @param entity The handle.
    /// @return `false` for destroyed entities and `entt::null`.
    [[nodiscard]] bool contains(entt::entity entity) const;

    /// @brief The current handle of a saved entity, by its ID.
    /// @param id The ID.
    /// @return The handle, or empty if no live entity has this ID.
    [[nodiscard]] std::optional<entt::entity> find(EntityId id) const;

    /// @brief An entity's saved ID.
    /// @param entity The entity.
    /// @pre `contains(entity)` (asserted).
    /// @return Its EntityId, or an invalid one for a transient entity.
    [[nodiscard]] EntityId idOf(entt::entity entity) const;

    /// @brief How many entities are alive, saved and transient.
    /// @return The count.
    [[nodiscard]] std::size_t entityCount() const;

    /// @brief How many alive entities have an EntityId.
    /// @return The count.
    [[nodiscard]] std::size_t persistentCount() const;

    /// @brief Creates the storage for component type `T` now, so jobs never trigger its creation.
    /// @tparam T A component (plain data struct).
    template <class T>
    void registerComponent()
    {
        assertOwnerThread();
        static_cast<void>(m_registry.storage<T>());
    }

    /// @brief Whether `T`'s storage exists (registerComponent() was called, or `T` was used on the owner thread).
    /// @tparam T A component.
    /// @return `true` if using `T` can't create a pool. Safe to call from jobs while nothing changes structure.
    template <class T>
    [[nodiscard]] bool isRegistered() const
    {
        return std::as_const(m_registry).template storage<T>() != nullptr;
    }

    /// @brief The command buffer of a job thread, for structural changes from jobs.
    /// @param threadIndex `JobSystem::threadIndex()` of the calling thread (0 = the main thread).
    /// @pre `threadIndex < threadCount` given at construction (asserted).
    /// @return That thread's buffer. Only that thread may record into it.
    [[nodiscard]] SceneCommands& commands(std::uint32_t threadIndex);

    /// @brief Performs every recorded command, buffer by buffer in thread order, each in recording order, then empties
    ///        the buffers. Call on the owner thread while no job records, e.g. once after the fixed steps.
    ///
    /// Commands for an entity that is gone by then (destroyed earlier in the same apply, or before) are skipped.
    void applyCommands();

    /// @brief The EnTT registry, for views and component access.
    /// @return The registry.
    /// @warning Don't create entities with `registry().create()`: they'd have no EntityId (use createTransient()).
    [[nodiscard]] Registry& registry();

    /// @brief The EnTT registry, read-only.
    /// @return The registry.
    [[nodiscard]] const Registry& registry() const;

private:
    friend class SceneCommands;

    /// @brief EntityId → handle; `entt::dense_map` (open addressing: no allocation per entity).
    using IdMap = entt::dense_map<EntityId, entt::entity, std::hash<EntityId>, std::equal_to<>,
                                  Memory::ResourceAllocator<std::pair<const EntityId, entt::entity>>>;

    void assertOwnerThread() const;
    entt::entity createEntity(const EntityId* id, std::string_view name);
    void onIdAdded(Registry& registry, entt::entity entity);
    void onIdRemoved(Registry& registry, entt::entity entity);

    UuidGenerator& m_ids;
    Memory::TaggedResource m_memory; // before everything that allocates from it
    IdMap m_entitiesById;            // before the registry: the registry's signals update it until the registry is gone
    Registry m_registry;
    std::vector<SceneCommands> m_commands;
    std::thread::id m_owner;
};

/// @brief One job thread's list of structural changes for a Scene, performed later by Scene::applyCommands().
///
/// Get it with `scene.commands(jobs.threadIndex())`. Recording only appends to this thread's list, so jobs on
/// different threads record at the same time without locks. Everything is performed in recording order.
/// Components must be registered (Scene::registerComponent()) and copyable.
/// @par Thread safety
/// One thread per buffer: the thread whose index it belongs to. Scene::applyCommands() runs on the Scene's thread
/// while no job records.
class SceneCommands
{
public:
    /// @brief An empty buffer for `scene`. Made by the Scene.
    /// @param scene The scene the commands are for.
    explicit SceneCommands(Scene& scene);

    /// @brief Records the creation of a saved entity with components.
    /// @tparam Components Component types (registered, copyable).
    /// @param name       Optional name.
    /// @param components The components it starts with.
    /// @return Its EntityId, valid now; the entity appears at the next Scene::applyCommands().
    template <class... Components>
    EntityId spawn(std::string_view name, Components... components)
    {
        assertRegistered<Components...>();
        const EntityId id = m_scene->m_ids.template next<IdKind::Entity>(); // the generator is thread-safe
        m_commands.emplace_back(
            [id, savedName = std::string{name}, ... values = std::move(components)](Scene& scene) mutable
            {
                const entt::entity entity = scene.createEntity(&id, savedName);
                (scene.m_registry.template emplace<Components>(entity, std::move(values)), ...);
            });
        return id;
    }

    /// @brief Records the creation of a transient entity (no EntityId) with components, e.g. a bullet.
    /// @tparam Components Component types (registered, copyable).
    /// @param components The components it starts with.
    template <class... Components>
    void spawnTransient(Components... components)
    {
        assertRegistered<Components...>();
        m_commands.emplace_back(
            [... values = std::move(components)](Scene& scene) mutable
            {
                const entt::entity entity = scene.createEntity(nullptr, {});
                (scene.m_registry.template emplace<Components>(entity, std::move(values)), ...);
            });
    }

    /// @brief Records the destruction of an entity. Skipped if it's already gone at apply time.
    /// @param entity The entity.
    void destroy(entt::entity entity);

    /// @brief Records adding (or replacing) a component. Skipped if the entity is gone at apply time.
    /// @tparam T         The component type (registered, copyable).
    /// @param entity    The entity.
    /// @param component The value.
    template <class T>
    void emplace(entt::entity entity, T component)
    {
        assertRegistered<T>();
        m_commands.emplace_back(
            [entity, value = std::move(component)](Scene& scene) mutable
            {
                if (scene.contains(entity))
                {
                    scene.m_registry.template emplace_or_replace<T>(entity, std::move(value));
                }
            });
    }

    /// @brief Records removing a component (nothing happens if the entity doesn't have it, or is gone).
    /// @tparam T      The component type (registered).
    /// @param entity The entity.
    template <class T>
    void remove(entt::entity entity)
    {
        assertRegistered<T>();
        m_commands.emplace_back(
            [entity](Scene& scene)
            {
                if (scene.contains(entity))
                {
                    scene.m_registry.template remove<T>(entity);
                }
            });
    }

    /// @brief How many commands are waiting for Scene::applyCommands().
    /// @return The count.
    [[nodiscard]] std::size_t size() const;

private:
    friend class Scene;

    template <class... Components>
    void assertRegistered() const
    {
        // A command for an unregistered type would create its pool at apply time; that's fine there, but it means the
        // type is also used by jobs without a pool, which is the race registerComponent() exists to prevent.
        MEOW2_ASSERT((m_scene->template isRegistered<Components>() && ...),
                     "register component types with Scene::registerComponent() at startup");
    }

    Scene* m_scene;                                      // never null; outlives the buffer
    std::vector<std::function<void(Scene&)>> m_commands; // in recording order
};
} // namespace Meow2::Scene
