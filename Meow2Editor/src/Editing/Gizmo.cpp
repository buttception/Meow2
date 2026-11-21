/// @file
/// @brief Gizmo (Gizmo.h).

#include "Editing/Gizmo.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Ray.h>
#include <Meow2/Math/Scalar.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Meow2::Editor
{
namespace
{
using namespace DirectX;

constexpr std::array<GizmoHandle, 3> c_axes{GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
constexpr int c_ringSegments = 48;          ///< Segments per rotate ring.
constexpr float c_headLength = 0.2F;        ///< Arrowhead length, as a fraction of the gizmo's size.
constexpr float c_headRadius = 0.06F;       ///< Arrowhead and box half-size, likewise.
constexpr float c_uniformRadius = 0.09F;    ///< The centre box's half-size, likewise.
constexpr float c_uniformHitPixels = 12.0F; ///< The centre box is grabbed within this many pixels of the centre.
constexpr float c_parallel = 1e-4F;         ///< Below this, a ray counts as parallel to an axis or plane.

/// @brief The colour of a handle; the highlighted one is yellow. Bright, since the lines are drawn before the tone map.
Math::Color colorOf(GizmoHandle handle, GizmoHandle highlight)
{
    if (handle == highlight)
    {
        return {.r = 1.0F, .g = 0.85F, .b = 0.1F, .a = 1.0F};
    }
    switch (handle)
    {
        case GizmoHandle::X:
            return {.r = 1.0F, .g = 0.12F, .b = 0.12F, .a = 1.0F};
        case GizmoHandle::Y:
            return {.r = 0.15F, .g = 0.9F, .b = 0.15F, .a = 1.0F};
        case GizmoHandle::Z:
            return {.r = 0.2F, .g = 0.4F, .b = 1.0F, .a = 1.0F};
        default:
            return {.r = 0.85F, .g = 0.85F, .b = 0.85F, .a = 1.0F};
    }
}

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

float dot(const Math::Vec3& a, const Math::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Math::Vec3 cross(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Math::Vec3 normalized(const Math::Vec3& v)
{
    const float length = std::sqrt(dot(v, v));
    return length > 0.0F ? scaled(v, 1.0F / length) : v;
}

/// @brief The unit axis of a handle (X, Y or Z) in world or local space.
Math::Vec3 axisOf(GizmoHandle handle, const Math::Quat& rotation, bool local)
{
    const Math::Vec3 world = handle == GizmoHandle::X   ? Math::c_right
                             : handle == GizmoHandle::Y ? Math::c_up
                                                        : Math::c_forward;
    return local ? normalized(Math::rotate(rotation, world)) : world;
}

/// @brief Two unit vectors at right angles to a unit axis and to each other.
std::pair<Math::Vec3, Math::Vec3> basisAround(const Math::Vec3& axis)
{
    const Math::Vec3 helper = std::abs(axis.y) < 0.9F ? Math::c_up : Math::c_right;
    const Math::Vec3 u = normalized(cross(helper, axis));
    return {u, cross(axis, u)};
}

/// @brief World units per pixel at a point: how far one pixel reaches there, measured along the camera's up.
float worldPerPixel(const GizmoView& view, const Math::Vec3& point)
{
    // Row vectors: the view matrix's second column is the camera's up in world space.
    const Math::Mat4& m = view.matrices.view;
    const Math::Vec3 cameraUp{m._12, m._22, m._32};
    const std::optional<Math::Vec3> a = Math::worldToScreen(point, view.matrices, view.viewport);
    const std::optional<Math::Vec3> b = Math::worldToScreen(add(point, cameraUp), view.matrices, view.viewport);
    if (!a || !b)
    {
        return 0.0F; // behind the camera: nothing to draw or grab
    }
    const float pixels = std::hypot(b->x - a->x, b->y - a->y);
    return pixels > 0.0F ? 1.0F / pixels : 0.0F;
}

/// @brief The distance in pixels from a point to a segment on screen.
float distanceToSegment(float x, float y, const Math::Vec3& a, const Math::Vec3& b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    const float t =
        lengthSquared > 0.0F ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / lengthSquared, 0.0F, 1.0F) : 0.0F;
    return std::hypot(x - (a.x + t * dx), y - (a.y + t * dy));
}

/// @brief Where on the line `point + t·axis` the ray passes closest: t, or nothing if they're parallel.
std::optional<float> closestOnLine(const Math::Ray& ray, const Math::Vec3& point, const Math::Vec3& axis)
{
    const Math::Vec3 w = sub(ray.origin, point);
    const float b = dot(axis, ray.direction);
    const float denominator = 1.0F - b * b;
    if (denominator < c_parallel)
    {
        return std::nullopt;
    }
    return (dot(w, axis) - dot(w, ray.direction) * b) / denominator;
}

/// @brief Where the ray meets the plane through `point` across `axis`, as a unit vector from `point`.
std::optional<Math::Vec3> ringVector(const Math::Ray& ray, const Math::Vec3& point, const Math::Vec3& axis)
{
    const float facing = dot(ray.direction, axis);
    if (std::abs(facing) < c_parallel)
    {
        return std::nullopt; // the ring is seen edge-on
    }
    const float t = dot(sub(point, ray.origin), axis) / facing;
    const Math::Vec3 hit = add(ray.origin, scaled(ray.direction, t));
    const Math::Vec3 offset = sub(hit, point);
    if (dot(offset, offset) < 1e-12F)
    {
        return std::nullopt;
    }
    return normalized(offset);
}

/// @brief Rounds a value to whole steps.
float snapTo(float value, float step)
{
    return step > 0.0F ? std::round(value / step) * step : value;
}
} // namespace

const char* gizmoModeName(GizmoMode mode)
{
    switch (mode)
    {
        case GizmoMode::Rotate:
            return "rotate";
        case GizmoMode::Scale:
            return "scale";
        default:
            return "translate";
    }
}

void Gizmo::setMode(GizmoMode mode)
{
    m_mode = mode;
    endDrag();
}

void Gizmo::setSpace(GizmoSpace space)
{
    m_space = space;
    endDrag();
}

std::vector<Renderer::DebugLine> Gizmo::lines(const GizmoView& view, const Math::Transform& target,
                                              GizmoHandle highlight) const
{
    std::vector<Renderer::DebugLine> out;
    const Math::Vec3 center = target.position;
    const float size = worldPerPixel(view, center) * c_sizePixels;
    if (size <= 0.0F)
    {
        return out;
    }
    // Scale is always along the entity's own axes; it's the only way a scale means anything.
    const bool local = m_space == GizmoSpace::Local || m_mode == GizmoMode::Scale;
    const auto line = [&out](const Math::Vec3& from, const Math::Vec3& to, const Math::Color& color)
    {
        out.push_back({.from = from, .to = to, .color = color});
    };
    // A box's twelve edges around a point, along three directions.
    const auto box = [&line](const Math::Vec3& at, const Math::Vec3& a, const Math::Vec3& b, const Math::Vec3& c,
                             float half, const Math::Color& color)
    {
        std::array<Math::Vec3, 8> corners;
        for (int i = 0; i < 8; ++i)
        {
            const float sa = (i & 1) != 0 ? half : -half;
            const float sb = (i & 2) != 0 ? half : -half;
            const float sc = (i & 4) != 0 ? half : -half;
            corners[static_cast<std::size_t>(i)] = add(at, add(scaled(a, sa), add(scaled(b, sb), scaled(c, sc))));
        }
        for (int i = 0; i < 8; ++i)
        {
            for (const int bit : {1, 2, 4})
            {
                if ((i & bit) == 0)
                {
                    line(corners[static_cast<std::size_t>(i)], corners[static_cast<std::size_t>(i | bit)], color);
                }
            }
        }
    };

    for (const GizmoHandle handle : c_axes)
    {
        const Math::Vec3 axis = axisOf(handle, target.rotation, local);
        const auto [u, v] = basisAround(axis);
        const Math::Color color = colorOf(handle, highlight);
        const Math::Vec3 tip = add(center, scaled(axis, size));
        switch (m_mode)
        {
            case GizmoMode::Translate:
            {
                line(center, tip, color);
                const Math::Vec3 base = add(center, scaled(axis, size * (1.0F - c_headLength)));
                const float radius = size * c_headRadius;
                const std::array<Math::Vec3, 4> rim{add(base, scaled(u, radius)), add(base, scaled(v, radius)),
                                                    add(base, scaled(u, -radius)), add(base, scaled(v, -radius))};
                for (std::size_t i = 0; i < rim.size(); ++i)
                {
                    line(tip, rim[i], color);
                    line(rim[i], rim[(i + 1) % rim.size()], color);
                }
                break;
            }
            case GizmoMode::Rotate:
                for (int i = 0; i < c_ringSegments; ++i)
                {
                    const auto point = [&](int k)
                    {
                        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(k) /
                                            static_cast<float>(c_ringSegments);
                        return add(center, add(scaled(u, std::cos(angle) * size), scaled(v, std::sin(angle) * size)));
                    };
                    line(point(i), point(i + 1), color);
                }
                break;
            case GizmoMode::Scale:
                line(center, tip, color);
                box(tip, axis, u, v, size * c_headRadius, color);
                break;
        }
    }
    if (m_mode == GizmoMode::Scale)
    {
        box(center, axisOf(GizmoHandle::X, target.rotation, true), axisOf(GizmoHandle::Y, target.rotation, true),
            axisOf(GizmoHandle::Z, target.rotation, true), size * c_uniformRadius,
            colorOf(GizmoHandle::Uniform, highlight));
    }
    return out;
}

GizmoHandle Gizmo::hitTest(const GizmoView& view, const Math::Transform& target, float x, float y) const
{
    const Math::Vec3 center = target.position;
    const float size = worldPerPixel(view, center) * c_sizePixels;
    const std::optional<Math::Vec3> centerOnScreen = Math::worldToScreen(center, view.matrices, view.viewport);
    if (size <= 0.0F || !centerOnScreen)
    {
        return GizmoHandle::None;
    }
    if (m_mode == GizmoMode::Scale && std::hypot(x - centerOnScreen->x, y - centerOnScreen->y) <= c_uniformHitPixels)
    {
        return GizmoHandle::Uniform; // the centre box wins over the axes, which all start there
    }
    const bool local = m_space == GizmoSpace::Local || m_mode == GizmoMode::Scale;
    GizmoHandle best = GizmoHandle::None;
    float bestDistance = c_hitPixels;
    for (const GizmoHandle handle : c_axes)
    {
        const Math::Vec3 axis = axisOf(handle, target.rotation, local);
        float distance = bestDistance + 1.0F;
        if (m_mode == GizmoMode::Rotate)
        {
            const auto [u, v] = basisAround(axis);
            std::optional<Math::Vec3> previous;
            for (int i = 0; i <= c_ringSegments; ++i)
            {
                const float angle =
                    2.0F * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(c_ringSegments);
                const Math::Vec3 point =
                    add(center, add(scaled(u, std::cos(angle) * size), scaled(v, std::sin(angle) * size)));
                const std::optional<Math::Vec3> onScreen = Math::worldToScreen(point, view.matrices, view.viewport);
                if (previous && onScreen)
                {
                    distance = std::min(distance, distanceToSegment(x, y, *previous, *onScreen));
                }
                previous = onScreen;
            }
        }
        else if (const std::optional<Math::Vec3> tip =
                     Math::worldToScreen(add(center, scaled(axis, size)), view.matrices, view.viewport))
        {
            distance = distanceToSegment(x, y, *centerOnScreen, *tip);
        }
        if (distance <= bestDistance)
        {
            best = handle;
            bestDistance = distance;
        }
    }
    return best;
}

bool Gizmo::beginDrag(const GizmoView& view, const Math::Transform& target, GizmoHandle handle, float x, float y)
{
    m_handle = GizmoHandle::None;
    if (handle == GizmoHandle::None)
    {
        return false;
    }
    m_start = target;
    m_grabX = x;
    if (handle == GizmoHandle::Uniform)
    {
        m_handle = handle;
        return true;
    }
    const bool local = m_space == GizmoSpace::Local || m_mode == GizmoMode::Scale;
    m_axis = axisOf(handle, target.rotation, local);
    const Math::Ray ray = Math::rayFromScreen(x, y, view.matrices, view.viewport);
    if (m_mode == GizmoMode::Rotate)
    {
        const std::optional<Math::Vec3> grabbed = ringVector(ray, target.position, m_axis);
        if (!grabbed)
        {
            return false;
        }
        m_grabVector = *grabbed;
    }
    else
    {
        const std::optional<float> grabbed = closestOnLine(ray, target.position, m_axis);
        if (!grabbed || (m_mode == GizmoMode::Scale && std::abs(*grabbed) < 1e-6F))
        {
            return false;
        }
        m_grabParam = *grabbed;
    }
    m_handle = handle;
    return true;
}

std::optional<Math::Transform> Gizmo::drag(const GizmoView& view, float x, float y, const GizmoSnap& snap) const
{
    if (m_handle == GizmoHandle::None)
    {
        return std::nullopt;
    }
    Math::Transform result = m_start;
    const auto snapScale = [&snap](float value)
    {
        const float snapped = snap.enabled ? snapTo(value, snap.scale) : value;
        // A scale of 0 would collapse the entity (and its matrix couldn't be inverted): keep a sliver.
        return std::abs(snapped) < 1e-3F ? std::copysign(1e-3F, value == 0.0F ? 1.0F : value) : snapped;
    };
    if (m_handle == GizmoHandle::Uniform)
    {
        // Sideways movement on screen: a gizmo's length to the right doubles the size, to the left shrinks it.
        const float factor = std::max(1.0F + (x - m_grabX) / c_sizePixels, 0.01F);
        result.scale = {snapScale(m_start.scale.x * factor), snapScale(m_start.scale.y * factor),
                        snapScale(m_start.scale.z * factor)};
        return result;
    }
    const Math::Ray ray = Math::rayFromScreen(x, y, view.matrices, view.viewport);
    switch (m_mode)
    {
        case GizmoMode::Translate:
            if (const std::optional<float> t = closestOnLine(ray, m_start.position, m_axis))
            {
                const float moved = snap.enabled ? snapTo(*t - m_grabParam, snap.translate) : *t - m_grabParam;
                result.position = add(m_start.position, scaled(m_axis, moved));
            }
            break;
        case GizmoMode::Rotate:
            if (const std::optional<Math::Vec3> now = ringVector(ray, m_start.position, m_axis))
            {
                float angle = std::atan2(dot(cross(m_grabVector, *now), m_axis), dot(m_grabVector, *now));
                if (snap.enabled)
                {
                    angle = snapTo(angle, Math::toRadians(snap.angleDegrees));
                }
                // The turn about a world-space axis, after the rotation it had: for a local axis that is the same as
                // turning about the entity's own axis.
                result.rotation = Math::normalize(Math::combine(m_start.rotation, Math::fromAxisAngle(m_axis, angle)));
            }
            break;
        case GizmoMode::Scale:
            if (const std::optional<float> t = closestOnLine(ray, m_start.position, m_axis))
            {
                const float factor = *t / m_grabParam;
                float* component = m_handle == GizmoHandle::X   ? &result.scale.x
                                   : m_handle == GizmoHandle::Y ? &result.scale.y
                                                                : &result.scale.z;
                *component = snapScale(*component * factor);
            }
            break;
    }
    return result;
}
} // namespace Meow2::Editor
