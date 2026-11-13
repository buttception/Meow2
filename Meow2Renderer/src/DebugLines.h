/// @file
/// @brief Debug lines in world space (Meow2Renderer only): the frozen camera's frustum, and later other helpers.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Math/Types.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief One line in world space. Plain data.
struct DebugLine
{
    Math::Vec3 from{};   ///< One end.
    Math::Vec3 to{};     ///< The other.
    Math::Color color{}; ///< Linear colour, the same along it.
};

/// @brief The edges of a camera's view volume, out to a distance: the near rectangle, the far one at `distance`, and
///        the four edges from the camera through the corners to the far one.
///
/// The engine's projection has no far plane (reversed infinite Z), so the far end is chosen: a point at distance d
/// has depth near / d.
/// @param frame    The camera's frame constants: viewProjection, projection (its near plane) and cameraPosition.
/// @param distance How far to draw the sides; > the near plane.
/// @param color    The lines' colour.
/// @return Twelve lines.
[[nodiscard]] std::array<DebugLine, 12> frustumLines(const Shader::FrameConstants& frame, float distance,
                                                     const Math::Color& color);

/// @brief Draws lines over the scene each frame: a line-list pipeline (`shaders/DebugLines.hlsl`), the lines in an
///        upload buffer per frame slot.
///
/// Lines are added between frames and drawn by addPass(), always on top (no depth test), so they show through
/// geometry; clear() starts the next frame's list.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The RenderDevice, the PipelineLibrary and the Logger must outlive it.
class DebugLines
{
public:
    /// @brief The most lines a frame can draw.
    static constexpr std::uint32_t c_maxLines = 4096;

    /// @brief Builds the pipeline. The buffers are made when first needed.
    /// @param device      The device.
    /// @param pipelines   Builds the pipeline.
    /// @param shader      `shaders/DebugLines.hlsl`.
    /// @param colorFormat The target's format (the scene's HDR colour).
    /// @param log         Receives a pipeline that couldn't be built.
    DebugLines(Renderer::RenderDevice& device, Renderer::PipelineLibrary& pipelines, AssetId shader,
               Rhi::RhiFormat colorFormat, Log::Logger& log);

    /// @brief Releases the buffers.
    ~DebugLines();

    DebugLines(const DebugLines&) = delete;            ///< Not copyable: owns buffers.
    DebugLines& operator=(const DebugLines&) = delete; ///< Not copyable: owns buffers.

    /// @brief Forgets the lines added so far.
    void clear()
    {
        m_lines.clear();
    }

    /// @brief Adds a line for the next addPass(). Lines past c_maxLines are dropped.
    /// @param line The line.
    void add(const DebugLine& line)
    {
        if (m_lines.size() < c_maxLines)
        {
            m_lines.push_back(line);
        }
    }

    /// @brief How many lines are waiting.
    /// @return The count.
    [[nodiscard]] std::size_t size() const
    {
        return m_lines.size();
    }

    /// @brief Adds the pass that draws the lines into a colour target, if there are any.
    /// @param graph          The frame's graph.
    /// @param frameSlot      IRhiDevice::frameSlot().
    /// @param viewProjection The camera's view-projection matrix (FrameConstants::viewProjection).
    /// @param color          The target in the graph.
    /// @param colorTexture   The same texture.
    void addPass(Renderer::RenderGraph& graph, std::uint32_t frameSlot, const Math::Mat4& viewProjection,
                 Renderer::GraphResource color, Rhi::TextureHandle colorTexture);

private:
    /// @brief Destroys the buffers (a backend switch, or the end).
    /// @param device The device they were made on.
    void release(Rhi::IRhiDevice& device);

    Renderer::RenderDevice& m_device;
    Renderer::PipelineLibrary& m_pipelines;
    std::optional<Renderer::PipelineId> m_pipeline;
    std::vector<DebugLine> m_lines;
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_buffers{}; ///< Matrix and vertices, per frame slot.
    Renderer::DeviceUser m_deviceUser; ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::RendererApp
