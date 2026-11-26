/// @file
/// @brief FrameCapture: one frame kept for inspection (its render graph and every texture as each pass left it), and
///        a kept texture turned into a small image on the CPU. A debugging tool of Meow2Renderer and the editor,
///        never in games; RenderDoc is for keeping captures.

#pragma once

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/FrameFlow.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Meow2::Renderer
{
/// @brief How a texture is turned into a picture. Plain data.
struct TextureDisplaySettings
{
    std::uint32_t channels = 0b0111; ///< Bits 0..3: R, G, B, A shown; one bit alone shows that channel as grey.
    float rangeMin = 0.0F;           ///< The value shown black.
    float rangeMax = 1.0F;           ///< The value shown white.

    /// @brief Settings are equal when every field is.
    friend bool operator==(const TextureDisplaySettings&, const TextureDisplaySettings&) = default;
};

/// @brief How a texture starts out shown: depth spread over the near range (reversed Z puts most of a scene near 0),
///        colour as it is.
/// @param depth Whether it holds depth.
/// @return The settings.
[[nodiscard]] TextureDisplaySettings defaultDisplaySettings(bool depth);

/// @brief One texture of a capture, as a pass left it. Plain data.
struct CaptureSnapshot
{
    std::uint32_t pass = 0;        ///< The pass that wrote it (index into the capture's FrameFlow::passes).
    std::uint32_t resource = 0;    ///< Which resource (index into FrameFlow::resources).
    bool depth = false;            ///< Kept as R32F depth, else as RGBA16F.
    Rhi::TextureHandle texture{};  ///< The copy, the source's size.
    Rhi::TextureInfo info{};       ///< Its size and format.
};

/// @brief A snapshot as a picture on the CPU. Plain data.
struct CapturedImage
{
    std::uint32_t snapshot = 0;             ///< Which snapshot (index into FrameCapture::snapshots()).
    TextureDisplaySettings settings{};      ///< How it was shown.
    std::uint32_t width = 0;                ///< Pixels across.
    std::uint32_t height = 0;               ///< Pixels down.
    std::vector<std::byte> pixels{};        ///< RGBA8, sRGB-encoded (ready for a screen), top row first, rows packed.
};

/// @brief One frame kept for inspection: request(), and the next frame that goes through afterPasses() is captured.
///
/// Capturing copies every texture a pass writes, right after that pass (a debug pass inserted into the graph), into a
/// float texture of the same size: RGBA16F, or R32F for depth. The raw values are kept, so how they're shown can change
/// afterwards. The copies stay on the GPU; the captured frame's graph is kept as a FrameFlow.
///
/// requestImage() turns one snapshot into a picture for a UI that can't show GPU textures (Qt): a display pass into
/// an RGBA8 sRGB texture of at most `maxSide` pixels a side, then a copy into a readback buffer. The picture is taken
/// when that frame slot comes round again (beginFrame()), after its fence, so nothing waits on the GPU; takeImage()
/// hands it over a frame or two after the request.
///
/// Each frame, in order: beginFrame() (after IRhiDevice::beginFrame()), the frame's passes, afterPasses() (every pass
/// added, before compile), then endFrame() (after execution).
/// @par Thread safety
/// Not thread-safe; the main thread.
/// @par Lifetime
/// The RenderDevice, PipelineLibrary and Logger must outlive it. A backend switch drops the capture (the copies are
/// GPU objects of the old device and the frame is gone).
class FrameCapture
{
public:
    /// @brief Builds its pipelines and registers with the device.
    /// @param device    The render device.
    /// @param pipelines Builds the pipelines.
    /// @param shader    The AssetId of `shaders/DebugTexture.hlsl`.
    /// @param log       Receives errors and a line per capture.
    FrameCapture(RenderDevice& device, PipelineLibrary& pipelines, AssetId shader, Log::Logger& log);

    /// @brief Frees its textures and buffers.
    ~FrameCapture();

    FrameCapture(const FrameCapture&) = delete;            ///< Not copyable: owns GPU objects.
    FrameCapture& operator=(const FrameCapture&) = delete; ///< Not copyable: owns GPU objects.

    /// @brief Captures the next frame that goes through afterPasses().
    void request()
    {
        m_requested = true;
    }

    /// @brief Whether a capture is asked for and not made yet.
    /// @return True until the next afterPasses().
    [[nodiscard]] bool requested() const
    {
        return m_requested;
    }

    /// @brief Drops the capture and frees its copies.
    void clear();

    /// @brief The captured frame's graph.
    /// @return It, or nothing before the first capture.
    [[nodiscard]] const std::optional<FrameFlow>& flow() const
    {
        return m_flow;
    }

    /// @brief Which frame was captured: a count of the frames that went through beginFrame().
    /// @return The number.
    [[nodiscard]] std::uint64_t frame() const
    {
        return m_capturedFrame;
    }

    /// @brief The captured textures, in pass order.
    /// @return Them.
    [[nodiscard]] std::span<const CaptureSnapshot> snapshots() const
    {
        return m_snapshots;
    }

    /// @brief The snapshot a pass left of a resource.
    /// @param pass     The pass that wrote it.
    /// @param resource The resource.
    /// @return Its index in snapshots(), or nothing (a buffer, or a pass that didn't write it).
    [[nodiscard]] std::optional<std::uint32_t> snapshotFor(std::uint32_t pass, std::uint32_t resource) const;

    /// @brief Step 1, after IRhiDevice::beginFrame(): this slot's last frame has finished, so a picture read back
    ///        then is taken now.
    /// @param frameSlot IRhiDevice::frameSlot().
    void beginFrame(std::uint32_t frameSlot);

    /// @brief Imports a snapshot into this frame's graph, for a pass that reads it (Meow2Renderer's capture viewer).
    /// @param graph The frame's graph.
    /// @param index Index into snapshots().
    /// @return Its graph resource, or nothing if there's no such snapshot or it was taken this frame.
    std::optional<GraphResource> importSnapshot(RenderGraph& graph, std::uint32_t index);

    /// @brief Asks for a snapshot as a picture (takeImage()). A newer request replaces one not drawn yet.
    /// @param index    Index into snapshots().
    /// @param settings How to show it.
    /// @param maxSide  The picture's longest side at most, in pixels (smaller textures keep their size).
    void requestImage(std::uint32_t index, const TextureDisplaySettings& settings, std::uint32_t maxSide = 512);

    /// @brief Whether a picture is asked for or on its way back.
    /// @return True until takeImage() can return it.
    [[nodiscard]] bool imagePending() const;

    /// @brief The latest picture that came back, once.
    /// @return It, or nothing if none came back since the last call.
    [[nodiscard]] std::optional<CapturedImage> takeImage();

    /// @brief Step 2, every pass added and before compile: captures the frame when asked (a copy after every pass
    ///        that writes a texture), and adds the asked-for picture's passes.
    /// @param graph     The frame's graph.
    /// @param frameSlot IRhiDevice::frameSlot().
    void afterPasses(RenderGraph& graph, std::uint32_t frameSlot);

    /// @brief Step 3, after execution: keeps its textures' states for the next frame.
    /// @param graph The executed graph.
    void endFrame(const RenderGraph& graph);

private:
    /// @brief A snapshot's state between frames, and its resource in this frame's graph.
    struct SnapshotState
    {
        Rhi::ResourceState state = Rhi::ResourceState::Undefined; ///< After the last frame that touched it.
        std::optional<GraphResource> resource;                     ///< In this frame's graph, if imported.
        bool ready = false;                                        ///< Written by a finished frame's graph.
    };

    /// @brief A picture asked for. Plain data.
    struct ImageRequest
    {
        std::uint32_t snapshot = 0;
        TextureDisplaySettings settings{};
        std::uint32_t maxSide = 512;
    };

    /// @brief A picture drawn into a slot's readback buffer, not read yet. Plain data.
    struct ImageInFlight
    {
        ImageRequest request{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /// @brief Captures this frame: the flow, and a copy after every pass that writes a texture.
    void capture(RenderGraph& graph);

    /// @brief Adds the picture's display and copy passes for this slot.
    void addImagePasses(RenderGraph& graph, std::uint32_t frameSlot, const ImageRequest& request);

    /// @brief Frees every GPU object (backend switch, destruction).
    void release(Rhi::IRhiDevice& device);

    RenderDevice& m_device;
    PipelineLibrary& m_pipelines;
    Log::Logger& m_log;
    std::optional<PipelineId> m_copyColor;    ///< psCopy into RGBA16F.
    std::optional<PipelineId> m_copyDepth;    ///< psCopy into R32F.
    std::optional<PipelineId> m_display;      ///< psDisplay into RGBA8 sRGB.
    bool m_requested = false;                 ///< request() since the last capture.
    std::optional<FrameFlow> m_flow;          ///< The captured frame's graph.
    std::vector<CaptureSnapshot> m_snapshots; ///< Its textures, in pass order.
    std::vector<SnapshotState> m_states;      ///< Per snapshot.
    std::uint64_t m_frame = 0;                ///< Frames seen.
    std::uint64_t m_capturedFrame = 0;        ///< m_frame when captured.
    std::vector<Rhi::TextureHandle> m_retired; ///< Freed after this frame (a graph may still name them).

    std::optional<ImageRequest> m_imageRequest; ///< Asked for, not drawn yet.
    std::array<std::optional<ImageInFlight>, Rhi::c_maxFramesInFlight> m_imageInFlight{}; ///< Per frame slot.
    std::optional<CapturedImage> m_image;                                                  ///< Came back.
    Rhi::TextureHandle m_imageTarget{};         ///< The picture on the GPU (RGBA8 sRGB).
    Rhi::TextureInfo m_imageTargetInfo{};       ///< Its size.
    Rhi::ResourceState m_imageTargetState = Rhi::ResourceState::Undefined; ///< After the last frame.
    std::optional<GraphResource> m_imageTargetResource; ///< In this frame's graph.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_readback{};      ///< Per slot.
    std::array<std::uint64_t, Rhi::c_maxFramesInFlight> m_readbackBytes{};     ///< Their sizes.
    std::array<Rhi::BufferHandle, Rhi::c_maxFramesInFlight> m_displayRecords{}; ///< One DebugDisplay per slot.

    DeviceUser m_deviceUser; ///< Last: unregisters first, before what its callbacks use.
};
} // namespace Meow2::Renderer
