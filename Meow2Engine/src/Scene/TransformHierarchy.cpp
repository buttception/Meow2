/// @file
/// @brief TransformHierarchy: depth levels, the parallel per-level update, and render interpolation.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <optional>
#include <utility>

namespace Meow2::Scene
{
namespace
{
/// @brief Smallest piece of a level worth a job: a world update is ~50 ns, so 256 entities are ~13 µs of work.
constexpr std::uint32_t c_minEntitiesPerJob = 256;
} // namespace

TransformHierarchy::TransformHierarchy(Scene& scene, Jobs::JobSystem& jobs)
    : m_scene(scene)
    , m_jobs(jobs)
{
    m_scene.registerComponent<LocalTransform>();
    m_scene.registerComponent<WorldTransform>();
    m_scene.registerComponent<PreviousWorldTransform>();
    m_scene.registerComponent<Parent>();
    m_scene.registerComponent<TransformState>();
    Scene::Registry& registry = m_scene.registry();
    registry.on_construct<LocalTransform>().connect<&TransformHierarchy::onStructureChanged>(*this);
    registry.on_destroy<LocalTransform>().connect<&TransformHierarchy::onStructureChanged>(*this);
    registry.on_construct<Parent>().connect<&TransformHierarchy::onStructureChanged>(*this);
    registry.on_update<Parent>().connect<&TransformHierarchy::onStructureChanged>(*this);
    registry.on_destroy<Parent>().connect<&TransformHierarchy::onStructureChanged>(*this);
}

TransformHierarchy::~TransformHierarchy()
{
    Scene::Registry& registry = m_scene.registry();
    registry.on_construct<LocalTransform>().disconnect(this);
    registry.on_destroy<LocalTransform>().disconnect(this);
    registry.on_construct<Parent>().disconnect(this);
    registry.on_update<Parent>().disconnect(this);
    registry.on_destroy<Parent>().disconnect(this);
}

void TransformHierarchy::onStructureChanged(Scene::Registry& /*registry*/, entt::entity /*entity*/)
{
    m_structureChanged = true;
}

void TransformHierarchy::add(entt::entity entity, const Math::Transform& local, entt::entity parent)
{
    Scene::Registry& registry = m_scene.registry();
    MEOW2_ASSERT(!registry.all_of<LocalTransform>(entity), "the entity already has a transform");
    MEOW2_ASSERT(parent == entt::null || registry.all_of<LocalTransform>(parent), "the parent needs a transform");
    // Until the first update the world transform is the local one; update() fixes it for children.
    const Math::Mat4 matrix = Math::toMat4(local);
    registry.emplace<LocalTransform>(entity, local);
    registry.emplace<WorldTransform>(entity, matrix);
    registry.emplace<PreviousWorldTransform>(entity, matrix);
    registry.emplace<TransformState>(entity);
    if (parent != entt::null)
    {
        registry.emplace<Parent>(entity, parent);
    }
}

void TransformHierarchy::setLocal(entt::entity entity, const Math::Transform& local)
{
    Scene::Registry& registry = m_scene.registry();
    registry.get<LocalTransform>(entity).value = local;
    registry.get<TransformState>(entity).dirty = true;
}

void TransformHierarchy::markDirty(entt::entity entity)
{
    m_scene.registry().get<TransformState>(entity).dirty = true;
}

void TransformHierarchy::setParent(entt::entity entity, entt::entity parent)
{
    Scene::Registry& registry = m_scene.registry();
    MEOW2_ASSERT(registry.all_of<LocalTransform>(entity), "the entity needs a transform");
    if (parent == entt::null)
    {
        registry.remove<Parent>(entity);
    }
    else
    {
        MEOW2_ASSERT(registry.all_of<LocalTransform>(parent), "the parent needs a transform");
        // Walk up from the new parent: meeting the entity itself means it would become its own ancestor.
        for (entt::entity ancestor = parent; ancestor != entt::null;)
        {
            MEOW2_ASSERT(ancestor != entity, "setParent would create a cycle");
            const Parent* up = registry.try_get<Parent>(ancestor);
            ancestor = up != nullptr ? up->entity : entt::null;
        }
        registry.emplace_or_replace<Parent>(entity, parent);
    }
    registry.get<TransformState>(entity).dirty = true;
}

void TransformHierarchy::rebuildLevels()
{
    MEOW2_PROFILE_ZONE();
    Scene::Registry& registry = m_scene.registry();

    // Children of destroyed parents become roots. (Removing Parent fires the signal again; it's reset below.)
    std::vector<entt::entity> orphans;
    for (const auto [entity, parent] : registry.storage<Parent>().each())
    {
        if (!registry.valid(parent.entity) || !registry.all_of<LocalTransform>(parent.entity))
        {
            orphans.push_back(entity);
        }
    }
    for (const entt::entity orphan : orphans)
    {
        registry.remove<Parent>(orphan);
        registry.get<TransformState>(orphan).dirty = true;
    }

    // Depth of every entity, memoised by entity index: each chain is walked once.
    constexpr std::uint32_t c_unknown = ~0U;
    std::vector<std::uint32_t> depthByIndex;
    const auto depthOf = [&](entt::entity start)
    {
        std::vector<entt::entity> chain;
        entt::entity current = start;
        std::uint32_t depth = 0;
        for (;;)
        {
            const auto index = static_cast<std::size_t>(entt::to_entity(current));
            if (index < depthByIndex.size() && depthByIndex[index] != c_unknown)
            {
                depth = depthByIndex[index];
                break;
            }
            chain.push_back(current);
            const Parent* up = registry.try_get<Parent>(current);
            if (up == nullptr)
            {
                depth = 0;
                chain.pop_back();
                if (index >= depthByIndex.size())
                {
                    depthByIndex.resize(index + 1, c_unknown);
                }
                depthByIndex[index] = 0;
                break;
            }
            current = up->entity;
        }
        // Unwind: the chain holds start … (the child of the known entity), deepest first.
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            ++depth;
            const auto index = static_cast<std::size_t>(entt::to_entity(*it));
            if (index >= depthByIndex.size())
            {
                depthByIndex.resize(index + 1, c_unknown);
            }
            depthByIndex[index] = depth;
        }
        return depthByIndex[static_cast<std::size_t>(entt::to_entity(start))];
    };

    for (std::vector<entt::entity>& level : m_levels)
    {
        level.clear();
    }
    for (const auto [entity, state] : registry.storage<TransformState>().each())
    {
        state.depth = depthOf(entity);
        if (state.depth >= m_levels.size())
        {
            m_levels.resize(state.depth + 1);
        }
        m_levels[state.depth].push_back(entity);
    }
    while (!m_levels.empty() && m_levels.back().empty())
    {
        m_levels.pop_back();
    }
    m_structureChanged = false;
}

void TransformHierarchy::update()
{
    MEOW2_PROFILE_ZONE();
    if (m_structureChanged)
    {
        rebuildLevels();
    }
    Scene::Registry& registry = m_scene.registry();
    auto& locals = registry.storage<LocalTransform>();
    auto& worlds = registry.storage<WorldTransform>();
    auto& states = registry.storage<TransformState>();
    auto& parents = registry.storage<Parent>();
    // One level at a time: a level only reads its parents' results, which the previous level finished. Within a level
    // every entity writes only its own WorldTransform and TransformState, so the jobs never share data.
    for (const std::vector<entt::entity>& level : m_levels)
    {
        const auto count = static_cast<std::uint32_t>(level.size());
        m_jobs.wait(m_jobs.parallelFor(
            count, c_minEntitiesPerJob,
            [&](std::uint32_t begin, std::uint32_t end, std::uint32_t /*threadIndex*/)
            {
                for (std::uint32_t i = begin; i < end; ++i)
                {
                    const entt::entity entity = level[i];
                    TransformState& state = states.get(entity);
                    const Parent* parent = parents.contains(entity) ? &parents.get(entity) : nullptr;
                    const bool parentChanged = parent != nullptr && states.get(parent->entity).changed;
                    state.changed = state.dirty || parentChanged;
                    if (!state.changed)
                    {
                        continue;
                    }
                    state.dirty = false;
                    DirectX::XMMATRIX world = Math::toMatrix(locals.get(entity).value);
                    if (parent != nullptr)
                    {
                        world = DirectX::XMMatrixMultiply(world, Math::load(worlds.get(parent->entity).matrix));
                    }
                    worlds.get(entity).matrix = Math::storeMat4(world);
                }
            }));
    }
}

void TransformHierarchy::savePrevious()
{
    MEOW2_PROFILE_ZONE();
    // A copy of 64 bytes per entity: memory-bound and fast enough on one thread (6 MB for 100k entities).
    // Through the storages: a view with a const component doesn't compile with the Scene's custom allocator.
    Scene::Registry& registry = m_scene.registry();
    const auto& worlds = registry.storage<WorldTransform>();
    for (const auto [entity, previous] : registry.storage<PreviousWorldTransform>().each())
    {
        previous.matrix = worlds.get(entity).matrix;
    }
}

Math::Mat4 TransformHierarchy::interpolatedWorld(entt::entity entity, float alpha) const
{
    const Scene::Registry& registry = std::as_const(m_scene).registry();
    const Math::Mat4& current = registry.get<WorldTransform>(entity).matrix;
    const std::optional<Math::Transform> from =
        Math::fromMatrix(Math::load(registry.get<PreviousWorldTransform>(entity).matrix));
    const std::optional<Math::Transform> to = Math::fromMatrix(Math::load(current));
    if (!from || !to)
    {
        return current; // a zero scale can't be split; show the current state
    }
    return Math::toMat4(Math::lerp(*from, *to, alpha));
}

std::size_t TransformHierarchy::levelCount() const
{
    return m_levels.size();
}
} // namespace Meow2::Scene
