#pragma once

/// @file
/// @brief Checks of physics inputs that every backend shares, so they all refuse the same things the same way.

#include <Meow2/Core/Result.h>
#include <Meow2/Physics/PhysicsTypes.h>

#include <cstdint>

namespace Meow2::Physics
{
/// @brief Checks a world description: 1 to c_maxCollisionLayers layers, room for at least one body.
/// @param desc The description.
/// @return Nothing, or ErrorCode::InvalidArgument naming the problem.
[[nodiscard]] Result<void> validateWorldDesc(const PhysicsWorldDesc& desc);

/// @brief Checks a shape: finite sizes above 0, at least 4 hull points, a mesh with whole triangles and indices in
///        range, a compound with parts. Whether a hull's points span a volume is left to the backend, which has to
///        build the hull anyway.
/// @param shape  The shape.
/// @param motion How the body moves: a mesh can't be on a dynamic body.
/// @return Nothing, or ErrorCode::InvalidArgument naming the problem.
[[nodiscard]] Result<void> validateShape(const ShapeDesc& shape, MotionType motion);

/// @brief Checks a shape used in a query (castShape(), overlap()): one of the convex ones, and valid.
/// @param shape The shape.
/// @return Nothing, or ErrorCode::InvalidArgument.
[[nodiscard]] Result<void> validateQueryShape(const ShapeDesc& shape);

/// @brief Checks a body description: its shape, a layer below the world's count, finite position, a non-zero
///        rotation, density and damping not below 0, friction and restitution in range.
/// @param desc       The body.
/// @param layerCount The world's PhysicsWorldDesc::layerCount.
/// @return Nothing, or ErrorCode::InvalidArgument naming the problem.
[[nodiscard]] Result<void> validateBodyDesc(const BodyDesc& desc, std::uint32_t layerCount);

/// @brief Whether two layers collide under a world's masks (both directions must allow it).
/// @param desc The world's description.
/// @param a    One layer.
/// @param b    The other.
/// @return True if bodies in them collide.
[[nodiscard]] bool layersCollide(const PhysicsWorldDesc& desc, std::uint32_t a, std::uint32_t b);
} // namespace Meow2::Physics
