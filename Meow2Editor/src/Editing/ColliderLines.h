#pragma once

/// @file
/// @brief ColliderLines: the selected entities' Colliders drawn as green outlines in the editor viewport.

#include <Meow2/Graphics/Renderer/DebugLines.h>
#include <Meow2/Physics/CollisionMeshes.h>
#include <Meow2/Physics/PhysicsComponents.h>
#include <Meow2/Scene/Scene.h>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace Meow2::Editor
{
/// @brief The colour of collider outlines (a bright green, apart from the gizmo's red, green and blue).
inline constexpr Math::Color c_colliderColor{0.35F, 1.0F, 0.45F, 1.0F};

/// @brief Outlines of the Colliders on the selected entities and the entities under them, in world space, sized as
///        physics sizes them (PhysicsScene: the entity's world scale, the largest axis for a sphere's radius):
/// - a box: its twelve edges;
/// - a sphere: three circles, one around each of the entity's axes;
/// - a capsule: a ring at each end of the cylinder, four lines along it, and the caps' half circles;
/// - a mesh: the box around its triangles (all of them would be thousands of lines).
///
/// Unity shows colliders the same way: only for what's selected, so a scene full of them stays readable.
/// @param scene    The scene (with world transforms up to date).
/// @param selected The selected entities.
/// @param meshes   Mesh triangles, for mesh Colliders' boxes.
/// @param meshOf   Which mesh an entity shows (its MeshRenderer).
/// @return The lines.
[[nodiscard]] std::vector<Renderer::DebugLine>
colliderLines(const Scene::Scene& scene, std::span<const entt::entity> selected, const Physics::CollisionMeshes& meshes,
              const std::function<std::optional<AssetId>(entt::entity)>& meshOf);
} // namespace Meow2::Editor
