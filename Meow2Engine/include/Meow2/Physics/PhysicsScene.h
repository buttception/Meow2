#pragma once

/// @file
/// @brief PhysicsScene: a Scene's RigidBody and Collider components as PhysicsWorld bodies, stepped with the scene.

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Physics/CollisionMeshes.h>
#include <Meow2/Physics/PhysicsComponents.h>
#include <Meow2/Physics/PhysicsWorld.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Meow2::Physics
{
/// @brief Which mesh an entity shows, for its mesh Collider (the renderer's MeshRenderer; Physics doesn't depend on
///        the renderer, so the app says how to find it).
using MeshOfEntity = std::function<std::optional<AssetId>(const Scene::Scene& scene, entt::entity entity)>;

/// @brief What a PhysicsScene works on. Plain data (references); everything must outlive it.
struct PhysicsSceneDeps
{
    Scene::Scene& scene;                   ///< The entities, with their RigidBody and Collider components.
    Scene::TransformHierarchy& transforms; ///< Their transforms: read for bodies, written back for dynamic ones.
    PhysicsWorld& world;                   ///< Where the bodies go.
    const CollisionMeshes& meshes;         ///< The triangles of mesh Colliders.
    MeshOfEntity meshOf;                   ///< Which mesh an entity shows; unset: mesh Colliders are skipped.
    Log::Logger& log;                      ///< Receives what couldn't be built.
};

/// @brief Turns a scene's RigidBody and Collider components into PhysicsWorld bodies, and keeps the two in step each
///        fixed step (techstack §14, §23).
///
/// build() makes the bodies, by Unity's rules (PhysicsComponents.h):
/// - A Collider with no RigidBody on its entity or an ancestor becomes a static body of its own, at the entity's
///   world position and rotation, its size times the entity's world scale.
/// - A RigidBody's body takes its own Collider and its descendants' (those not under another RigidBody) as one
///   compound, each part placed where its entity is relative to the body. A RigidBody without any Collider is skipped
///   with a warning.
/// - A mesh Collider is the mesh's triangles on a static body, and a convex hull of its vertices on a moving one.
///
/// step() runs one fixed step: kinematic bodies are moved to where their entities now are (so gameplay or the editor
/// can drive them), the world steps, and dynamic bodies that are awake write their new position and rotation back to
/// their entities' LocalTransform (their scale is kept). The world transforms are updated after.
///
/// It builds once: components added or changed after build() aren't seen until the next build (the editor builds on
/// Play and throws everything away on Stop).
/// @par Thread safety
/// Not thread-safe: the thread that owns the scene (the main thread, in fixedUpdate).
/// @par Lifetime
/// Everything in PhysicsSceneDeps must outlive it; destroying it destroys its bodies.
class PhysicsScene
{
public:
    /// @brief A scene without bodies yet.
    /// @param deps What it works on.
    explicit PhysicsScene(PhysicsSceneDeps deps);

    /// @brief Destroys its bodies.
    ~PhysicsScene();

    PhysicsScene(const PhysicsScene&) = delete;            ///< Not copyable: owns bodies.
    PhysicsScene& operator=(const PhysicsScene&) = delete; ///< Not copyable.

    /// @brief Creates a body for every RigidBody, and for every Collider not under one (destroying any made before).
    /// @return What was skipped and why, one line each (a RigidBody without a Collider, a mesh Collider whose mesh
    ///         isn't known, a size of 0); empty when everything was built.
    std::vector<std::string> build();

    /// @brief One fixed step (see the class description).
    /// @param seconds The step, > 0.
    void step(float seconds);

    /// @brief The body an entity owns (a RigidBody, or a static Collider).
    /// @param entity The entity.
    /// @return Its body, or nothing (a child Collider is part of its RigidBody's body).
    [[nodiscard]] std::optional<BodyId> bodyOf(entt::entity entity) const;

    /// @brief How many bodies it made.
    /// @return The count.
    [[nodiscard]] std::size_t bodyCount() const
    {
        return m_bodies.size();
    }

private:
    /// @brief One body and the entity it moves. Plain data.
    struct Body
    {
        entt::entity entity = entt::null; ///< The RigidBody's or static Collider's entity.
        BodyId id{};                      ///< In the world.
        MotionType motion = MotionType::Static;
    };

    /// @brief Destroys every body.
    void clear();

    PhysicsSceneDeps m_deps;
    std::vector<Body> m_bodies;
};
} // namespace Meow2::Physics
