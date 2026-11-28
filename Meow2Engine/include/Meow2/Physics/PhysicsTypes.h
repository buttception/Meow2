#pragma once

/// @file
/// @brief The physics API's plain data: body ids, shapes, body and world descriptions, query results and events.
///
/// Engine types only (Meow2/Math), no physics library's: the library is chosen at build time
/// (`MEOW2_PHYSICS_BACKEND`, techstack.md §14) and can be replaced without touching code that uses these.
/// Units: metres, kilograms, seconds, radians. Left-handed, +Y up (techstack.md §10).

#include <Meow2/Math/Transform.h>
#include <Meow2/Math/Types.h>

#include <array>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace Meow2::Physics
{
/// @brief Names a body of one PhysicsWorld. Opaque: the backend packs its own handle into it, and a destroyed body's
///        id stays invalid even if the backend reuses the slot (PhysicsWorld::contains()). Plain data.
struct BodyId
{
    std::uint64_t value = 0; ///< 0: no body.

    /// @brief Ids are equal when they name the same body.
    friend bool operator==(const BodyId&, const BodyId&) = default;
};

/// @brief Whether an id names a body at all (not whether that body still exists: PhysicsWorld::contains()).
/// @param id The id.
/// @return False for the default, empty id.
[[nodiscard]] constexpr bool isSet(BodyId id) noexcept
{
    return id.value != 0;
}

/// @brief How a body moves.
enum class MotionType : std::uint8_t
{
    Static,    ///< Never moves (floors, walls). Cheapest; two static bodies never collide.
    Kinematic, ///< Moved by code (moving platforms, doors) through PhysicsWorld::moveKinematic(); pushes dynamic
               ///< bodies but isn't pushed. Infinite mass.
    Dynamic    ///< Moved by the simulation: gravity, forces, impulses, contacts.
};

/// @brief Index of a collision layer, 0 to c_maxCollisionLayers - 1. Which layers collide is set per world
///        (PhysicsWorldDesc::layerMasks).
using CollisionLayer = std::uint8_t;

/// @brief The most collision layers a world has (one bit each in a 32-bit mask).
inline constexpr std::uint32_t c_maxCollisionLayers = 32;

/// @brief A layer mask with every layer set.
inline constexpr std::uint32_t c_allLayers = std::numeric_limits<std::uint32_t>::max();

/// @brief A sphere around the body's origin. Plain data.
struct SphereShape
{
    float radius = 0.5F; ///< Metres; > 0.
};

/// @brief A box centred on the body's origin. Plain data.
struct BoxShape
{
    Math::Vec3 halfExtents{0.5F, 0.5F, 0.5F}; ///< Half the size along each local axis, metres; each > 0.
};

/// @brief A capsule along the body's local Y axis, centred on its origin: a cylinder with a hemisphere on each end.
///        Plain data.
struct CapsuleShape
{
    float halfHeight = 0.5F; ///< Half the cylinder's height (without the end caps), metres; > 0.
    float radius = 0.3F;     ///< Radius of the cylinder and the caps, metres; > 0.
};

/// @brief The convex hull of a set of points in the body's local space. Plain data.
struct ConvexHullShape
{
    std::vector<Math::Vec3> points{}; ///< At least 4, not all in one plane. The hull is computed from them.
};

/// @brief A triangle mesh in the body's local space: terrain, level geometry. Static and kinematic bodies only (a mesh
///        has no inside, so no mass). Plain data.
struct MeshShape
{
    std::vector<Math::Vec3> vertices{};   ///< Local positions.
    std::vector<std::uint32_t> indices{}; ///< Three per triangle, into `vertices`, clockwise seen from the front
                                          ///< (the renderer's winding); at least one triangle.
};

/// @brief One of the shapes a CompoundShape is made of.
using CompoundPartShape = std::variant<SphereShape, BoxShape, CapsuleShape, ConvexHullShape>;

/// @brief One part of a CompoundShape: a convex shape placed in the body's local space. Plain data.
struct CompoundPart
{
    CompoundPartShape shape{SphereShape{}}; ///< The shape.
    Math::Vec3 position{};                  ///< Where its origin sits in the body.
    Math::Quat rotation{};                  ///< How it's turned in the body.
};

/// @brief Several convex shapes as one, e.g. a chair from boxes. Plain data.
struct CompoundShape
{
    std::vector<CompoundPart> parts{}; ///< At least one.
};

/// @brief Any shape a body can have.
using ShapeDesc = std::variant<SphereShape, BoxShape, CapsuleShape, ConvexHullShape, MeshShape, CompoundShape>;

/// @brief Everything needed to create a body (PhysicsWorld::createBody()). Plain data.
struct BodyDesc
{
    ShapeDesc shape{BoxShape{}};             ///< Its collision shape.
    Math::WorldPosition position{};          ///< Where its origin is.
    Math::Quat rotation{};                   ///< How it's turned; normalised.
    MotionType motion = MotionType::Dynamic; ///< How it moves.
    CollisionLayer layer = 0;                ///< Its collision layer; below PhysicsWorldDesc::layerCount.
    float friction = 0.5F;                   ///< 0 (ice) and up; two bodies use the root of the product of theirs.
    float restitution = 0.0F;                ///< Bounciness, 0 (none) to 1 (keeps all its speed).
    float density = 1000.0F;                 ///< kg/m³ (water: 1000); a dynamic body's mass is its volume times it.
    float mass = 0.0F;                       ///< kg, for a dynamic body: 0 takes it from the density, above 0 sets it
                                             ///< (and scales the inertia to match), whatever the shape's size.
    float linearDamping = 0.05F;             ///< Slows its movement down over time (1/s); ≥ 0.
    float angularDamping = 0.05F;            ///< Slows its spin down over time (1/s); ≥ 0.
    float gravityScale = 1.0F;               ///< Multiplies the world's gravity for this body (0: floats).
    bool sensor = false;                     ///< Detects bodies (SensorEvent) without pushing them.
    bool allowSleep = true;                  ///< May sleep when it comes to rest (it wakes when touched).
    bool continuous = false;                 ///< Continuous collision: fast bodies don't tunnel through thin ones.
    Math::Vec3 linearVelocity{};             ///< Starting velocity, m/s (dynamic and kinematic bodies).
    Math::Vec3 angularVelocity{};            ///< Starting spin, rad/s, around world axes.
    std::uint64_t userData = 0;              ///< Anything the caller wants back in hits and events (an EntityId).
};

/// @brief How a PhysicsWorld is set up. Plain data.
struct PhysicsWorldDesc
{
    Math::Vec3 gravity{0.0F, -9.81F, 0.0F};           ///< m/s².
    std::uint32_t maxBodies = 65536;                  ///< Bodies it can hold; createBody() fails beyond it.
    std::uint32_t maxBodyPairs = 65536;               ///< Pairs of bodies whose boxes overlap, per step.
    std::uint32_t maxContacts = 10240;                ///< Contact constraints per step.
    std::uint32_t tempMemoryBytes = 10 * 1024 * 1024; ///< Scratch memory for a step, taken once.
    std::uint32_t layerCount = 1;                     ///< Collision layers in use, 1 to c_maxCollisionLayers.
    /// Which layers each layer collides with: bit `b` of `layerMasks[a]` set means layer `a` collides with layer `b`.
    /// Collision needs both directions set (`a` with `b` and `b` with `a`). Every layer collides with every other by
    /// default.
    std::array<std::uint32_t, c_maxCollisionLayers> layerMasks = []
    {
        std::array<std::uint32_t, c_maxCollisionLayers> masks{};
        masks.fill(c_allLayers);
        return masks;
    }();
};

/// @brief Limits what a query sees. Plain data.
struct QueryFilter
{
    std::uint32_t layerMask = c_allLayers; ///< Only bodies in these layers (bit per CollisionLayer).
    BodyId ignore{};                       ///< A body to skip (e.g. the one casting); none by default.
    bool sensors = false;                  ///< Whether sensor bodies can be hit.
};

/// @brief Where a ray hit a body (PhysicsWorld::castRay()). Plain data.
struct RayHit
{
    BodyId body{};               ///< What it hit.
    float fraction = 0.0F;       ///< Along the ray's translation, 0 (its start) to 1 (its end).
    Math::WorldPosition point{}; ///< The hit point.
    Math::Vec3 normal{};         ///< The surface's outward unit normal there.
    std::uint64_t userData = 0;  ///< The body's BodyDesc::userData.
};

/// @brief Where a moving shape first touched a body (PhysicsWorld::castShape()). Plain data.
struct ShapeHit
{
    BodyId body{};               ///< What it touched.
    float fraction = 0.0F;       ///< How far along the translation it got, 0 to 1 (0: it started touching).
    Math::WorldPosition point{}; ///< The contact point on the body.
    Math::Vec3 normal{};         ///< The body's outward unit normal there (points at the moving shape).
    std::uint64_t userData = 0;  ///< The body's BodyDesc::userData.
};

/// @brief Whether a touch starts or ends.
enum class EventType : std::uint8_t
{
    Begin, ///< Started touching (or, for a sensor, entered) during the step.
    End    ///< Stopped touching (or left), or one of the bodies was destroyed.
};

/// @brief Two non-sensor bodies started or stopped touching (PhysicsWorld::contactEvents()). Plain data.
struct ContactEvent
{
    EventType type = EventType::Begin; ///< Begin or end.
    BodyId bodyA{};                    ///< One body (the one with the smaller id).
    BodyId bodyB{};                    ///< The other.
    Math::WorldPosition point{};       ///< Begin: a contact point. End: unset.
    Math::Vec3 normal{};               ///< Begin: the contact normal, from A towards B. End: unset.
};

/// @brief A body entered or left a sensor (PhysicsWorld::sensorEvents()). Plain data.
struct SensorEvent
{
    EventType type = EventType::Begin; ///< Entered or left.
    BodyId sensor{};                   ///< The sensor.
    BodyId other{};                    ///< The body.
};
} // namespace Meow2::Physics
