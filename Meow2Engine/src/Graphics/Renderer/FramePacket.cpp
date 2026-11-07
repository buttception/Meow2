/// @file
/// @brief extractFrame(): copies render data out of the scene.

#include "Meow2/Graphics/Renderer/FramePacket.h"

#include "Meow2/Scene/TransformHierarchy.h"

#include <DirectXMath.h>

#include <algorithm>
#include <memory_resource>

namespace Meow2::Renderer
{
FramePacket& extractFrame(Scene::Scene& scene, const FrameView& view, std::pmr::memory_resource& memory)
{
    std::pmr::polymorphic_allocator<std::byte> allocator{&memory};
    FramePacket& packet = *allocator.new_object<FramePacket>(memory);

    // Everything is relative to the camera: the camera sits at the origin, and only rotates.
    const float aspect = static_cast<float>(view.width) / static_cast<float>(std::max(view.height, 1U));
    const DirectX::XMMATRIX viewMatrix = Math::cameraRelativeViewMatrix(view.transform.rotation);
    const DirectX::XMMATRIX projection = Math::projectionMatrix(view.lens, aspect);
    DirectX::XMStoreFloat4x4(&packet.frame.view, viewMatrix);
    DirectX::XMStoreFloat4x4(&packet.frame.projection, projection);
    DirectX::XMStoreFloat4x4(&packet.frame.viewProjection, DirectX::XMMatrixMultiply(viewMatrix, projection));
    const Math::WorldPosition& eye = view.transform.position;
    packet.frame.cameraPosition = Math::Vec4{eye.x, eye.y, eye.z, 0.0F};
    packet.frame.viewport = Math::Vec4{static_cast<float>(view.width), static_cast<float>(view.height),
                                       1.0F / static_cast<float>(view.width), 1.0F / static_cast<float>(view.height)};

    // Storages rather than a view: a view doesn't build with Scene's allocator (see TransformHierarchy.cpp).
    auto& renderables = scene.registry().storage<Renderable>();
    auto& worlds = scene.registry().storage<Scene::WorldTransform>();
    packet.instances.reserve(renderables.size());
    for (const auto [entity, renderable] : renderables.each())
    {
        if (!worlds.contains(entity))
        {
            continue;
        }
        const Math::Mat4& world = worlds.get(entity).matrix;
        Shader::InstanceData instance{};
        instance.world = world;
        // Row vectors: the translation is the last row.
        instance.world.m[3][0] = world.m[3][0] - eye.x;
        instance.world.m[3][1] = world.m[3][1] - eye.y;
        instance.world.m[3][2] = world.m[3][2] - eye.z;
        instance.material = renderable.material;
        instance.mesh = renderable.mesh;
        packet.instances.push_back(instance);
    }
    return packet;
}
} // namespace Meow2::Renderer
