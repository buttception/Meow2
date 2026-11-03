/// @file
/// @brief Scene and SceneCommands (Scene.h).

#include "Meow2/Scene/Scene.h"

#include <format>

namespace Meow2::Scene
{
Scene::Scene(UuidGenerator& ids, std::pmr::memory_resource& upstream, std::uint32_t threadCount)
    : m_ids(ids)
    , m_memory(upstream, Memory::MemTag::Scene)
    , m_entitiesById(IdMap::allocator_type{&m_memory})
    , m_registry(Registry::allocator_type{&m_memory}) // EnTT and polymorphic_allocator clash: ResourceAllocator.h
    , m_owner(std::this_thread::get_id())
{
    MEOW2_ASSERT(threadCount > 0, "a scene needs at least one command buffer");
    m_commands.reserve(threadCount);
    for (std::uint32_t i = 0; i < threadCount; ++i)
    {
        m_commands.emplace_back(*this);
    }
    registerComponent<IdComponent>();
    registerComponent<NameComponent>();
    // The ID map follows IdComponent wherever it's added or removed, so it can't go stale whichever way an entity is
    // destroyed (destroy(), registry().destroy(), a command) or loses its ID.
    m_registry.on_construct<IdComponent>().connect<&Scene::onIdAdded>(*this);
    m_registry.on_destroy<IdComponent>().connect<&Scene::onIdRemoved>(*this);
}

Scene::~Scene()
{
    // Disconnect first: the registry's own destruction mustn't call back into a half-destroyed Scene.
    m_registry.on_construct<IdComponent>().disconnect(this); // every connection made with this instance
    m_registry.on_destroy<IdComponent>().disconnect(this);
}

entt::entity Scene::create(std::string_view name)
{
    const EntityId id = m_ids.next<IdKind::Entity>();
    return createEntity(&id, name);
}

Result<entt::entity> Scene::createWithId(EntityId id, std::string_view name)
{
    if (!id.isValid())
    {
        return fail(ErrorCode::InvalidArgument, "an entity needs a valid EntityId");
    }
    if (m_entitiesById.contains(id))
    {
        return fail(ErrorCode::AlreadyExists, std::format("entity {} is already in the scene", id.uuid()));
    }
    return createEntity(&id, name);
}

entt::entity Scene::createTransient(std::string_view name)
{
    return createEntity(nullptr, name);
}

entt::entity Scene::createEntity(const EntityId* id, std::string_view name)
{
    assertOwnerThread();
    const entt::entity entity = m_registry.create();
    if (id != nullptr)
    {
        m_registry.emplace<IdComponent>(entity, *id); // onIdAdded records it in the map
    }
    if (!name.empty())
    {
        m_registry.emplace<NameComponent>(entity, std::string{name});
    }
    return entity;
}

void Scene::onIdAdded(Registry& registry, entt::entity entity)
{
    const EntityId id = registry.get<IdComponent>(entity).id;
    const bool inserted = m_entitiesById.emplace(id, entity).second;
    MEOW2_ASSERT(inserted, "two entities with the same EntityId");
    static_cast<void>(inserted); // only checked by the assert
}

void Scene::onIdRemoved(Registry& registry, entt::entity entity)
{
    m_entitiesById.erase(registry.get<IdComponent>(entity).id); // called before the component is gone
}

void Scene::destroy(entt::entity entity)
{
    assertOwnerThread();
    MEOW2_ASSERT(contains(entity), "destroying an entity that isn't in the scene");
    m_registry.destroy(entity); // onIdRemoved updates the map
}

bool Scene::contains(entt::entity entity) const
{
    return m_registry.valid(entity);
}

std::optional<entt::entity> Scene::find(EntityId id) const
{
    const auto found = m_entitiesById.find(id);
    return found != m_entitiesById.end() ? std::optional{found->second} : std::nullopt;
}

EntityId Scene::idOf(entt::entity entity) const
{
    MEOW2_ASSERT(contains(entity), "an entity that isn't in the scene has no ID here");
    const IdComponent* idComponent = m_registry.try_get<IdComponent>(entity);
    return idComponent != nullptr ? idComponent->id : EntityId{};
}

std::size_t Scene::entityCount() const
{
    return m_registry.storage<entt::entity>()->free_list(); // EnTT keeps live entities at the front of this storage
}

std::size_t Scene::persistentCount() const
{
    return m_entitiesById.size();
}

SceneCommands& Scene::commands(std::uint32_t threadIndex)
{
    MEOW2_ASSERT(threadIndex < m_commands.size(), "a thread index without a command buffer (see threadCount)");
    return m_commands[threadIndex];
}

void Scene::applyCommands()
{
    assertOwnerThread();
    for (SceneCommands& buffer : m_commands)
    {
        // Take the list out first, so the buffer is empty and reusable even while its commands run.
        std::vector<std::function<void(Scene&)>> commands;
        commands.swap(buffer.m_commands);
        for (std::function<void(Scene&)>& command : commands)
        {
            command(*this);
        }
        if (buffer.m_commands.empty())
        {
            commands.clear();
            buffer.m_commands.swap(commands); // keep the capacity for the next frame
        }
    }
}

Scene::Registry& Scene::registry()
{
    return m_registry;
}

const Scene::Registry& Scene::registry() const
{
    return m_registry;
}

void Scene::assertOwnerThread() const
{
    MEOW2_ASSERT(std::this_thread::get_id() == m_owner,
                 "structural scene changes belong on the scene's thread (jobs use commands())");
}

SceneCommands::SceneCommands(Scene& scene)
    : m_scene(&scene)
{
}

void SceneCommands::destroy(entt::entity entity)
{
    m_commands.emplace_back(
        [entity](Scene& scene)
        {
            if (scene.contains(entity))
            {
                scene.destroy(entity);
            }
        });
}

std::size_t SceneCommands::size() const
{
    return m_commands.size();
}
} // namespace Meow2::Scene
