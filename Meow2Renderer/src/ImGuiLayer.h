/// @file
/// @brief ImGuiLayer: Dear ImGui in Meow2Renderer, fed by engine input events and drawn through the bindless RHI.

#pragma once

#include "UiTheme.h"

#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Platform/Input/InputTypes.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct ImGuiContext;
struct ImTextureData;

namespace Meow2::RendererApp
{
/// @brief The ImGui key for an engine key, for ImGui's input. Keys ImGui has no name for map to ImGuiKey_None (0).
/// @param key The engine key (a USB HID position).
/// @return The ImGuiKey value, as an int so this header needs no ImGui types.
[[nodiscard]] int toImGuiKey(Input::Key key);

/// @brief What ImGuiLayer uses. Plain data; everything must outlive the layer.
struct ImGuiLayerDeps
{
    Renderer::RenderDevice& device;       ///< Creates its textures and buffers; it's a device user.
    Renderer::PipelineLibrary& pipelines; ///< Builds its pipeline from shaders/ImGui.hlsl.
    Events::EventBus& events;             ///< Sends the input events it listens to.
    Log::Logger& log;                     ///< Receives errors.
};

/// @brief Dear ImGui for Meow2Renderer: one context, its input from the engine's events, its draw lists drawn as one
///        render-graph pass on top of the frame.
///
/// - **Input:** key, mouse, wheel and text events from the `EventBus` go to ImGui, so the app needs no SDL code.
/// - **Drawing:** every ImGui command becomes a `UiDraw` record in a buffer; `shaders/ImGui.hlsl` pulls vertices and
///   indices from buffers by heap index. One draw per command, with its scissor.
/// - **Textures:** ImGui 1.92+ asks the renderer to create, update and destroy textures (its font atlas grows as it
///   meets new glyphs). An ImGui texture ID is a slot in this class, never a GPU handle, so a backend switch only marks
///   textures for re-creation.
/// @par Thread safety
/// Not thread-safe. Use on the main thread; the pass it adds records on a job but reads only what it captured.
/// @par Lifetime
/// ImGui keeps its context in a process-wide pointer (`GImGui`): only one layer may exist at a time (asserted).
class ImGuiLayer
{
public:
    /// @brief Creates the context and the pipeline.
    /// @param deps        What it uses.
    /// @param shader      The AssetId of `shaders/ImGui.hlsl`.
    /// @param colorFormat The format of the swapchain it draws to.
    /// @param fonts       The theme's font files; a missing one falls back to ImGui's built-in font.
    /// @return The layer, or the error from building the pipeline.
    [[nodiscard]] static Result<std::unique_ptr<ImGuiLayer>> create(const ImGuiLayerDeps& deps, AssetId shader,
                                                                    Rhi::RhiFormat colorFormat, UiFontFiles fonts = {});

    /// @brief Use create().
    /// @param deps What it uses.
    explicit ImGuiLayer(const ImGuiLayerDeps& deps);

    /// @brief Destroys its GPU objects and the ImGui context.
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;            ///< Not copyable: owns the ImGui context.
    ImGuiLayer& operator=(const ImGuiLayer&) = delete; ///< Not copyable: owns the ImGui context.

    /// @brief Starts an ImGui frame; call ImGui functions after it.
    /// @param deltaSeconds     Time since the last frame.
    /// @param width            Window width in window coordinates: the units of mouse positions.
    /// @param height           Window height in window coordinates.
    /// @param framebufferScale Pixels per window coordinate: 1 on Windows, 2 on a Retina Mac.
    /// @param contentScale     How much larger UI should be drawn, in window coordinates: the display's scale (1.25 at
    ///                          125 %) over `framebufferScale`. Text and widgets are scaled by it.
    void newFrame(float deltaSeconds, float width, float height, float framebufferScale, float contentScale);

    /// @brief Ends the ImGui frame and adds its pass (and any texture uploads) to the graph, drawing on top of the
    ///        swapchain's contents.
    /// @param graph     The frame's graph, after the passes that draw the scene.
    /// @param frameSlot IRhiDevice::frameSlot().
    /// @param swapchain The acquired swapchain.
    void addPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain& swapchain);

    /// @brief Ends an ImGui frame that won't be drawn (minimised window, no swapchain image).
    void skipFrame();

    /// @brief Releases staging buffers whose copies are recorded. Call after the graph has executed.
    void endFrame();

    /// @brief The theme's fonts in ImGui's atlas, for pushMonoFont().
    /// @return The fonts; a member is null when its file wasn't found.
    [[nodiscard]] const UiFonts& fonts() const
    {
        return m_fonts;
    }

    /// @brief Whether ImGui is using the mouse (over a window), so the app should ignore it.
    /// @return ImGui's WantCaptureMouse.
    [[nodiscard]] bool wantsMouse() const;

    /// @brief Whether ImGui is using the keyboard (a text box has focus), so the app should ignore it.
    /// @return ImGui's WantCaptureKeyboard.
    [[nodiscard]] bool wantsKeyboard() const;

private:
    static constexpr float c_textFontSize = 16.0F; ///< Barlow's size at scale 1, in pixels.
    static constexpr float c_monoFontSize = 14.0F; ///< Consolas' size at scale 1: it runs larger than Barlow.

    /// @brief Adds the theme's fonts to ImGui's atlas; the first becomes the default.
    /// @param files The TrueType files, kept by the layer.
    void addFonts(UiFontFiles files);

    /// @brief A texture ImGui asked for; its slot index + 1 is the ImGui texture ID.
    struct TextureSlot
    {
        Rhi::TextureHandle texture{}; ///< On the current device; unset while the slot is free.
        bool used = false;            ///< Whether the slot holds a texture.
        bool ready = false;           ///< Whether a copy has filled it (it's in ShaderRead).
    };

    /// @brief One texture copy waiting for the next upload pass.
    struct PendingUpload
    {
        Rhi::BufferHandle staging{};  ///< The pixels, RGBA8.
        Rhi::TextureHandle texture{}; ///< Where they go.
        bool firstFill = true;        ///< The texture has no contents yet (else it's in ShaderRead).
    };

    /// @brief The buffers one frame slot draws from; they grow as needed.
    struct FrameBuffers
    {
        Rhi::BufferHandle vertices{};  ///< ImDrawVert array.
        Rhi::BufferHandle indices{};   ///< 32-bit indices.
        Rhi::BufferHandle draws{};     ///< UiDraw records.
        std::uint64_t vertexBytes = 0; ///< Capacity of `vertices`.
        std::uint64_t indexBytes = 0;  ///< Capacity of `indices`.
        std::uint64_t drawBytes = 0;   ///< Capacity of `draws`.
    };

    /// @brief Honours ImGui's texture requests: create, update, destroy.
    /// @param device The device.
    void updateTextures(Rhi::IRhiDevice& device);

    /// @brief Queues a copy of a texture's pixels (converted to RGBA8).
    /// @param device  The device.
    /// @param texture ImGui's texture.
    /// @param slot    Its slot.
    /// @return False if the staging buffer can't be created.
    bool queueUpload(Rhi::IRhiDevice& device, ImTextureData& texture, TextureSlot& slot);

    /// @brief Makes a buffer at least `bytes` big, replacing a smaller one (the old one is destroyed deferred).
    /// @param device  The device.
    /// @param buffer  The buffer.
    /// @param capacity Its current size.
    /// @param bytes   The size needed.
    /// @param name    Shown in debugging tools.
    /// @return False if the device refused.
    bool ensureBuffer(Rhi::IRhiDevice& device, Rhi::BufferHandle& buffer, std::uint64_t& capacity, std::uint64_t bytes,
                      const char* name);

    /// @brief Destroys every GPU object; ImGui re-creates its textures on later frames.
    /// @param device The device they were made on.
    void releaseGpu(Rhi::IRhiDevice& device);

    ImGuiLayerDeps m_deps;
    ImGuiContext* m_context = nullptr;
    float m_contentScale = 1.0F; ///< The scale the style was last sized for.
    UiFontFiles m_fontFiles;     ///< Read by the atlas whenever it bakes glyphs.
    UiFonts m_fonts;
    std::optional<Renderer::PipelineId> m_pipeline;
    std::vector<TextureSlot> m_textures;
    std::vector<PendingUpload> m_uploads;
    std::vector<Rhi::BufferHandle> m_spentStaging;
    std::array<FrameBuffers, Rhi::c_maxFramesInFlight> m_frames{};
    std::vector<Events::Subscription> m_input;
    Renderer::DeviceUser m_deviceUser; ///< Last: stops its callbacks before the members they use go away.
};
} // namespace Meow2::RendererApp
