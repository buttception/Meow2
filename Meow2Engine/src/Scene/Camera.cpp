/// @file
/// @brief The Camera component's helpers (Camera.h).

#include "Meow2/Scene/Camera.h"

#include "Meow2/Scene/TransformHierarchy.h"

namespace Meow2::Scene
{
std::optional<entt::entity> findMainCamera(Scene& scene)
{
    // One storage walked: a view over two types needs an allocator with a default constructor (TransformHierarchy.cpp).
    std::optional<entt::entity> first;
    for (const auto [entity, camera] : scene.registry().storage<Camera>().each())
    {
        if (camera.main)
        {
            return entity;
        }
        if (!first)
        {
            first = entity;
        }
    }
    return first;
}

std::optional<Math::Transform> cameraTransform(Scene& scene, entt::entity camera)
{
    const auto* world = scene.registry().try_get<WorldTransform>(camera);
    if (world == nullptr)
    {
        return std::nullopt;
    }
    std::optional<Math::Transform> transform = Math::fromMatrix(Math::load(world->matrix));
    if (transform)
    {
        transform->scale = {1.0F, 1.0F, 1.0F};
    }
    return transform;
}
} // namespace Meow2::Scene
