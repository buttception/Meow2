/// @file
/// @brief Environment (Environment.h).

#include <Meow2/Math/Scalar.h>
#include <Meow2/Scene/Environment.h>

#include <cmath>

namespace Meow2::Scene
{
Math::Vec3 sunDirection(const Environment& environment)
{
    const float elevation = Math::toRadians(environment.sunElevation);
    const float azimuth = Math::toRadians(environment.sunAzimuth);
    return {std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth)};
}

std::optional<entt::entity> findEnvironment(const Scene& scene)
{
    const Scene::Registry& registry = scene.registry();
    if (!scene.isRegistered<Environment>())
    {
        return std::nullopt;
    }
    // The storage directly: EnTT views over a const registry need an allocator with a default constructor, which the
    // scene's hasn't.
    const auto* storage = registry.storage<Environment>();
    if (storage == nullptr || storage->empty())
    {
        return std::nullopt;
    }
    // A storage iterates its components; each() gives the entity with each.
    return std::get<0>(*storage->each().begin());
}

// The setting lives in the registry's context: one value per scene, outside any entity, gone with the scene.
Environment environment(const Scene& scene)
{
    const Environment* found = scene.registry().ctx().find<Environment>();
    return found != nullptr ? *found : Environment{};
}

void setEnvironment(Scene& scene, const Environment& environment)
{
    scene.registry().ctx().insert_or_assign(environment);
}

bool hasEnvironment(const Scene& scene)
{
    return scene.registry().ctx().contains<Environment>();
}
} // namespace Meow2::Scene
