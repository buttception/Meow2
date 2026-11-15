/// @file
/// @brief EditorCamera (EditorCamera.h).

#include "Viewports/EditorCamera.h"

#include <algorithm>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

QString cameraModeName(CameraMode mode)
{
    return mode == CameraMode::Fly ? u"fly"_s : u"orbit"_s;
}

Math::CameraInput cameraInputFrom(const Input::InputSystem& input)
{
    using Input::Key;
    Math::CameraInput camera{};
    const Input::Vec2 delta = input.mouseDelta();
    camera.mouseDelta = {delta.x, delta.y};
    camera.wheel = input.wheelDelta().y;
    camera.leftDrag = input.isDown(Input::MouseButton::Left);
    camera.middleDrag = input.isDown(Input::MouseButton::Middle);
    camera.rightDrag = input.isDown(Input::MouseButton::Right);
    const auto axis = [&input](Key plus, Key minus)
    {
        return (input.isDown(plus) ? 1.0F : 0.0F) - (input.isDown(minus) ? 1.0F : 0.0F);
    };
    camera.move = {axis(Key::D, Key::A), axis(Key::E, Key::Q), axis(Key::W, Key::S)};
    camera.fast = input.isDown(Key::LeftShift) || input.isDown(Key::RightShift);
    return camera;
}

EditorCamera::EditorCamera()
{
    focus({0.0F, 0.0F, 0.0F}, 5.0F);
}

void EditorCamera::update(const Math::CameraInput& input, float deltaSeconds)
{
    const bool fly = m_mode == CameraMode::Fly || input.rightDrag;
    if (fly && !m_flying)
    {
        // Starting to fly from the orbit: from where the orbit puts the camera, looking the same way.
        m_fly = Math::flyFrom(Math::orbitTransform(m_orbit), m_fly.speed);
        m_flying = true;
    }
    else if (!fly && m_flying)
    {
        // Back to orbiting, around the point as far ahead as the orbit was: no jump.
        m_orbit = Math::orbitFrom(Math::flyTransform(m_fly), m_orbit.distance);
        m_flying = false;
    }
    if (m_flying)
    {
        Math::updateFly(m_fly, input, deltaSeconds);
    }
    else
    {
        Math::updateOrbit(m_orbit, input, deltaSeconds);
    }
}

void EditorCamera::focus(const Math::Vec3& center, float radius)
{
    m_radius = std::max(radius, 1e-3F);
    m_orbit = Math::frameScene(center, m_radius);
    m_orbit.spin = false; // an editor camera stays where it's put
    m_fly = Math::flyFrom(Math::orbitTransform(m_orbit), std::max(m_radius * 0.5F, 0.1F));
    m_flying = m_mode == CameraMode::Fly;
}

void EditorCamera::setMode(CameraMode mode)
{
    if (mode == m_mode)
    {
        return;
    }
    const Math::Transform current = transform();
    m_mode = mode;
    if (mode == CameraMode::Fly)
    {
        m_fly = Math::flyFrom(current, m_fly.speed);
        m_flying = true;
    }
    else
    {
        m_orbit = Math::orbitFrom(current, m_orbit.distance);
        m_flying = false;
    }
}

Math::Transform EditorCamera::transform() const
{
    return m_flying ? Math::flyTransform(m_fly) : Math::orbitTransform(m_orbit);
}
} // namespace Meow2::Editor
