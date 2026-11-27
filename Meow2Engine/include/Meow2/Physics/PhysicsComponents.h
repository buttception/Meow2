#pragma once

/// @file
/// @brief The physics components: RigidBody and Collider on entities.
///
/// They follow Unity's rules (owner decision 2026-11-27):
/// - An entity with a Collider and no RigidBody, on it or on an ancestor, is **static scenery**: a static body of
///   its own.
/// - A **RigidBody** makes its entity a body. Its shape is its own Collider plus the Colliders of its descendants that
///   aren't under another RigidBody, as one compound, so a chair made of child boxes moves as one.
/// - The entity's **world scale** scales its collider (each axis for boxes; the largest for spheres).
/// - A **mesh** Collider uses the entity's MeshRenderer mesh (Physics::CollisionMeshes): its triangles for static
///   scenery, a convex hull of its vertices on a body (a mesh has no inside, so it can't have a mass).
///
/// PhysicsScene turns these into PhysicsWorld bodies and keeps them in step with the entities.

#include <Meow2/Math/Types.h>
#include <Meow2/Physics/PhysicsTypes.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Scene.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace Meow2::Physics
{
/// @brief Makes its entity a physics body. Plain data (a component, techstack §25).
struct RigidBody
{
    MotionType motion = MotionType::Dynamic; ///< Dynamic (simulated) or kinematic (follows its transform); a static
                                             ///< RigidBody is allowed too, though a Collider alone is the same.
    float mass = 0.0F;                       ///< kg; 0 takes it from the colliders' volume at 1000 kg/m³ (water).
    float linearDamping = 0.05F;             ///< Slows its movement down over time (1/s); ≥ 0.
    float angularDamping = 0.05F;            ///< Slows its spin down over time (1/s); ≥ 0.
    float gravityScale = 1.0F;               ///< Multiplies the scene's gravity for this body (0: floats).
    bool continuous = false;                 ///< Continuous collision, for fast bodies that mustn't tunnel.
    bool allowSleep = true;                  ///< May sleep at rest (wakes when touched).
};

/// @brief The shape a Collider has.
enum class ColliderShape : std::uint8_t
{
    Box,     ///< Collider::size, centred on Collider::center.
    Sphere,  ///< Collider::radius.
    Capsule, ///< Along the entity's Y axis: Collider::height overall (end caps included) and Collider::radius.
    Mesh     ///< The entity's MeshRenderer mesh: triangles for static scenery, a convex hull on a body.
};

/// @brief Gives its entity a collision shape. Plain data (a component).
struct Collider
{
    ColliderShape shape = ColliderShape::Box; ///< Which shape.
    Math::Vec3 center{};                      ///< Offset from the entity's origin, in its local space (scaled).
    Math::Vec3 size{1.0F, 1.0F, 1.0F};        ///< Box: its full size along each local axis, metres.
    float radius = 0.5F;                      ///< Sphere and capsule: radius, metres.
    float height = 2.0F;                      ///< Capsule: overall height including the caps, metres (≥ 2 × radius).
    float friction = 0.5F;                    ///< 0 (ice) and up.
    float restitution = 0.0F;                 ///< Bounciness, 0 to 1.
    bool sensor = false;                      ///< Detects bodies (sensor events) without pushing them.
};

/// @brief Registers RigidBody and Collider, for scene files, the Inspector and agents.
/// @param registry Where.
void registerPhysicsComponents(Scene::ComponentRegistry& registry);
} // namespace Meow2::Physics

namespace Meow2::Scene
{
/// @brief MotionType's names in files and the Inspector.
template <>
struct EnumNames<Physics::MotionType>
{
    /// @brief By value.
    static constexpr std::array<std::string_view, 3> c_names{"static", "kinematic", "dynamic"};
};

/// @brief ColliderShape's names in files and the Inspector.
template <>
struct EnumNames<Physics::ColliderShape>
{
    /// @brief By value.
    static constexpr std::array<std::string_view, 4> c_names{"box", "sphere", "capsule", "mesh"};
};
} // namespace Meow2::Scene
