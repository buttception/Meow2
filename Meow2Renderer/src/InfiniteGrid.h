/// @file
/// @brief Meow2Renderer's infinite grid on the ground plane (y = 0), worked out by a compute shader. Renderer only,
///        never in games.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>

#include <array>
#include <cstdint>
#include <optional>

namespace Meow2::RendererApp
{
/// @brief The grid's per-frame constants, as `shaders/DebugGrid.hlsl` reads them. Plain data; 176 bytes.
struct GridConstants
{
    Math::Mat4 inverseViewProjection{}; ///< Clip space → world.
    Math::Mat4 viewProjection{};        ///< World → clip space.
    Math::Vec4 camera{};                ///< xyz: the camera's position in the world.
    std::uint32_t depthTexture = 0;     ///< Heap index of the scene's depth.
    std::uint32_t gridTexture = 0;      ///< Writable heap index of the grid layer.
    std::uint32_t width = 0;            ///< The layer's width in pixels.
    std::uint32_t height = 0;           ///< Its height.
    std::uint32_t gridRead = 0;         ///< Heap index of the grid layer, to read.
    std::uint32_t padding0 = 0;         ///< Unused.
    std::uint32_t padding1 = 0;         ///< Unused.
    std::uint32_t padding2 = 0;         ///< Unused.
};
static_assert(sizeof(GridConstants) == 176, "shaders/DebugGrid.hlsl reads 176 bytes");

/// @brief Draws an endless grid on the plane y = 0 over the scene, before tone mapping: the X axis red, the Z axis
/// blue,
///        cells in powers of ten that follow the camera's height, hidden behind geometry.
///
/// Two passes: "Grid" (compute, `csGrid`) writes a straight-alpha RGBA16F layer the size of the scene, testing each
/// pixel's ray hit against the scene's depth; "Grid composite" (`psComposite`) blends that layer over the scene's
/// colour. The layer is the size of the scene's targets and is remade when they change.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The RenderDevice, the PipelineLibrary and the Logger must outlive it.
class InfiniteGrid
{
public:
    /// @brief Builds the pipelines; the layer and buffers are made when first needed.
    /// @param device      The device.
    /// @param pipelines   Builds the pipelines.
    /// @param shader      `shaders/DebugGrid.hlsl`.
    /// @param colorFormat The scene colour's format.
    /// @param log         Receives a pipeline that couldn't be built.
    InfiniteGrid(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
                 Rhi::RhiFormat colorFormat, Log::Logger& log);

    /// @brief Releases the layer and the buffers.
    ~InfiniteGrid();

    InfiniteGrid(const InfiniteGrid&) = delete;            ///< Not copyable: owns GPU resources.
    InfiniteGrid& operator=(const InfiniteGrid&) = delete; ///< Not copyable: owns GPU resources.

    /// @brief Whether both pipelines were built.
    /// @return True if addPasses() can draw.
    [[nodiscard]] bool ready() const
    {
        return m_compute.has_value() && m_composite.has_value();
    }

    /// @brief Adds the grid's two passes: after the scene has drawn its depth and colour, before the tone curve.
    /// @param graph        The frame's graph.
    /// @param frameSlot    IRhiDevice::frameSlot().
    /// @param frame        The camera: viewProjection and cameraPosition.
    /// @param color        The scene's colour in the graph.
    /// @param colorTexture The same texture.
    /// @param depth        The scene's depth in the graph (GpuScene::depthResource()).
    /// @param depthTexture The same texture.
    /// @param width        The targets' width in pixels.
    /// @param height       Their height.
    void addPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot, const Shader::FrameConstants& frame,
                   Renderer::GraphResource color, Rhi::TextureHandle colorTexture, Renderer::GraphResource depth,
                   Rhi::TextureHandle depthTexture, std::uint32_t width, std::uint32_t height);

    /// @brief The constants for a camera, as addPasses() writes them (heap indices left 0). For tests.
    /// @param frame  The camera.
    /// @param width  The layer's width.
    /// @param height Its height.
    /// @return The constants.
    [[nodiscard]] static GridConstants constantsFor(const Shader::FrameConstants& frame, std::uint32_t width,
                                                    std::uint32_t height);

private:
    /// @brief Destroys the layer and the buffers (a backend switch, or the end).
    /// @param device The device they were made on.
    void release(Rhi::IRhiDevice& device);

    Renderer::RenderDevice& m_device;
    Renderer::PipelineLibrary& m_pipelines;
    Log::Logger& m_log;
    std::optional<Renderer::PipelineId> m_compute;
    std::optional<Renderer::PipelineId> m_composite;
    Rhi::TextureHandle m_layer;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    /// The layer's state from the last frame: Undefined when new, then ShaderRead (the composite's), so the next
    /// frame's write waits for the last frame's read.
    Rhi::ResourceState m_layerState = Rhi::ResourceState::Undefined;
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_constants{}; ///< GridConstants, per frame slot.
    Renderer::DeviceUser m_deviceUser; ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::RendererApp
