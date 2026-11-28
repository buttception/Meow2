/// @file
/// @brief The shared checks of physics inputs (PhysicsValidation.h).

#include "Physics/PhysicsValidation.h"

#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Meow2::Physics
{
namespace
{
/// @brief A size that's a finite number above 0.
bool positive(float value)
{
    return std::isfinite(value) && value > 0.0F;
}

/// @brief A vector whose components are all finite numbers.
bool finite(const Math::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Math::Vec3 subtract(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Math::Vec3 cross(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(const Math::Vec3& a, const Math::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// @brief Whether points enclose a volume: not all on one line or in one plane. A hull of them would have no inside,
///        so no mass, and libraries refuse it in different ways (or build a flat one); this decides it once for all.
///
/// Takes the point farthest from the first, then the one farthest from that line, then the one farthest from that
/// plane; the points span a volume when the last distance isn't tiny next to the points' size.
bool spansVolume(const std::vector<Math::Vec3>& points)
{
    const Math::Vec3& origin = points.front();
    const auto farthest = [&points](auto distance)
    {
        float best = 0.0F;
        const Math::Vec3* chosen = &points.front();
        for (const Math::Vec3& point : points)
        {
            if (const float value = distance(point); value > best)
            {
                best = value;
                chosen = &point;
            }
        }
        return std::pair{chosen, best};
    };
    const auto [second, size] = farthest(
        [&](const Math::Vec3& p)
        {
            const Math::Vec3 d = subtract(p, origin);
            return dot(d, d);
        });
    const Math::Vec3 axis = subtract(*second, origin);
    const auto [third, lineDistance] = farthest(
        [&](const Math::Vec3& p)
        {
            const Math::Vec3 c = cross(axis, subtract(p, origin));
            return dot(c, c);
        });
    const Math::Vec3 normal = cross(axis, subtract(*third, origin));
    const auto [fourth, planeDistance] =
        farthest([&](const Math::Vec3& p) { return std::abs(dot(normal, subtract(p, origin))); });
    // `size` is a squared length L²; the volume term |n·d| grows like L³ (n is a cross product of two edges).
    const float scale = size * std::sqrt(size);
    static_cast<void>(lineDistance);
    static_cast<void>(fourth);
    return scale > 0.0F && planeDistance > 1e-6F * scale;
}

Unexpected<Error> invalid(std::string message)
{
    return fail(ErrorCode::InvalidArgument, std::move(message));
}

Result<void> validateConvex(const CompoundPartShape& shape, std::string_view where)
{
    return std::visit(
        [where](const auto& convex) -> Result<void>
        {
            using Shape = std::decay_t<decltype(convex)>;
            if constexpr (std::is_same_v<Shape, SphereShape>)
            {
                if (!positive(convex.radius))
                {
                    return invalid(std::format("{}sphere radius must be above 0", where));
                }
            }
            else if constexpr (std::is_same_v<Shape, BoxShape>)
            {
                if (!positive(convex.halfExtents.x) || !positive(convex.halfExtents.y) ||
                    !positive(convex.halfExtents.z))
                {
                    return invalid(std::format("{}box half extents must be above 0", where));
                }
            }
            else if constexpr (std::is_same_v<Shape, CapsuleShape>)
            {
                if (!positive(convex.halfHeight) || !positive(convex.radius))
                {
                    return invalid(std::format("{}capsule half height and radius must be above 0", where));
                }
            }
            else
            {
                static_assert(std::is_same_v<Shape, ConvexHullShape>);
                if (convex.points.size() < 4)
                {
                    return invalid(
                        std::format("{}convex hull needs at least 4 points, not {}", where, convex.points.size()));
                }
                for (const Math::Vec3& point : convex.points)
                {
                    if (!finite(point))
                    {
                        return invalid(std::format("{}convex hull has a point that isn't a finite number", where));
                    }
                }
                if (!spansVolume(convex.points))
                {
                    return invalid(std::format("{}convex hull's points lie on a line or in one plane", where));
                }
            }
            return {};
        },
        shape);
}
} // namespace

Result<void> validateWorldDesc(const PhysicsWorldDesc& desc)
{
    if (desc.layerCount == 0 || desc.layerCount > c_maxCollisionLayers)
    {
        return invalid(std::format("layerCount must be 1 to {}, not {}", c_maxCollisionLayers, desc.layerCount));
    }
    if (desc.maxBodies == 0 || desc.maxBodyPairs == 0 || desc.maxContacts == 0)
    {
        return invalid("maxBodies, maxBodyPairs and maxContacts must be above 0");
    }
    if (!finite(desc.gravity))
    {
        return invalid("gravity must be finite");
    }
    return {};
}

Result<void> validateShape(const ShapeDesc& shape, MotionType motion)
{
    if (const auto* mesh = std::get_if<MeshShape>(&shape))
    {
        if (motion == MotionType::Dynamic)
        {
            return invalid("a mesh shape can't be on a dynamic body (use a convex hull or a compound)");
        }
        if (mesh->indices.empty() || mesh->indices.size() % 3 != 0)
        {
            return invalid(std::format("a mesh needs whole triangles: {} indices", mesh->indices.size()));
        }
        for (const std::uint32_t index : mesh->indices)
        {
            if (index >= mesh->vertices.size())
            {
                return invalid(
                    std::format("mesh index {} is out of range ({} vertices)", index, mesh->vertices.size()));
            }
        }
        for (const Math::Vec3& vertex : mesh->vertices)
        {
            if (!finite(vertex))
            {
                return invalid("a mesh has a vertex that isn't a finite number");
            }
        }
        return {};
    }
    if (const auto* compound = std::get_if<CompoundShape>(&shape))
    {
        if (compound->parts.empty())
        {
            return invalid("a compound shape needs at least one part");
        }
        for (std::size_t i = 0; i < compound->parts.size(); ++i)
        {
            const CompoundPart& part = compound->parts[i];
            if (!finite(part.position))
            {
                return invalid(std::format("compound part {}: its position isn't finite", i));
            }
            if (Result<void> valid = validateConvex(part.shape, std::format("compound part {}: ", i)); !valid)
            {
                return valid;
            }
        }
        return {};
    }
    // The convex shapes: the same alternatives as a compound part.
    return std::visit(
        [](const auto& convex) -> Result<void>
        {
            using Shape = std::decay_t<decltype(convex)>;
            if constexpr (std::is_same_v<Shape, MeshShape> || std::is_same_v<Shape, CompoundShape>)
            {
                return {}; // handled above
            }
            else
            {
                return validateConvex(CompoundPartShape{convex}, "");
            }
        },
        shape);
}

Result<void> validateQueryShape(const ShapeDesc& shape)
{
    if (std::holds_alternative<MeshShape>(shape) || std::holds_alternative<CompoundShape>(shape))
    {
        return invalid("queries take a sphere, box, capsule or convex hull");
    }
    return validateShape(shape, MotionType::Static);
}

Result<void> validateBodyDesc(const BodyDesc& desc, std::uint32_t layerCount)
{
    if (Result<void> shape = validateShape(desc.shape, desc.motion); !shape)
    {
        return shape;
    }
    if (desc.layer >= layerCount)
    {
        return invalid(std::format("layer {} is outside the world's {} layers", desc.layer, layerCount));
    }
    if (!finite(desc.position) || !finite(desc.linearVelocity) || !finite(desc.angularVelocity))
    {
        return invalid("position and velocities must be finite");
    }
    const Math::Quat& q = desc.rotation;
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6F)
    {
        return invalid("rotation must be a non-zero quaternion");
    }
    if (!(desc.density > 0.0F) || !(desc.mass >= 0.0F) || !std::isfinite(desc.mass) || !(desc.linearDamping >= 0.0F) ||
        !(desc.angularDamping >= 0.0F) || !(desc.friction >= 0.0F) ||
        !(desc.restitution >= 0.0F && desc.restitution <= 1.0F) || !std::isfinite(desc.gravityScale))
    {
        return invalid("density must be above 0, mass and damping and friction at least 0, restitution 0 to 1");
    }
    return {};
}

bool layersCollide(const PhysicsWorldDesc& desc, std::uint32_t a, std::uint32_t b)
{
    return (desc.layerMasks[a] & (1U << b)) != 0 && (desc.layerMasks[b] & (1U << a)) != 0;
}
} // namespace Meow2::Physics
