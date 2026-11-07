/// @file
/// @brief FramePacket: what the renderer needs from a frame, copied out of the scene by the extract phase.

#pragma once

#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Scene/Scene.h>

#include <cstdint>
#include <memory_resource>
#include <vector>

namespace Meow2::Renderer
{
/// @brief Makes an entity drawn: which mesh and material it uses. Plain data; a component.
struct Renderable
{
    std::uint32_t mesh = 0;     ///< Index of the mesh.
    std::uint32_t material = 0; ///< Index of the material.
};

/// @brief The camera a frame is seen through. Plain data.
struct FrameView
{
    Math::Transform transform{}; ///< Where the camera is.
    Math::CameraLens lens{};     ///< Its projection.
    std::uint32_t width = 1;     ///< Target width in pixels.
    std::uint32_t height = 1;    ///< Target height in pixels.
};

/// @brief One frame's render data, allocated from the frame arena.
///
/// The renderer reads only this, never the scene, so the game can start on its next frame while this one is drawn, and
/// a render thread could take the packet later with nothing else changing (techstack.md §23). World matrices are camera
/// relative, so the GPU only sees small numbers (techstack.md §10).
/// @warning It lives in the frame arena, and is gone when that frame slot comes round again.
/// @par Thread safety
/// Immutable once extracted: any thread may read.
struct FramePacket
{
    /// @brief An empty packet.
    /// @param memory Where its arrays live.
    explicit FramePacket(std::pmr::memory_resource& memory)
        : instances(&memory)
    {
    }

    Shader::FrameConstants frame{};                   ///< What every shader of the view needs.
    std::pmr::vector<Shader::InstanceData> instances; ///< Every drawn entity.
};

/// @brief Copies a frame's render data out of the scene.
/// @param scene  The scene: entities with Scene::WorldTransform and Renderable. Register Renderable first.
/// @param view   The camera.
/// @param memory The frame arena.
/// @return The packet, in `memory`.
[[nodiscard]] FramePacket& extractFrame(Scene::Scene& scene, const FrameView& view, std::pmr::memory_resource& memory);
} // namespace Meow2::Renderer
