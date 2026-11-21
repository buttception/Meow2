#pragma once

/// @file
/// @brief Gizmo: the editor's translate, rotate and scale handles: what they look like, what the cursor is over, and
///        what a drag does to a transform. Plain maths, no Qt and no GPU.

#include <Meow2/Graphics/Renderer/DebugLines.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Projection.h>
#include <Meow2/Math/Transform.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace Meow2::Editor
{
/// @brief What a gizmo does.
enum class GizmoMode : std::uint8_t
{
    Translate, ///< Arrows: move along an axis.
    Rotate,    ///< Rings: turn about an axis.
    Scale      ///< Boxes: scale along an axis, or all at once from the centre.
};

/// @brief Which axes a gizmo uses for translate and rotate (scale is always along the entity's own axes).
enum class GizmoSpace : std::uint8_t
{
    World, ///< The world's X, Y and Z.
    Local  ///< The entity's own axes (its world rotation).
};

/// @brief A part of the gizmo the cursor can grab.
enum class GizmoHandle : std::uint8_t
{
    None,   ///< Nothing.
    X,      ///< The X axis (arrow, ring or box).
    Y,      ///< The Y axis.
    Z,      ///< The Z axis.
    Uniform ///< Scale mode's centre box: all three axes at once.
};

/// @brief The camera a gizmo is seen through. Plain data.
struct GizmoView
{
    Math::CameraMatrices matrices{}; ///< The viewport camera's matrices.
    Math::Viewport viewport{};       ///< The viewport in pixels (D3D form: y down from the top).
};

/// @brief Snapping while dragging. Plain data.
struct GizmoSnap
{
    bool enabled = false;       ///< Whether to snap.
    float translate = 0.5F;     ///< Translate step along the axis, in world units (metres).
    float angleDegrees = 15.0F; ///< Rotate step, in degrees.
    float scale = 0.1F;         ///< Scale step: the scale snaps to multiples of it.
};

/// @brief The name of a mode for commands: `translate`, `rotate` or `scale`.
/// @param mode The mode.
/// @return Its name.
[[nodiscard]] const char* gizmoModeName(GizmoMode mode);

/// @brief The editor's transform gizmo for one entity.
///
/// - **Size:** the handles are c_sizePixels long on screen wherever the entity is, so they're as easy to grab far away
///   as near.
/// - **Hit tests** are in screen space: the closest handle within c_hitPixels of the cursor (the arrows and boxes as
///   segments, the rings as polylines).
/// - **Dragging** works on the entity's *world* transform from when the drag began, and returns the new world
///   transform; the caller turns it into a local one. Translate moves to the point on the axis line closest to the
///   cursor's ray; rotate turns by the angle the cursor sweeps in the plane across the axis; scale multiplies by how
///   far along the axis the cursor is compared with where it grabbed (the centre box: by how far it moved sideways).
///   Snapping rounds the *change*: whole steps along the axis, whole angle steps, scales to multiples of the step.
/// @par Thread safety
/// Not thread-safe.
class Gizmo
{
public:
    static constexpr float c_sizePixels = 100.0F; ///< How long the handles are on screen.
    static constexpr float c_hitPixels = 8.0F;    ///< How close the cursor must be to grab one.

    /// @brief Sets what the gizmo does; ends a drag.
    /// @param mode The mode.
    void setMode(GizmoMode mode);

    /// @brief What it does.
    /// @return The mode.
    [[nodiscard]] GizmoMode mode() const
    {
        return m_mode;
    }

    /// @brief Sets world or local axes; ends a drag.
    /// @param space The space.
    void setSpace(GizmoSpace space);

    /// @brief World or local axes.
    /// @return The space.
    [[nodiscard]] GizmoSpace space() const
    {
        return m_space;
    }

    /// @brief The handles as lines in world space, X red, Y green, Z blue, the highlighted one yellow.
    /// @param view      The camera.
    /// @param target    The entity's world transform.
    /// @param highlight The hovered (or dragged) handle.
    /// @return The lines.
    [[nodiscard]] std::vector<Renderer::DebugLine> lines(const GizmoView& view, const Math::Transform& target,
                                                         GizmoHandle highlight) const;

    /// @brief The handle under the cursor.
    /// @param view   The camera.
    /// @param target The entity's world transform.
    /// @param x      The cursor, in viewport pixels.
    /// @param y      The cursor.
    /// @return The closest handle within c_hitPixels, or None.
    [[nodiscard]] GizmoHandle hitTest(const GizmoView& view, const Math::Transform& target, float x, float y) const;

    /// @brief Starts dragging a handle.
    /// @param view   The camera.
    /// @param target The entity's world transform now.
    /// @param handle The handle grabbed (from hitTest()); None does nothing.
    /// @param x      The cursor, in viewport pixels.
    /// @param y      The cursor.
    /// @return True if a drag started (false for None, or an axis seen end-on that can't be dragged).
    bool beginDrag(const GizmoView& view, const Math::Transform& target, GizmoHandle handle, float x, float y);

    /// @brief The world transform for the cursor's position in the drag.
    /// @param view The camera (it may have moved since the drag began).
    /// @param x    The cursor, in viewport pixels.
    /// @param y    The cursor.
    /// @param snap Snapping.
    /// @return The new world transform; the one from when the drag began while the cursor's ray misses (parallel to
    ///         the axis or its plane); nothing when not dragging.
    [[nodiscard]] std::optional<Math::Transform> drag(const GizmoView& view, float x, float y,
                                                      const GizmoSnap& snap) const;

    /// @brief Ends the drag.
    void endDrag()
    {
        m_handle = GizmoHandle::None;
    }

    /// @brief The handle being dragged.
    /// @return It, or None.
    [[nodiscard]] GizmoHandle dragged() const
    {
        return m_handle;
    }

    /// @brief The world transform when the drag began.
    /// @return It (meaningless when not dragging).
    [[nodiscard]] const Math::Transform& dragStart() const
    {
        return m_start;
    }

private:
    GizmoMode m_mode = GizmoMode::Translate;
    GizmoSpace m_space = GizmoSpace::World;
    GizmoHandle m_handle = GizmoHandle::None; ///< Being dragged.
    Math::Transform m_start{};                ///< The world transform when the drag began.
    Math::Vec3 m_axis{};                      ///< The dragged axis in world space (unit).
    float m_grabParam = 0.0F;                 ///< Translate, scale: where on the axis line it was grabbed.
    Math::Vec3 m_grabVector{};                ///< Rotate: from the centre to where the ring's plane was grabbed (unit).
    float m_grabX = 0.0F;                     ///< Uniform scale: the cursor's x when grabbed.
};
} // namespace Meow2::Editor
