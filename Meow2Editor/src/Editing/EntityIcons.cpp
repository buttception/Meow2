/// @file
/// @brief EntityIcons (EntityIcons.h).

#include "Editing/EntityIcons.h"

#include "Editing/CameraIcons.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Meow2::Editor
{
namespace
{
Math::Vec3 add(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Math::Vec3 sub(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Math::Vec3 scaled(const Math::Vec3& v, float s)
{
    return {v.x * s, v.y * s, v.z * s};
}

Math::Vec3 cross(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// @brief The viewing camera's position, from its view matrix.
Math::Vec3 eyeOf(const GizmoView& view)
{
    DirectX::XMVECTOR determinant{};
    const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, Math::load(view.matrices.view));
    Math::Vec3 eye;
    DirectX::XMStoreFloat3(&eye, inverse.r[3]);
    return eye;
}

/// @brief World units per pixel at a point, along the viewing camera's up (as the gizmo measures it).
float worldPerPixel(const GizmoView& view, const Math::Vec3& point)
{
    const Math::Mat4& m = view.matrices.view;
    const Math::Vec3 up{m._12, m._22, m._32}; // row vectors: the view matrix's second column
    const std::optional<Math::Vec3> a = Math::worldToScreen(point, view.matrices, view.viewport);
    const std::optional<Math::Vec3> b = Math::worldToScreen(add(point, up), view.matrices, view.viewport);
    if (!a || !b)
    {
        return 0.0F; // behind the viewer
    }
    const float pixels = std::hypot(b->x - a->x, b->y - a->y);
    return pixels > 0.0F ? 1.0F / pixels : 0.0F;
}

/// @brief Metres per icon unit for an icon (1.3 units are c_iconPixels on screen); 0 behind the viewer.
float iconScale(const GizmoView& view, const PlacedIcon& icon)
{
    return worldPerPixel(view, icon.world.position) * c_iconPixels / 1.3F;
}

/// @brief A point of the entity's space in the world, `scale` metres per unit.
Math::Vec3 toWorld(const PlacedIcon& icon, float scale, const Math::Vec3& local)
{
    return add(icon.world.position, Math::rotate(icon.world.rotation, scaled(local, scale)));
}

constexpr float c_lineWidthPixels = 2.0F; ///< As the gizmo's lines.
} // namespace

std::vector<std::string> EntityIcons::kinds() const
{
    std::vector<std::string> names;
    for (const Kind& kind : m_kinds)
    {
        names.push_back(kind.name);
    }
    return names;
}

std::vector<PlacedIcon> EntityIcons::collect(Scene::Scene& scene, std::span<const EntityId> selection,
                                             const IconContext& context) const
{
    std::vector<PlacedIcon> icons;
    const auto& registry = scene.registry();
    for (const Kind& kind : m_kinds)
    {
        for (const entt::entity entity : kind.entities(scene))
        {
            if (std::ranges::any_of(icons, [entity](const PlacedIcon& icon) { return icon.entity == entity; }) ||
                !registry.all_of<Scene::WorldTransform>(entity))
            {
                continue; // an earlier kind's icon, or not placed in the world yet
            }
            std::optional<IconShape> shape = kind.provider(scene, entity, context);
            if (!shape)
            {
                continue;
            }
            // Where it is and which way it faces; its scale would stretch the icon, so it's left out.
            std::optional<Math::Transform> world =
                Math::fromMatrix(Math::load(registry.get<Scene::WorldTransform>(entity).matrix));
            if (!world)
            {
                continue; // a degenerate matrix (scaled to nothing): nowhere to put it
            }
            world->scale = {1.0F, 1.0F, 1.0F};
            icons.push_back({.entity = entity,
                             .world = *world,
                             .shape = std::move(*shape),
                             .selected = std::ranges::find(selection, scene.idOf(entity)) != selection.end()});
        }
    }
    return icons;
}

void registerDefaultIcons(EntityIcons& icons)
{
    registerCameraIcon(icons);
}

GizmoShapes iconShapes(const GizmoView& view, std::span<const PlacedIcon> icons)
{
    GizmoShapes out;
    const Math::Vec3 eye = eyeOf(view);
    for (const PlacedIcon& icon : icons)
    {
        const float scale = iconScale(view, icon);
        if (scale <= 0.0F)
        {
            continue; // behind the viewer
        }
        const float perPixel = scale * 1.3F / c_iconPixels;
        const Math::Color color = icon.selected           ? Math::Color{.r = 1.0F, .g = 0.55F, .b = 0.1F, .a = 1.0F}
                                  : icon.shape.emphasised ? Math::Color{.r = 0.9F, .g = 0.9F, .b = 0.9F, .a = 1.0F}
                                                          : Math::Color{.r = 0.6F, .g = 0.6F, .b = 0.62F, .a = 1.0F};
        // A line a few pixels thick: a quad along it, turned to face the viewer (as the gizmo draws).
        const auto thick = [&](const Math::Vec3& a, const Math::Vec3& b)
        {
            const Math::Vec3 middle = scaled(add(a, b), 0.5F);
            const Math::Vec3 side = cross(sub(b, a), sub(eye, middle));
            const float sideLength = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
            if (sideLength <= 0.0F)
            {
                return;
            }
            const Math::Vec3 offset = scaled(side, 0.5F * c_lineWidthPixels * perPixel / sideLength);
            out.triangles.push_back({.corners = {sub(a, offset), add(a, offset), add(b, offset)}, .color = color});
            out.triangles.push_back({.corners = {sub(a, offset), add(b, offset), sub(b, offset)}, .color = color});
        };
        for (const auto& [a, b] : icon.shape.edges)
        {
            thick(toWorld(icon, scale, a), toWorld(icon, scale, b));
        }
        for (const std::array<Math::Vec3, 3>& fill : icon.shape.fills)
        {
            out.triangles.push_back({.corners = {toWorld(icon, scale, fill[0]), toWorld(icon, scale, fill[1]),
                                                 toWorld(icon, scale, fill[2])},
                                     .color = color});
        }
        if (icon.selected)
        {
            for (const auto& [a, b] : icon.shape.selectedLines)
            {
                out.lines.push_back({.from = toWorld(icon, 1.0F, a), .to = toWorld(icon, 1.0F, b), .color = color});
            }
        }
    }
    return out;
}

std::optional<entt::entity> pickIcon(const GizmoView& view, std::span<const PlacedIcon> icons, float x, float y)
{
    std::optional<entt::entity> best;
    float bestDepth = -std::numeric_limits<float>::infinity();
    for (const PlacedIcon& icon : icons)
    {
        const float scale = iconScale(view, icon);
        if (scale <= 0.0F)
        {
            continue;
        }
        const std::optional<Math::Vec3> centre =
            Math::worldToScreen(toWorld(icon, scale, icon.shape.centre), view.matrices, view.viewport);
        if (!centre)
        {
            continue;
        }
        constexpr float c_radiusPixels = c_iconPixels * 0.5F;
        // Reversed Z: a larger depth is nearer.
        if (std::hypot(centre->x - x, centre->y - y) <= c_radiusPixels && centre->z > bestDepth)
        {
            best = icon.entity;
            bestDepth = centre->z;
        }
    }
    return best;
}
} // namespace Meow2::Editor
