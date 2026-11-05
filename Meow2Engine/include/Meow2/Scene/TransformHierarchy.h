#pragma once

/// @file
/// @brief Parented transforms in a Scene: local and world transforms, updated in parallel by depth level, with dirty
///        flags and the previous world transform for `render(alpha)`.
///
/// @code
/// Meow2::Scene::TransformHierarchy transforms{scene, jobs};      // composition root, after the Scene
/// const entt::entity car = scene.create("Car");
/// const entt::entity wheel = scene.create("Wheel");
/// transforms.add(car, Math::Transform{.position = {0.0F, 0.0F, 10.0F}});
/// transforms.add(wheel, Math::Transform{.position = {1.0F, -0.5F, 1.5F}}, car);
///
/// // each fixed step: transforms.savePrevious(); … gameplay moves things with setLocal() … transforms.update();
/// // render(alpha):   const Math::Mat4 world = transforms.interpolatedWorld(wheel, alpha);
/// @endcode
/// @see techstack.md §23, §25, docs/wiki/Scene.md

#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>
#include <Meow2/Scene/Scene.h>

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Meow2::Jobs
{
class JobSystem;
}

namespace Meow2::Scene
{
/// @brief An entity's transform relative to its parent (or the world, for a root). Plain data.
///
/// Write it through TransformHierarchy::setLocal(), or write it directly and call TransformHierarchy::markDirty().
struct LocalTransform
{
    Math::Transform value; ///< Position, rotation and scale relative to the parent.
};

/// @brief An entity's transform in the world: the product of its local transform and all its ancestors'. Plain data.
///
/// Written by TransformHierarchy::update(); read it, don't write it.
struct WorldTransform
{
    Math::Mat4 matrix; ///< Local → world (row vectors). Exact also under non-uniform parent scale.
};

/// @brief The world transform at the previous fixed step, for `render(alpha)` interpolation. Plain data.
struct PreviousWorldTransform
{
    Math::Mat4 matrix; ///< WorldTransform as of the last TransformHierarchy::savePrevious().
};

/// @brief The entity's parent in the hierarchy. Absent on roots. Plain data.
///
/// Set it with TransformHierarchy::setParent(), which checks for cycles and updates the depth levels.
struct Parent
{
    entt::entity entity = entt::null; ///< The parent; must have a LocalTransform.
};

/// @brief Bookkeeping of the hierarchy update. Plain data; managed by TransformHierarchy.
struct TransformState
{
    bool dirty = true;       ///< The local transform changed since the last update.
    bool changed = false;    ///< The world transform was recomputed in the last update (children must follow).
    std::uint32_t depth = 0; ///< 0 for roots, parent's depth + 1 otherwise.
};

/// @brief Keeps the world transforms of a Scene's entities up to date.
///
/// - **Levels:** entities are grouped by depth (roots, their children, grandchildren, …). update() processes one level
///   at a time, each level as a parallel-for on the JobSystem: an entity only reads its parent's world transform,
///   which the previous level has finished, so there are no locks and no races.
/// - **Dirty flags:** only entities whose local transform changed, or whose parent's world transform changed, are
///   recomputed. A static level costs one flag check per entity.
/// - **Structure:** the levels are rebuilt only after a structural change (add, setParent, an entity with a
///   LocalTransform destroyed), which EnTT signals report.
/// - **Destroyed parents:** their children become roots, keeping their local transform (now relative to the world).
///   Destroy a subtree child-first if it should disappear with its parent.
/// @par Thread safety
/// Not thread-safe; use it on the Scene's owner thread. update() and savePrevious() use the JobSystem internally and
/// return when all work is done. Don't change the scene's structure from jobs while they run.
/// @par Lifetime
/// The Scene and the JobSystem must outlive it. Create it after the Scene, before any entity gets a transform.
class TransformHierarchy
{
public:
    /// @brief Registers the transform components with the scene and starts watching it.
    /// @param scene The scene. Must outlive this.
    /// @param jobs  Runs the parallel updates. Must outlive this.
    TransformHierarchy(Scene& scene, Jobs::JobSystem& jobs);

    /// @brief Stops watching the scene. The components stay on the entities.
    ~TransformHierarchy();

    TransformHierarchy(const TransformHierarchy&) = delete;            ///< Not copyable: connected to the scene.
    TransformHierarchy& operator=(const TransformHierarchy&) = delete; ///< Not copyable: connected to the scene.
    TransformHierarchy(TransformHierarchy&&) = delete;            ///< Not movable: the scene's signals point at it.
    TransformHierarchy& operator=(TransformHierarchy&&) = delete; ///< Not movable: the scene's signals point at it.

    /// @brief Gives an entity a transform, optionally under a parent.
    /// @param entity The entity; must not have a LocalTransform yet (asserted).
    /// @param local  Its transform relative to the parent (or the world).
    /// @param parent The parent, or `entt::null` for a root. Must have a transform (asserted).
    void add(entt::entity entity, const Math::Transform& local = {}, entt::entity parent = entt::null);

    /// @brief Sets an entity's local transform and marks it dirty.
    /// @param entity The entity (with a transform).
    /// @param local  The new local transform.
    void setLocal(entt::entity entity, const Math::Transform& local);

    /// @brief Marks an entity's local transform as changed, after writing LocalTransform directly.
    ///
    /// Only writes the entity's own TransformState, so jobs may call it for the entities they own.
    /// @param entity The entity (with a transform).
    void markDirty(entt::entity entity);

    /// @brief Moves an entity under a new parent (or makes it a root). Its local transform is kept, so it moves with
    ///        the new parent.
    /// @param entity The entity (with a transform).
    /// @param parent The new parent (with a transform), or `entt::null` to make it a root.
    /// @pre `parent` is not `entity` or one of its descendants (asserted: that would be a cycle).
    void setParent(entt::entity entity, entt::entity parent);

    /// @brief Recomputes the world transforms that need it.
    ///
    /// Call it once per fixed step (after gameplay moved things) and before rendering.
    void update();

    /// @brief Copies every world transform to PreviousWorldTransform: the start of a fixed step.
    void savePrevious();

    /// @brief The world transform between the previous fixed step and the current one.
    ///
    /// Both matrices are split into position, rotation and scale, which are interpolated (lerp, slerp) and recombined.
    /// Exact for transforms without shear (see Math::Transform).
    /// @param entity The entity (with a transform).
    /// @param alpha  0 = the previous step, 1 = the current one (`render(alpha)`'s value).
    /// @return The interpolated world matrix.
    [[nodiscard]] Math::Mat4 interpolatedWorld(entt::entity entity, float alpha) const;

    /// @brief How many depth levels the hierarchy has (1 = only roots), as of the last update().
    /// @return The level count.
    [[nodiscard]] std::size_t levelCount() const;

private:
    /// @brief Regroups all transform entities by depth (and orphans children of destroyed parents).
    void rebuildLevels();
    /// @brief Signal handler: an entity got or lost a transform, or its parent changed.
    void onStructureChanged(Scene::Registry& registry, entt::entity entity);

    Scene& m_scene;
    Jobs::JobSystem& m_jobs;
    std::vector<std::vector<entt::entity>> m_levels; ///< Entities by depth; rebuilt when m_structureChanged.
    bool m_structureChanged = true;
};
} // namespace Meow2::Scene
