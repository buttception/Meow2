/// @file
/// @brief Cascaded shadow maps for the sun, the CPU side: where each cascade's shadow map sits, and when it moved (so a
///        cached map of static casters must be drawn again).

#pragma once

#include <Meow2/Math/Bounds.h>
#include <Meow2/Math/Types.h>

#include <array>
#include <cstdint>
#include <span>

namespace Meow2::Renderer
{
/// @brief The most cascades a sun shadow has.
inline constexpr std::uint32_t c_maxShadowCascades = 4;

/// @brief How the sun's shadow is laid out. Plain data.
struct CascadeSettings
{
    std::uint32_t count = 4;         ///< Cascades, 1..c_maxShadowCascades.
    std::uint32_t resolution = 2048; ///< Texels per side of each cascade's map.
    float distance = 100.0F;         ///< How far from the camera shadows reach, in metres (view distance).
    float splitLambda = 0.8F;        ///< Where the splits go: 0 evenly spaced, 1 logarithmic (more near the camera).
    float padding = 1.25F;           ///< Each map covers this many times its slice's radius: room to move before it
                                     ///< has to (and its cached static casters with it).
};

/// @brief One cascade: a slice of the camera's view, and the orthographic box the sun sees it through. Plain data.
struct ShadowCascade
{
    Math::Mat4 viewProjection{}; ///< World → the cascade's clip space (reversed Z: 1 nearest the sun, 0 farthest).
    Math::Vec3 lightCenter{};    ///< The box's centre in the light's rotated space (x, y snapped to whole texels).
    float radius = 0.0F;         ///< Half the box's width, in metres.
    float nearZ = 0.0F;          ///< The box's extent along the light, in the light's rotated space.
    float farZ = 0.0F;           ///< Ditto.
    float splitNear = 0.0F;      ///< View distance where this cascade starts.
    float splitFar = 0.0F;       ///< View distance where it ends.
    float texelSize = 0.0F;      ///< World size of one texel, in metres (for normal-offset bias).
};

/// @brief Where the cascades split the view distance: a blend of evenly spaced and logarithmic splits (the "practical"
///        scheme), which keeps near cascades small, where detail matters, without starving the far ones.
/// @param nearZ    The camera's near plane, > 0.
/// @param distance How far shadows reach, > nearZ.
/// @param count    Cascades, 1..c_maxShadowCascades.
/// @param lambda   0 evenly spaced … 1 logarithmic.
/// @return `count + 1` distances: nearZ, then each cascade's far end; the last is `distance`. The rest are unused.
[[nodiscard]] std::array<float, c_maxShadowCascades + 1> cascadeSplits(float nearZ, float distance, std::uint32_t count,
                                                                       float lambda);

/// @brief The radius of a sphere around a slice of a symmetric perspective view, centred on its axis. It depends only
///        on the slice and the field of view, not on where the camera looks, so a cascade fitted to it keeps its size
///        (and its texels) while the camera turns: no shimmering edges.
/// @param splitNear Slice start, view distance.
/// @param splitFar  Slice end.
/// @param tanHalfX  tan of half the horizontal field of view.
/// @param tanHalfY  tan of half the vertical field of view.
/// @return The radius; every corner of the slice is within it of the point on the axis at (splitNear + splitFar) / 2.
[[nodiscard]] float sliceRadius(float splitNear, float splitFar, float tanHalfX, float tanHalfY);

/// @brief The camera as the cascades need it. Plain data.
struct CascadeCamera
{
    Math::Mat4 view{};     ///< World → view space (FrameConstants::view).
    float tanHalfX = 1.0F; ///< tan of half the horizontal field of view (1 / projection._11).
    float tanHalfY = 1.0F; ///< tan of half the vertical field of view (1 / projection._22).
    float nearZ = 0.1F;    ///< The near plane (projection._43 with reversed infinite Z).
};

/// @brief Keeps the sun's cascades from frame to frame, moving each only when it must.
///
/// Each cascade is a box the sun looks through, `padding` times the radius of the camera's slice. While the slice stays
/// inside the box, the box stays put, and so does a cached map of the static casters in it. When the slice leaves, the
/// box moves to the slice, snapped to whole texels so the shadow doesn't crawl. The box's depth covers the casters'
/// sphere, so objects between the sun and the slice cast into it.
/// @par Thread safety
/// Not thread-safe; use from one thread.
class ShadowCascades
{
public:
    /// @brief No cascades until the first update().
    /// @param settings The layout.
    explicit ShadowCascades(const CascadeSettings& settings = {});

    /// @brief Changes the layout; every cascade moves at the next update().
    /// @param settings The layout.
    void setSettings(const CascadeSettings& settings);

    /// @brief The layout.
    /// @return It.
    [[nodiscard]] const CascadeSettings& settings() const
    {
        return m_settings;
    }

    /// @brief Fits the cascades to this frame's camera and sun.
    /// @param camera       The camera.
    /// @param sunDirection Unit vector towards the sun.
    /// @param casters      A sphere around everything that casts a shadow (the scene's bounds).
    /// @return A bit per cascade that moved (or is new): its cached static casters must be drawn again.
    std::uint32_t update(const CascadeCamera& camera, const Math::Vec3& sunDirection, const Math::Sphere& casters);

    /// @brief The cascades, after update().
    /// @return settings().count of them.
    [[nodiscard]] std::span<const ShadowCascade> cascades() const
    {
        return {m_cascades.data(), m_settings.count};
    }

private:
    CascadeSettings m_settings;
    std::array<ShadowCascade, c_maxShadowCascades> m_cascades{};
    Math::Vec3 m_sunDirection{};
    bool m_valid = false; ///< Whether the cascades came from an earlier update() with these settings.
};
} // namespace Meow2::Renderer
