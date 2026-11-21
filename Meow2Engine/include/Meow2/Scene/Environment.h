#pragma once

/// @file
/// @brief Environment: a scene's sun and exposure, saved with the scene until lights are entities of their own.

#include <Meow2/Math/Color.h>
#include <Meow2/Math/Types.h>
#include <Meow2/Scene/Scene.h>

#include <optional>

namespace Meow2::Scene
{
/// @brief A scene's sun, sky light and exposure: a scene setting (environment(), setEnvironment()), one per scene,
///        saved as the scene file's `[settings.Environment]`. Plain data.
///
/// The sun is given by two angles, as a person would set it: how high it stands and which way it comes from.
struct Environment
{
    float sunElevation = 35.0F;                   ///< Degrees above the horizon: −10 (just set) to 90 (overhead).
    float sunAzimuth = 30.0F;                     ///< Degrees around +Y from +Z, towards +X.
    Math::Color sunColor{1.0F, 1.0F, 1.0F, 1.0F}; ///< Linear colour; the sky tints it further.
    float sunIntensity = 3.0F;                    ///< Multiplies sunColor (the sun's illuminance in scene units).
    float exposure = 1.0F;                        ///< Scales the final colour before the tone curve.
};

/// @brief The unit direction towards the sun.
/// @param environment The angles.
/// @return The direction (left-handed, +Y up).
[[nodiscard]] Math::Vec3 sunDirection(const Environment& environment);

/// @brief The scene's environment setting.
/// @param scene The scene.
/// @return Its Environment, or the default one when none was set.
[[nodiscard]] Environment environment(const Scene& scene);

/// @brief Sets the scene's environment.
/// @param scene       The scene.
/// @param environment The sun and exposure.
void setEnvironment(Scene& scene, const Environment& environment);

/// @brief Whether the scene has its own environment (one was set or read from its file).
/// @param scene The scene.
/// @return True if it has.
[[nodiscard]] bool hasEnvironment(const Scene& scene);

/// @brief An entity with an Environment component: how scene files before Environment became a setting kept it
///        (readScene() folds it into the setting).
/// @param scene The scene.
/// @return The first such entity, or nothing.
[[nodiscard]] std::optional<entt::entity> findEnvironment(const Scene& scene);
} // namespace Meow2::Scene
