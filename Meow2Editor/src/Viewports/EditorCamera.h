#pragma once

/// @file
/// @brief EditorCamera: the editor viewport's own camera, an orbit with fly on the right mouse button.

#include <Meow2/Math/CameraRig.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <QString>

namespace Meow2::Editor
{
/// @brief How the editor camera moves without a button held.
enum class CameraMode
{
    Orbit, ///< Left drag turns around a point, middle drag pans, the wheel zooms; right drag flies meanwhile.
    Fly    ///< WASD/QE move, right drag looks, Shift is faster, the wheel changes speed.
};

/// @brief The mode's name for commands and the config: `orbit` or `fly`.
/// @param mode The mode.
/// @return Its name.
[[nodiscard]] QString cameraModeName(CameraMode mode);

/// @brief The camera input of a frame, from the InputSystem: mouse delta and wheel, buttons, WASD/QE and Shift. A
///        left drag counts only with Alt held, so a plain left click is free to select.
/// @param input The input system, after its beginFrame().
/// @return The input for the rigs.
[[nodiscard]] Math::CameraInput cameraInputFrom(const Input::InputSystem& input);

/// @brief The editor viewport's camera: the engine's orbit and fly rigs (Math/CameraRig.h), switched without jumps.
///
/// In Orbit mode, holding the right button flies (WASD while looking around, as in Unreal), and letting go orbits
/// again around the point ahead. focus() frames a sphere, e.g. the scene's bounds (and later the selection).
/// @par Thread safety
/// Not thread-safe; GUI thread only.
class EditorCamera
{
public:
    /// @brief A camera framing a 10 m scene around the origin, in Orbit mode.
    EditorCamera();

    /// @brief Moves the camera by a frame's input.
    /// @param input        The frame's input (cameraInputFrom()); empty when the viewport doesn't have focus.
    /// @param deltaSeconds The frame's length.
    void update(const Math::CameraInput& input, float deltaSeconds);

    /// @brief Frames a sphere: orbits its centre from the front, far enough to see all of it.
    /// @param center Its centre.
    /// @param radius Its radius; > 0.
    void focus(const Math::Vec3& center, float radius);

    /// @brief Changes the mode, keeping where the camera is and where it looks.
    /// @param mode The new mode.
    void setMode(CameraMode mode);

    /// @brief The mode.
    /// @return Orbit or Fly.
    [[nodiscard]] CameraMode mode() const
    {
        return m_mode;
    }

    /// @brief Where the camera is and which way it looks.
    /// @return Its transform.
    [[nodiscard]] Math::Transform transform() const;

    /// @brief The lens: the default one with a near plane suited to the framed size.
    /// @return The lens.
    [[nodiscard]] Math::CameraLens lens() const
    {
        return Math::sceneLens(m_radius);
    }

    /// @brief Whether it's flying now: Fly mode, or the right button held in Orbit mode (WASD moves it then, so
    ///        letter shortcuts such as the gizmo's W/E/R must not fire).
    /// @return True while flying.
    [[nodiscard]] bool flying() const
    {
        return m_flying || m_mode == CameraMode::Fly;
    }

private:
    CameraMode m_mode = CameraMode::Orbit;
    Math::OrbitCamera m_orbit;
    Math::FlyCamera m_fly;
    bool m_flying = false; ///< Flying now: Fly mode, or the right button held in Orbit mode.
    float m_radius = 5.0F; ///< The size last framed, for the lens and the fly speed.
};
} // namespace Meow2::Editor
