/// @file
/// @brief Debug lines: the frustum's edges, the line buffer and the pass that draws them.

#include "DebugLines.h"

#include <DirectXMath.h>

#include <cstring>

namespace Meow2::RendererApp
{
namespace
{
/// @brief Bytes before the vertices in a line buffer: the view-projection matrix.
constexpr std::size_t c_headerBytes = 64;

/// @brief One vertex as the shader reads it: position (w unused), colour. Plain data.
struct LineVertex
{
    Math::Vec4 position;
    Math::Vec4 color;
};
static_assert(sizeof(LineVertex) == 32, "DebugLines.hlsl reads 32 bytes per vertex");

/// @brief A point of clip space back in the world.
Math::Vec3 unproject(DirectX::FXMMATRIX inverse, float x, float y, float depth)
{
    using namespace DirectX;
    const XMVECTOR point = XMVector4Transform(XMVectorSet(x, y, depth, 1.0F), inverse);
    Math::Vec3 out;
    XMStoreFloat3(&out, XMVectorScale(point, 1.0F / XMVectorGetW(point)));
    return out;
}
} // namespace

std::array<DebugLine, 12> frustumLines(const Shader::FrameConstants& frame, float distance, const Math::Color& color)
{
    using namespace DirectX;
    const XMMATRIX inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&frame.viewProjection));
    // Reversed infinite Z: depth = near / distance, with the near plane in the projection's [3][2].
    const float nearZ = frame.projection._43;
    const float farDepth = nearZ / distance;
    constexpr std::array<std::array<float, 2>, 4> c_corners{
        {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}};
    std::array<Math::Vec3, 4> nearCorners;
    std::array<Math::Vec3, 4> farCorners;
    for (std::size_t i = 0; i < 4; ++i)
    {
        nearCorners[i] = unproject(inverse, c_corners[i][0], c_corners[i][1], 1.0F);
        farCorners[i] = unproject(inverse, c_corners[i][0], c_corners[i][1], farDepth);
    }
    const Math::Vec3 eye{frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z};
    std::array<DebugLine, 12> lines;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const std::size_t next = (i + 1) % 4;
        lines[i] = {.from = nearCorners[i], .to = nearCorners[next], .color = color};
        lines[4 + i] = {.from = farCorners[i], .to = farCorners[next], .color = color};
        lines[8 + i] = {.from = eye, .to = farCorners[i], .color = color};
    }
    return lines;
}

DebugLines::DebugLines(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                       Rhi::RhiFormat colorFormat, Log::Logger& log)
    : m_device(device)
    , m_pipelines(pipelines)
{
    // On top of everything: no depth target, so the lines show through the scene.
    Result<Renderer::PipelineId> pipeline =
        pipelines.add(Renderer::GraphicsPipelineRequest{.shader = shader,
                                                        .colorFormat = colorFormat,
                                                        .depthFormat = Rhi::RhiFormat::Unknown,
                                                        .depthWrite = false,
                                                        .topology = Rhi::PrimitiveTopology::Lines,
                                                        .name = "Debug lines"});
    if (pipeline)
    {
        m_pipeline = *pipeline;
    }
    else
    {
        MEOW2_LOG_ERROR(log, "Debug line pipeline not built: {}", pipeline.error());
    }
    // The buffers are made on demand, so a new device has nothing to restore.
    m_deviceUser = device.addUser(
        "DebugLines", [this](Rhi::IRhiDevice& old) { release(old); },
        [](Rhi::IRhiDevice&) -> Result<void> { return {}; });
}

DebugLines::~DebugLines()
{
    if (m_device.hasDevice())
    {
        release(m_device.device());
    }
}

void DebugLines::release(Rhi::IRhiDevice& device)
{
    for (Rhi::BufferHandle& buffer : m_buffers)
    {
        device.destroy(buffer); // deferred until the GPU is done with it
        buffer = {};
    }
}

void DebugLines::addPass(Renderer::RenderGraph& graph, std::uint32_t frameSlot, const Math::Mat4& viewProjection,
                         Renderer::GraphResource color, Rhi::TextureHandle colorTexture)
{
    if (m_lines.empty() || !m_pipeline)
    {
        return;
    }
    Rhi::IRhiDevice& device = m_device.device();
    Rhi::BufferHandle& buffer = m_buffers[frameSlot];
    if (!buffer.isSet())
    {
        Result<Rhi::BufferHandle> made =
            device.createBuffer({.size = c_headerBytes + std::uint64_t{c_maxLines} * 2 * sizeof(LineVertex),
                                 .memory = Rhi::RhiMemory::Upload,
                                 .tag = Memory::MemTag::Renderer,
                                 .name = "Debug lines"});
        if (!made)
        {
            return;
        }
        buffer = *made;
    }
    // This slot's last frame is done (beginFrame() waited for it), so its buffer can be rewritten.
    std::byte* memory = device.mappedMemory(buffer).data();
    std::memcpy(memory, &viewProjection, c_headerBytes);
    auto* vertices = reinterpret_cast<LineVertex*>(memory + c_headerBytes);
    for (const DebugLine& line : m_lines)
    {
        const Math::Vec4 colour{line.color.r, line.color.g, line.color.b, line.color.a};
        *vertices++ = {.position = {line.from.x, line.from.y, line.from.z, 1.0F}, .color = colour};
        *vertices++ = {.position = {line.to.x, line.to.y, line.to.z, 1.0F}, .color = colour};
    }
    Shader::PushConstants push{};
    push.userData = device.bindlessIndex(buffer);
    const auto vertexCount = static_cast<std::uint32_t>(m_lines.size() * 2);
    graph.addPass({.name = "Debug lines",
                   .accesses = {{color, Rhi::ResourceState::ColorTarget}},
                   .record =
                       [this, push, vertexCount, colorTexture](Rhi::IRhiCommandList& list)
                   {
                       list.beginRendering(Rhi::RenderTargets{.color = colorTexture});
                       list.setPipeline(m_pipelines.get(*m_pipeline));
                       list.setPushConstants(push);
                       list.draw(vertexCount);
                       list.endRendering();
                   },
                   .small = true,
                   .debug = true});
}
} // namespace Meow2::RendererApp
