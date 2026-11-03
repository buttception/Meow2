/// @file
/// @brief Scene (Scene.h).

#include "Meow2/Scene/Scene.h"

#include "Meow2/Core/Assert.h"

#include <format>

namespace Meow2::Scene
{
Scene::Scene(UuidGenerator& ids, std::pmr::memory_resource& upstream)
    : m_ids(ids)
    , m_memory(upstream, Memory::MemTag::Scene)
    , m_registry(Registry::allocator_type{&m_memory}) // EnTT and polymorphic_allocator clash: ResourceAllocator.h
    , m_entitiesById(&m_memory)
    , m_owner(std::this_thread::get_id())
{
    registerComponent<IdComponent>();
    registerComponent<NameComponent>();
}

entt::entity Scene::create(std::string_view name)
{
    return createEntity(m_ids.next<IdKind::Entity>(), name);
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
    return createEntity(id, name);
}

entt::entity Scene::createEntity(EntityId id, std::string_view name)
{
    assertOwnerThread();
    const entt::entity entity = m_registry.create();
    m_registry.emplace<IdComponent>(entity, id);
    if (!name.empty())
    {
        m_registry.emplace<NameComponent>(entity, std::string{name});
    }
    m_entitiesById.emplace(id, entity);
    return entity;
}

void Scene::destroy(entt::entity entity)
{
    assertOwnerThread();
    MEOW2_ASSERT(contains(entity), "destroying an entity that isn't in the scene");
    m_entitiesById.erase(m_registry.get<IdComponent>(entity).id);
    m_registry.destroy(entity);
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
    return m_registry.get<IdComponent>(entity).id;
}

std::size_t Scene::entityCount() const
{
    return m_entitiesById.size();
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
                 "structural scene changes belong on the scene's thread (jobs use command buffers)");
}
} // namespace Meow2::Scene
