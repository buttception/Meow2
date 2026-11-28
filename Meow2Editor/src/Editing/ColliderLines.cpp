/// @file
/// @brief colliderLines() (ColliderLines.h).

#include "Editing/ColliderLines.h"

#include <Meow2/Math/Scalar.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Meow2::Editor
{
namespace
{
/// @brief Segments per full circle: smooth enough at any size on screen, cheap enough for many colliders.
constexpr int c_circleSegments = 32;

/// @brief Turns points in a collider's frame (the entity's position and rotation, sizes already scaled) into world
///        lines.
class Pen
{
public:
    Pen(const Math::Transform& world, std::vector<Renderer::DebugLine>& out)
        : m_position(DirectX::XMLoadFloat3(&world.position))
        , m_rotation(DirectX::XMLoadFloat4(&world.rotation))
        , m_out(out)
    {
    }

    /// @brief A line between two points of the collider's frame.
    void line(const Math::Vec3& from, const Math::Vec3& to)
    {
        m_out.push_back({.from = place(from), .to = place(to), .color = c_colliderColor});
    }

    /// @brief An arc around `center` in the plane of the unit axes `u` and `v`, from angle `from` to `to` (radians).
    void arc(const Math::Vec3& center, const Math::Vec3& u, const Math::Vec3& v, float radius, float from, float to)
    {
        const int segments = std::max(
            4, static_cast<int>(std::ceil(c_circleSegments * (to - from) / (2.0F * std::numbers::pi_v<float>))));
        const auto at = [&](float angle)
        {
            const float c = std::cos(angle) * radius;
            const float s = std::sin(angle) * radius;
            return Math::Vec3{center.x + u.x * c + v.x * s, center.y + u.y * c + v.y * s, center.z + u.z * c + v.z * s};
        };
        Math::Vec3 previous = at(from);
        for (int i = 1; i <= segments; ++i)
        {
            const Math::Vec3 next = at(from + (to - from) * static_cast<float>(i) / static_cast<float>(segments));
            line(previous, next);
            previous = next;
        }
    }

    /// @brief The twelve edges of a box between two corners of the collider's frame.
    void box(const Math::Vec3& low, const Math::Vec3& high)
    {
        const auto corner = [&](int i)
        {
            return Math::Vec3{(i & 1) != 0 ? high.x : low.x, (i & 2) != 0 ? high.y : low.y,
                              (i & 4) != 0 ? high.z : low.z};
        };
        for (int i = 0; i < 8; ++i)
        {
            for (const int bit : {1, 2, 4})
            {
                if ((i & bit) == 0)
                {
                    line(corner(i), corner(i | bit));
                }
            }
        }
    }

private:
    Math::Vec3 place(const Math::Vec3& point) const
    {
        Math::Vec3 out;
        DirectX::XMStoreFloat3(
            &out,
            DirectX::XMVectorAdd(DirectX::XMVector3Rotate(DirectX::XMLoadFloat3(&point), m_rotation), m_position));
        return out;
    }

    DirectX::XMVECTOR m_position;
    DirectX::XMVECTOR m_rotation;
    std::vector<Renderer::DebugLine>& m_out;
};

Math::Vec3 multiply(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

/// @brief One Collider's outline (see colliderLines()).
void outline(const Physics::Collider& collider, const Math::Transform& world, const Physics::CollisionMesh* mesh,
             std::vector<Renderer::DebugLine>& out)
{
    Pen pen{world, out};
    const Math::Vec3& s = world.scale;
    const Math::Vec3 center = multiply(collider.center, s);
    constexpr float c_pi = std::numbers::pi_v<float>;
    const Math::Vec3 x{1.0F, 0.0F, 0.0F};
    const Math::Vec3 y{0.0F, 1.0F, 0.0F};
    const Math::Vec3 z{0.0F, 0.0F, 1.0F};
    switch (collider.shape)
    {
        case Physics::ColliderShape::Box:
        {
            const Math::Vec3 half = multiply(collider.size, s);
            const Math::Vec3 h{std::abs(half.x) * 0.5F, std::abs(half.y) * 0.5F, std::abs(half.z) * 0.5F};
            pen.box({center.x - h.x, center.y - h.y, center.z - h.z}, {center.x + h.x, center.y + h.y, center.z + h.z});
            return;
        }
        case Physics::ColliderShape::Sphere:
        {
            const float radius = collider.radius * std::max({std::abs(s.x), std::abs(s.y), std::abs(s.z)});
            pen.arc(center, x, y, radius, 0.0F, 2.0F * c_pi);
            pen.arc(center, y, z, radius, 0.0F, 2.0F * c_pi);
            pen.arc(center, z, x, radius, 0.0F, 2.0F * c_pi);
            return;
        }
        case Physics::ColliderShape::Capsule:
        {
            const float radius = collider.radius * std::max(std::abs(s.x), std::abs(s.z));
            const float half = std::max(0.0F, collider.height * std::abs(s.y) * 0.5F - radius);
            const Math::Vec3 top{center.x, center.y + half, center.z};
            const Math::Vec3 bottom{center.x, center.y - half, center.z};
            pen.arc(top, x, z, radius, 0.0F, 2.0F * c_pi);
            pen.arc(bottom, x, z, radius, 0.0F, 2.0F * c_pi);
            for (const Math::Vec3& side : {x, z})
            {
                const Math::Vec3 offset{side.x * radius, 0.0F, side.z * radius};
                pen.line({top.x + offset.x, top.y, top.z + offset.z},
                         {bottom.x + offset.x, bottom.y, bottom.z + offset.z});
                pen.line({top.x - offset.x, top.y, top.z - offset.z},
                         {bottom.x - offset.x, bottom.y, bottom.z - offset.z});
                pen.arc(top, side, y, radius, 0.0F, c_pi);           // the top cap
                pen.arc(bottom, side, y, radius, c_pi, 2.0F * c_pi); // the bottom cap
            }
            return;
        }
        case Physics::ColliderShape::Mesh:
        {
            if (mesh == nullptr || mesh->positions.empty())
            {
                return;
            }
            Math::Vec3 low = multiply(mesh->positions.front(), s);
            Math::Vec3 high = low;
            for (const Math::Vec3& position : mesh->positions)
            {
                const Math::Vec3 p = multiply(position, s);
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
            pen.box({low.x + center.x, low.y + center.y, low.z + center.z},
                    {high.x + center.x, high.y + center.y, high.z + center.z});
            return;
        }
    }
}
} // namespace

std::vector<Renderer::DebugLine> colliderLines(const Scene::Scene& scene, std::span<const entt::entity> selected,
                                               const Physics::CollisionMeshes& meshes,
                                               const std::function<std::optional<AssetId>(entt::entity)>& meshOf)
{
    std::vector<Renderer::DebugLine> lines;
    if (selected.empty())
    {
        return lines;
    }
    const auto& registry = scene.registry();
    const auto isSelected = [&](entt::entity entity)
    {
        for (entt::entity at = entity; at != entt::null && registry.valid(at);)
        {
            if (std::ranges::find(selected, at) != selected.end())
            {
                return true;
            }
            const auto* parent = registry.try_get<Scene::Parent>(at);
            at = parent != nullptr ? parent->entity : entt::null;
        }
        return false;
    };
    for (const auto [entity, collider] : registry.view<Physics::Collider>().each())
    {
        const auto* world = registry.try_get<Scene::WorldTransform>(entity);
        if (world == nullptr || !isSelected(entity))
        {
            continue;
        }
        const std::optional<Math::Transform> split = Math::fromMatrix(DirectX::XMLoadFloat4x4(&world->matrix));
        if (!split)
        {
            continue;
        }
        const Physics::CollisionMesh* mesh = nullptr;
        if (collider.shape == Physics::ColliderShape::Mesh && meshOf)
        {
            if (const std::optional<AssetId> id = meshOf(entity))
            {
                mesh = meshes.find(*id);
            }
        }
        outline(collider, *split, mesh, lines);
    }
    return lines;
}
} // namespace Meow2::Editor
