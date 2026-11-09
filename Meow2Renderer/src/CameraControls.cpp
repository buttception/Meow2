/// @file
/// @brief CameraControls (CameraControls.h).

#include "CameraControls.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Scalar.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Meow2::RendererApp
{
namespace
{
constexpr float c_turnPerPixel = 0.005F; // radians per pixel of mouse drag
constexpr float c_pitchLimit = 1.5F;     // just short of straight up or down, where yaw has no meaning
constexpr float c_spinPerSecond = 0.2F;  // the orbit's own turn, radians per second
constexpr float c_zoomPerNotch = 0.9F;   // distance factor per wheel notch
constexpr float c_speedPerNotch = 1.25F; // fly speed factor per wheel notch

Math::Vec3 add(const Math::Vec3& a, const Math::Vec3& b, float scale = 1.0F)
{
    return {a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale};
}

/// @brief The camera's right, for a yaw (horizontal: flying sideways doesn't climb).
Math::Vec3 rightOf(float yaw)
{
    return {std::cos(yaw), 0.0F, -std::sin(yaw)};
}
} // namespace

Math::Vec3 lookDirection(float yaw, float pitch)
{
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

void updateOrbit(OrbitCamera& camera, const CameraInput& input, float deltaSeconds)
{
    if (camera.spin)
    {
        camera.yaw += c_spinPerSecond * deltaSeconds;
    }
    if (input.leftDrag)
    {
        // Dragging right moves the camera right around the target: the view direction turns left.
        camera.yaw -= input.mouseDelta.x * c_turnPerPixel;
        camera.pitch = std::clamp(camera.pitch + input.mouseDelta.y * c_turnPerPixel, -c_pitchLimit, c_pitchLimit);
    }
    if (input.middleDrag)
    {
        // Panning moves the target with the mouse, at a speed that keeps it under the cursor at about this distance.
        const float scale = camera.distance * 0.0015F;
        const Math::Vec3 forward = lookDirection(camera.yaw, camera.pitch);
        const Math::Vec3 right = rightOf(camera.yaw);
        const Math::Vec3 up{forward.y * right.z - forward.z * right.y, forward.z * right.x - forward.x * right.z,
                            forward.x * right.y - forward.y * right.x};
        camera.target = add(camera.target, right, -input.mouseDelta.x * scale);
        camera.target = add(camera.target, up, input.mouseDelta.y * scale);
    }
    if (input.wheel != 0.0F)
    {
        camera.distance = std::max(camera.distance * std::pow(c_zoomPerNotch, input.wheel), 0.001F);
    }
    camera.yaw = Math::wrapAngle(camera.yaw);
}

Math::Transform orbitTransform(const OrbitCamera& camera)
{
    const Math::Vec3 forward = lookDirection(camera.yaw, camera.pitch);
    Math::Transform transform;
    transform.position = add(camera.target, forward, -camera.distance);
    transform.rotation = Math::lookRotation(forward);
    return transform;
}

void updateFly(FlyCamera& camera, const CameraInput& input, float deltaSeconds)
{
    if (input.rightDrag)
    {
        camera.yaw = Math::wrapAngle(camera.yaw + input.mouseDelta.x * c_turnPerPixel);
        camera.pitch = std::clamp(camera.pitch - input.mouseDelta.y * c_turnPerPixel, -c_pitchLimit, c_pitchLimit);
    }
    if (input.wheel != 0.0F)
    {
        camera.speed = std::clamp(camera.speed * std::pow(c_speedPerNotch, input.wheel), 0.01F, 10000.0F);
    }
    const float step = camera.speed * (input.fast ? 4.0F : 1.0F) * deltaSeconds;
    // Forward goes where the camera looks (pitch included), sideways stays level, and up is the world's up.
    camera.position = add(camera.position, lookDirection(camera.yaw, camera.pitch), input.move.z * step);
    camera.position = add(camera.position, rightOf(camera.yaw), input.move.x * step);
    camera.position = add(camera.position, Math::Vec3{0.0F, 1.0F, 0.0F}, input.move.y * step);
}

Math::Transform flyTransform(const FlyCamera& camera)
{
    Math::Transform transform;
    transform.position = camera.position;
    transform.rotation = Math::lookRotation(lookDirection(camera.yaw, camera.pitch));
    return transform;
}

Math::Transform sceneCameraTransform(const Math::Mat4& nodeWorld)
{
    Math::Transform transform;
    if (const std::optional<Math::Transform> decomposed = Math::fromMatrix(Math::load(nodeWorld)))
    {
        transform = *decomposed;
    }
    // The half turn about the node's own Y comes first, then the node's rotation.
    const Math::Quat halfTurn = Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, Math::c_pi);
    transform.rotation = Math::combine(halfTurn, transform.rotation);
    transform.scale = {1.0F, 1.0F, 1.0F};
    return transform;
}

Math::CameraLens sceneCameraLens(const Assets::ImportedCamera& camera, float sceneRadius)
{
    Math::CameraLens lens{};
    lens.nearZ = std::max(camera.nearZ, 0.0001F);
    if (camera.perspective)
    {
        lens.type = Math::ProjectionType::Perspective;
        lens.verticalFov = std::clamp(camera.verticalFov, 0.01F, 3.1F);
    }
    else
    {
        lens.type = Math::ProjectionType::Orthographic;
        lens.orthographicHeight = std::max(camera.halfHeight * 2.0F, 0.0001F);
        lens.farZ = camera.farZ > lens.nearZ ? camera.farZ : lens.nearZ + sceneRadius * 10.0F;
    }
    return lens;
}

void drawCameraWindow(CameraWindowState& state, const std::vector<SceneCameraEntry>& cameras,
                      const std::vector<Assets::ImportedAnimation>& animations, OrbitCamera& orbit, FlyCamera& fly,
                      const UiFonts& fonts)
{
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2{690.0F * scale, 10.0F * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{300.0F * scale, 0.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CAMERA###Camera"))
    {
        ImGui::End();
        return;
    }
    drawWindowAccents();
    int mode = static_cast<int>(state.mode);
    ImGui::RadioButton("ORBIT", &mode, static_cast<int>(CameraMode::Orbit));
    ImGui::SameLine();
    ImGui::RadioButton("FLY", &mode, static_cast<int>(CameraMode::Fly));
    if (!cameras.empty())
    {
        ImGui::SameLine();
        ImGui::RadioButton("SCENE CAMERA", &mode, static_cast<int>(CameraMode::SceneCamera));
    }
    state.mode = static_cast<CameraMode>(mode);

    switch (state.mode)
    {
        case CameraMode::Orbit:
            ImGui::Checkbox("SPIN", &orbit.spin);
            ImGui::TextColored(UiColors::c_textDim, "Left-drag turns, middle-drag pans, wheel zooms.");
            break;
        case CameraMode::Fly:
            ImGui::SliderFloat("SPEED", &fly.speed, 0.01F, 1000.0F, "%.2f /s", ImGuiSliderFlags_Logarithmic);
            ImGui::TextColored(UiColors::c_textDim,
                               "WASD moves, Q/E down/up, right-drag looks,\nShift is faster, wheel "
                               "sets the speed.");
            break;
        case CameraMode::SceneCamera:
        {
            state.sceneCamera = std::clamp(state.sceneCamera, 0, static_cast<int>(cameras.size()) - 1);
            pushMonoFont(fonts);
            if (ImGui::BeginListBox(
                    "##cameras", ImVec2{-1.0F, ImGui::GetTextLineHeightWithSpacing() *
                                                   static_cast<float>(std::min<std::size_t>(cameras.size(), 6) + 1)}))
            {
                for (int i = 0; i < static_cast<int>(cameras.size()); ++i)
                {
                    const SceneCameraEntry& entry = cameras[static_cast<std::size_t>(i)];
                    const std::string label = std::format("{} ({})##{}", entry.name,
                                                          entry.lens.perspective ? "perspective" : "orthographic", i);
                    if (ImGui::Selectable(label.c_str(), state.sceneCamera == i))
                    {
                        state.sceneCamera = i;
                    }
                }
                ImGui::EndListBox();
            }
            popMonoFont();
            break;
        }
    }

    if (!animations.empty())
    {
        ImGui::SeparatorText("ANIMATION");
        const auto nameOf = [&animations](int index) -> std::string
        {
            if (index < 0)
            {
                return "(none: rest pose)";
            }
            const Assets::ImportedAnimation& animation = animations[static_cast<std::size_t>(index)];
            return std::format("{} ({:.1f} s)",
                               animation.name.empty() ? std::format("Animation {}", index) : animation.name,
                               static_cast<double>(animation.duration));
        };
        if (ImGui::BeginCombo("##animation", nameOf(state.animation).c_str()))
        {
            for (int i = -1; i < static_cast<int>(animations.size()); ++i)
            {
                if (ImGui::Selectable(nameOf(i).c_str(), state.animation == i))
                {
                    state.animation = i;
                    state.animationTime = 0.0F;
                }
            }
            ImGui::EndCombo();
        }
        if (state.animation >= 0)
        {
            const float duration = animations[static_cast<std::size_t>(state.animation)].duration;
            if (ImGui::Button(state.playing ? "PAUSE" : "PLAY"))
            {
                state.playing = !state.playing;
            }
            ImGui::SameLine();
            ImGui::Checkbox("LOOP", &state.loop);
            ImGui::SliderFloat("TIME", &state.animationTime, 0.0F, std::max(duration, 0.001F), "%.2f s");
        }
    }
    ImGui::End();
}
} // namespace Meow2::RendererApp
