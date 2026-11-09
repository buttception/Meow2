/// @file
/// @brief ImGuiLayer: ImGui's input, textures and draw lists on the engine.

#include "ImGuiLayer.h"

#include "UiTheme.h"

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Platform/Input/InputEvents.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace Meow2::RendererApp
{
namespace
{
static_assert(sizeof(ImDrawVert) == 20, "shaders/ImGui.hlsl reads 20-byte vertices: position, UV, colour");

// ImGui's allocator hooks get a user-data pointer but no size on free, like the other C library hooks: they reach the
// process-wide tracker (techstack.md §6, §18), or malloc when tracking is off.
void* imguiAllocate(std::size_t size, void* /*user*/)
{
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        return Memory::globalTracker().allocateTagged(size, alignof(std::max_align_t), Memory::MemTag::ImGui);
    }
    else
    {
        return std::malloc(size); // NOLINT(cppcoreguidelines-no-malloc): ImGui's own contract
    }
}

void imguiFree(void* memory, void* /*user*/)
{
    if (memory == nullptr)
    {
        return;
    }
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        Memory::globalTracker().deallocateUnsized(memory);
    }
    else
    {
        std::free(memory); // NOLINT(cppcoreguidelines-no-malloc): ImGui's own contract
    }
}

/// @brief One recorded ImGui command.
struct UiCommand
{
    Rhi::ScissorRect scissor{};   ///< In pixels.
    std::uint32_t record = 0;     ///< Index of its UiDraw.
    std::uint32_t firstIndex = 0; ///< Its first index, passed in PushConstants::userData.
    std::uint32_t indexCount = 0; ///< Indices.
};

/// @brief Updates ImGui's modifier keys after one of them changes.
void updateModifier(ImGuiIO& io, Input::Key key, bool down)
{
    switch (key)
    {
        case Input::Key::LeftCtrl:
        case Input::Key::RightCtrl:
            io.AddKeyEvent(ImGuiMod_Ctrl, down);
            break;
        case Input::Key::LeftShift:
        case Input::Key::RightShift:
            io.AddKeyEvent(ImGuiMod_Shift, down);
            break;
        case Input::Key::LeftAlt:
        case Input::Key::RightAlt:
            io.AddKeyEvent(ImGuiMod_Alt, down);
            break;
        case Input::Key::LeftGui:
        case Input::Key::RightGui:
            io.AddKeyEvent(ImGuiMod_Super, down);
            break;
        default:
            break;
    }
}
} // namespace

int toImGuiKey(Input::Key key)
{
    using Input::Key;
    const auto value = static_cast<int>(key);
    // Letters, digits and F1–F12 are contiguous in both enums.
    if (key >= Key::A && key <= Key::Z)
    {
        return ImGuiKey_A + (value - static_cast<int>(Key::A));
    }
    if (key >= Key::Num1 && key <= Key::Num9)
    {
        return ImGuiKey_1 + (value - static_cast<int>(Key::Num1));
    }
    if (key >= Key::F1 && key <= Key::F12)
    {
        return ImGuiKey_F1 + (value - static_cast<int>(Key::F1));
    }
    if (key >= Key::Keypad1 && key <= Key::Keypad9)
    {
        return ImGuiKey_Keypad1 + (value - static_cast<int>(Key::Keypad1));
    }
    switch (key)
    {
        case Key::Num0:
            return ImGuiKey_0;
        case Key::Keypad0:
            return ImGuiKey_Keypad0;
        case Key::Return:
            return ImGuiKey_Enter;
        case Key::Escape:
            return ImGuiKey_Escape;
        case Key::Backspace:
            return ImGuiKey_Backspace;
        case Key::Tab:
            return ImGuiKey_Tab;
        case Key::Space:
            return ImGuiKey_Space;
        case Key::Minus:
            return ImGuiKey_Minus;
        case Key::Equals:
            return ImGuiKey_Equal;
        case Key::LeftBracket:
            return ImGuiKey_LeftBracket;
        case Key::RightBracket:
            return ImGuiKey_RightBracket;
        case Key::Backslash:
            return ImGuiKey_Backslash;
        case Key::Semicolon:
            return ImGuiKey_Semicolon;
        case Key::Apostrophe:
            return ImGuiKey_Apostrophe;
        case Key::Grave:
            return ImGuiKey_GraveAccent;
        case Key::Comma:
            return ImGuiKey_Comma;
        case Key::Period:
            return ImGuiKey_Period;
        case Key::Slash:
            return ImGuiKey_Slash;
        case Key::CapsLock:
            return ImGuiKey_CapsLock;
        case Key::PrintScreen:
            return ImGuiKey_PrintScreen;
        case Key::ScrollLock:
            return ImGuiKey_ScrollLock;
        case Key::Pause:
            return ImGuiKey_Pause;
        case Key::Insert:
            return ImGuiKey_Insert;
        case Key::Home:
            return ImGuiKey_Home;
        case Key::PageUp:
            return ImGuiKey_PageUp;
        case Key::Delete:
            return ImGuiKey_Delete;
        case Key::End:
            return ImGuiKey_End;
        case Key::PageDown:
            return ImGuiKey_PageDown;
        case Key::Right:
            return ImGuiKey_RightArrow;
        case Key::Left:
            return ImGuiKey_LeftArrow;
        case Key::Down:
            return ImGuiKey_DownArrow;
        case Key::Up:
            return ImGuiKey_UpArrow;
        case Key::NumLock:
            return ImGuiKey_NumLock;
        case Key::KeypadDivide:
            return ImGuiKey_KeypadDivide;
        case Key::KeypadMultiply:
            return ImGuiKey_KeypadMultiply;
        case Key::KeypadMinus:
            return ImGuiKey_KeypadSubtract;
        case Key::KeypadPlus:
            return ImGuiKey_KeypadAdd;
        case Key::KeypadEnter:
            return ImGuiKey_KeypadEnter;
        case Key::KeypadPeriod:
            return ImGuiKey_KeypadDecimal;
        case Key::Application:
            return ImGuiKey_Menu;
        case Key::LeftCtrl:
            return ImGuiKey_LeftCtrl;
        case Key::LeftShift:
            return ImGuiKey_LeftShift;
        case Key::LeftAlt:
            return ImGuiKey_LeftAlt;
        case Key::LeftGui:
            return ImGuiKey_LeftSuper;
        case Key::RightCtrl:
            return ImGuiKey_RightCtrl;
        case Key::RightShift:
            return ImGuiKey_RightShift;
        case Key::RightAlt:
            return ImGuiKey_RightAlt;
        case Key::RightGui:
            return ImGuiKey_RightSuper;
        default:
            return ImGuiKey_None;
    }
}

Result<std::unique_ptr<ImGuiLayer>> ImGuiLayer::create(const ImGuiLayerDeps& deps, AssetId shader,
                                                       Rhi::RhiFormat colorFormat, UiFontFiles fonts)
{
    auto layer = std::make_unique<ImGuiLayer>(deps);
    layer->addFonts(std::move(fonts));
    Result<Renderer::PipelineId> pipeline = deps.pipelines.add(Renderer::GraphicsPipelineRequest{
        .shader = shader, .colorFormat = colorFormat, .blend = Rhi::BlendMode::Alpha, .name = "ImGui"});
    if (!pipeline)
    {
        return Unexpected<Error>{pipeline.error()};
    }
    layer->m_pipeline = *pipeline;
    return layer;
}

ImGuiLayer::ImGuiLayer(const ImGuiLayerDeps& deps)
    : m_deps(deps)
{
    MEOW2_ASSERT(ImGui::GetCurrentContext() == nullptr, "Only one ImGuiLayer at a time: ImGui's context is global");
    // GLOBAL: ImGui keeps its context and allocator in process-wide pointers; this object owns both while it lives.
    ImGui::SetAllocatorFunctions(imguiAllocate, imguiFree, nullptr);
    {
        const Memory::MemTagScope tag{Memory::MemTag::ImGui};
        m_context = ImGui::CreateContext();
    }
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.BackendRendererName = "Meow2 RHI";
    io.BackendPlatformName = "Meow2 input";
    io.IniFilename = nullptr; // no imgui.ini: the layout goes into the TOML config (loadLayout, takeLayoutChange)
    applyUiTheme(ImGui::GetStyle(), m_contentScale);

    Events::EventBus& events = deps.events;
    m_input.push_back(events.subscribe<Input::KeyPressed>(
        [](const Input::KeyPressed& e)
        {
            ImGuiIO& keys = ImGui::GetIO();
            updateModifier(keys, e.key, true);
            keys.AddKeyEvent(static_cast<ImGuiKey>(toImGuiKey(e.key)), true);
        }));
    m_input.push_back(events.subscribe<Input::KeyReleased>(
        [](const Input::KeyReleased& e)
        {
            ImGuiIO& keys = ImGui::GetIO();
            updateModifier(keys, e.key, false);
            keys.AddKeyEvent(static_cast<ImGuiKey>(toImGuiKey(e.key)), false);
        }));
    m_input.push_back(events.subscribe<Input::MouseMoved>([](const Input::MouseMoved& e)
                                                          { ImGui::GetIO().AddMousePosEvent(e.x, e.y); }));
    m_input.push_back(events.subscribe<Input::MouseButtonChanged>(
        [](const Input::MouseButtonChanged& e)
        {
            ImGuiIO& mouse = ImGui::GetIO();
            mouse.AddMousePosEvent(e.x, e.y);
            const int button = e.button == Input::MouseButton::Left     ? 0
                               : e.button == Input::MouseButton::Right  ? 1
                               : e.button == Input::MouseButton::Middle ? 2
                                                                        : -1;
            if (button >= 0)
            {
                mouse.AddMouseButtonEvent(button, e.down);
            }
        }));
    m_input.push_back(events.subscribe<Input::MouseWheelScrolled>([](const Input::MouseWheelScrolled& e)
                                                                  { ImGui::GetIO().AddMouseWheelEvent(e.x, e.y); }));
    m_input.push_back(events.subscribe<Input::TextEntered>([](const Input::TextEntered& e)
                                                           { ImGui::GetIO().AddInputCharactersUTF8(e.text.c_str()); }));

    m_deviceUser = deps.device.addUser(
        "ImGui", [this](Rhi::IRhiDevice& device) { releaseGpu(device); },
        [](Rhi::IRhiDevice&) -> Result<void>
        {
            // Nothing to make now: textures come back through ImGui's requests, buffers on the next frame.
            return {};
        });
}

ImGuiLayer::~ImGuiLayer()
{
    m_input.clear();
    if (m_deps.device.hasDevice())
    {
        Rhi::IRhiDevice& device = m_deps.device.device();
        releaseGpu(device);
        for (const Rhi::BufferHandle buffer : m_spentStaging)
        {
            device.destroy(buffer);
        }
    }
    ImGui::DestroyContext(m_context);
}

void ImGuiLayer::releaseGpu(Rhi::IRhiDevice& device)
{
    for (TextureSlot& slot : m_textures)
    {
        device.destroy(slot.texture);
        slot = {};
    }
    for (const PendingUpload& upload : m_uploads)
    {
        device.destroy(upload.staging);
    }
    m_uploads.clear();
    for (FrameBuffers& frame : m_frames)
    {
        device.destroy(frame.vertices);
        device.destroy(frame.indices);
        device.destroy(frame.draws);
        frame = {};
    }
    // ImGui keeps its textures' pixels, so marking them destroyed makes it ask for them again (status WantCreate).
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
    {
        if (texture->Status != ImTextureStatus_Destroyed)
        {
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void ImGuiLayer::newFrame(float deltaSeconds, float width, float height, float framebufferScale, float contentScale)
{
    // Sizes and fonts follow the display's scale, so the UI is as big on a 125 % screen as Windows' own windows. The
    // style is rebuilt from the defaults rather than scaled again, which would compound.
    if (contentScale > 0.0F && contentScale != m_contentScale)
    {
        applyUiTheme(ImGui::GetStyle(), contentScale);
        m_contentScale = contentScale;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaSeconds > 0.0F ? deltaSeconds : 1.0F / 60.0F;
    io.DisplaySize = ImVec2{width, height};
    io.DisplayFramebufferScale = ImVec2{framebufferScale, framebufferScale};
    ImGui::NewFrame();
}

void ImGuiLayer::addFonts(UiFontFiles files)
{
    // The atlas reads the files whenever it bakes glyphs (ImGui 1.92 bakes on demand), so the bytes stay here for
    // the layer's life and the atlas doesn't free them.
    m_fontFiles = std::move(files);
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    const Memory::MemTagScope tag{Memory::MemTag::ImGui};
    const auto add = [&atlas](std::vector<std::byte>& file, float size, const char* name) -> ImFont*
    {
        if (file.empty())
        {
            return nullptr;
        }
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        std::snprintf(config.Name, sizeof(config.Name), "%s", name);
        return atlas.AddFontFromMemoryTTF(file.data(), static_cast<int>(file.size()), size, &config);
    };
    // The first font added is ImGui's default. Sizes in unscaled pixels; FontScaleDpi scales them.
    m_fonts.text = add(m_fontFiles.text, c_textFontSize, "Barlow Semi Condensed");
    m_fonts.mono = add(m_fontFiles.mono, c_monoFontSize, "Consolas");
    if (m_fonts.text == nullptr)
    {
        MEOW2_LOG_WARN(m_deps.log, "UI font not found: using ImGui's built-in font");
    }
}

void ImGuiLayer::skipFrame()
{
    ImGui::EndFrame();
}

bool ImGuiLayer::wantsMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantsKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiLayer::queueUpload(Rhi::IRhiDevice& device, ImTextureData& texture, TextureSlot& slot)
{
    const auto pixelCount = static_cast<std::size_t>(texture.Width) * static_cast<std::size_t>(texture.Height);
    Result<Rhi::BufferHandle> staging = device.createBuffer({.size = pixelCount * 4,
                                                             .memory = Rhi::RhiMemory::Upload,
                                                             .tag = Memory::MemTag::ImGui,
                                                             .name = "ImGui upload"});
    if (!staging)
    {
        MEOW2_LOG_ERROR(m_deps.log, "ImGui texture not uploaded: {}", staging.error());
        return false;
    }
    std::byte* target = device.mappedMemory(*staging).data();
    const auto* source = static_cast<const unsigned char*>(texture.GetPixels());
    if (texture.Format == ImTextureFormat_RGBA32)
    {
        std::memcpy(target, source, pixelCount * 4);
    }
    else // Alpha8: white with that alpha
    {
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            target[i * 4 + 0] = std::byte{255};
            target[i * 4 + 1] = std::byte{255};
            target[i * 4 + 2] = std::byte{255};
            target[i * 4 + 3] = static_cast<std::byte>(source[i]);
        }
    }
    m_uploads.push_back({.staging = *staging, .texture = slot.texture, .firstFill = !slot.ready});
    slot.ready = true;
    return true;
}

void ImGuiLayer::updateTextures(Rhi::IRhiDevice& device)
{
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
    {
        if (texture->Status == ImTextureStatus_WantCreate)
        {
            auto free = std::ranges::find_if(m_textures, [](const TextureSlot& slot) { return !slot.used; });
            if (free == m_textures.end())
            {
                m_textures.emplace_back();
                free = m_textures.end() - 1;
            }
            // sRGB: ImGui's colours are sRGB, and sampling decodes them like the shader decodes vertex colours.
            Result<Rhi::TextureHandle> created =
                device.createTexture({.width = static_cast<std::uint32_t>(texture->Width),
                                      .height = static_cast<std::uint32_t>(texture->Height),
                                      .format = Rhi::RhiFormat::Rgba8Srgb,
                                      .tag = Memory::MemTag::ImGui,
                                      .name = "ImGui texture"});
            if (!created)
            {
                MEOW2_LOG_ERROR(m_deps.log, "ImGui texture not created: {}", created.error());
                continue;
            }
            *free = {.texture = *created, .used = true, .ready = false};
            if (!queueUpload(device, *texture, *free))
            {
                device.destroy(free->texture);
                *free = {};
                continue;
            }
            texture->SetTexID(static_cast<ImTextureID>(free - m_textures.begin()) + 1);
            texture->SetStatus(ImTextureStatus_OK);
        }
        else if (texture->Status == ImTextureStatus_WantUpdates)
        {
            // The whole texture again: the font atlas changes rarely and is small, and the RHI copies whole textures.
            const auto slot = static_cast<std::size_t>(texture->GetTexID() - 1);
            if (slot < m_textures.size() && m_textures[slot].used && queueUpload(device, *texture, m_textures[slot]))
            {
                texture->SetStatus(ImTextureStatus_OK);
            }
        }
        else if (texture->Status == ImTextureStatus_WantDestroy &&
                 texture->UnusedFrames >= static_cast<int>(Rhi::c_maxFramesInFlight))
        {
            const auto slot = static_cast<std::size_t>(texture->GetTexID() - 1);
            if (slot < m_textures.size())
            {
                device.destroy(m_textures[slot].texture); // deferred until the GPU is done with it
                m_textures[slot] = {};
            }
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

bool ImGuiLayer::ensureBuffer(Rhi::IRhiDevice& device, Rhi::BufferHandle& buffer, std::uint64_t& capacity,
                              std::uint64_t bytes, const char* name)
{
    if (bytes <= capacity && buffer.isSet())
    {
        return true;
    }
    // Grow by half again, so a UI that grows a little each frame doesn't make a buffer every frame.
    const std::uint64_t size = std::max<std::uint64_t>({bytes + bytes / 2, capacity, 64 * 1024});
    Result<Rhi::BufferHandle> created = device.createBuffer(
        {.size = size, .memory = Rhi::RhiMemory::Upload, .tag = Memory::MemTag::ImGui, .name = name});
    if (!created)
    {
        MEOW2_LOG_ERROR(m_deps.log, "ImGui buffer not created: {}", created.error());
        return false;
    }
    device.destroy(buffer); // deferred: frames in flight may still read it
    buffer = *created;
    capacity = size;
    return true;
}

void ImGuiLayer::addPasses(Renderer::RenderGraph& graph, std::uint32_t frameSlot, Rhi::IRhiSwapchain& swapchain)
{
    ImGui::Render();
    Rhi::IRhiDevice& device = m_deps.device.device();
    updateTextures(device);

    if (!m_uploads.empty())
    {
        std::vector<PendingUpload> uploads = std::move(m_uploads);
        m_uploads.clear();
        for (const PendingUpload& upload : uploads)
        {
            m_spentStaging.push_back(upload.staging);
        }
        graph.addPass(
            {.name = "ImGui textures",
             .accesses = {},
             .record =
                 [uploads = std::move(uploads)](Rhi::IRhiCommandList& list)
             {
                 for (const PendingUpload& upload : uploads)
                 {
                     list.barrier(upload.texture,
                                  upload.firstFill ? Rhi::ResourceState::Undefined : Rhi::ResourceState::ShaderRead,
                                  Rhi::ResourceState::CopyTarget);
                     list.copyBufferToTexture(upload.staging, upload.texture);
                     list.barrier(upload.texture, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::ShaderRead);
                 }
             },
             .small = true});
    }

    const ImDrawData* data = ImGui::GetDrawData();
    if (data == nullptr || data->TotalIdxCount == 0 || swapchain.width() == 0)
    {
        return;
    }
    FrameBuffers& frame = m_frames[frameSlot];
    std::size_t commandCount = 0;
    for (const ImDrawList* drawList : data->CmdLists)
    {
        commandCount += static_cast<std::size_t>(drawList->CmdBuffer.Size);
    }
    const auto vertexBytes = static_cast<std::uint64_t>(data->TotalVtxCount) * sizeof(ImDrawVert);
    const auto indexBytes = static_cast<std::uint64_t>(data->TotalIdxCount) * sizeof(std::uint32_t);
    if (!ensureBuffer(device, frame.vertices, frame.vertexBytes, vertexBytes, "ImGui vertices") ||
        !ensureBuffer(device, frame.indices, frame.indexBytes, indexBytes, "ImGui indices") ||
        !ensureBuffer(device, frame.draws, frame.drawBytes, commandCount * sizeof(Shader::UiDraw), "ImGui draws"))
    {
        return;
    }

    // ImGui's pixels → clip space (Y up), from its display rectangle.
    const ImVec2 position = data->DisplayPos;
    const ImVec2 size = data->DisplaySize;
    const Math::Vec4 scaleTranslate{2.0F / size.x, -2.0F / size.y, -1.0F - position.x * 2.0F / size.x,
                                    1.0F + position.y * 2.0F / size.y};
    const ImVec2 scale = data->FramebufferScale;
    const auto targetWidth = static_cast<float>(swapchain.width());
    const auto targetHeight = static_cast<float>(swapchain.height());

    std::byte* vertices = device.mappedMemory(frame.vertices).data();
    auto* indices = reinterpret_cast<std::uint32_t*>(device.mappedMemory(frame.indices).data());
    std::byte* draws = device.mappedMemory(frame.draws).data();
    const std::uint32_t vertexBuffer = device.bindlessIndex(frame.vertices);
    const std::uint32_t indexBuffer = device.bindlessIndex(frame.indices);
    std::vector<UiCommand> commands;
    commands.reserve(commandCount);
    std::uint32_t vertexBase = 0;
    std::uint32_t indexBase = 0;
    for (const ImDrawList* drawList : data->CmdLists)
    {
        std::memcpy(vertices + std::size_t{vertexBase} * sizeof(ImDrawVert), drawList->VtxBuffer.Data,
                    static_cast<std::size_t>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert));
        for (int i = 0; i < drawList->IdxBuffer.Size; ++i)
        {
            indices[indexBase + static_cast<std::uint32_t>(i)] = drawList->IdxBuffer[i];
        }
        for (const ImDrawCmd& command : drawList->CmdBuffer)
        {
            if (command.UserCallback != nullptr || command.ElemCount == 0)
            {
                continue; // no callbacks are used in Meow2Renderer
            }
            // Clip rectangle → target pixels, clamped to the target.
            const float left = std::clamp((command.ClipRect.x - position.x) * scale.x, 0.0F, targetWidth);
            const float top = std::clamp((command.ClipRect.y - position.y) * scale.y, 0.0F, targetHeight);
            const float right = std::clamp((command.ClipRect.z - position.x) * scale.x, 0.0F, targetWidth);
            const float bottom = std::clamp((command.ClipRect.w - position.y) * scale.y, 0.0F, targetHeight);
            if (right <= left || bottom <= top)
            {
                continue;
            }
            const ImTextureID id = command.GetTexID();
            const auto slot = static_cast<std::size_t>(id) - 1;
            const bool textured = id != ImTextureID_Invalid && slot < m_textures.size() && m_textures[slot].used;
            Shader::UiDraw draw{};
            draw.scaleTranslate = scaleTranslate;
            draw.vertexBuffer = vertexBuffer;
            draw.indexBuffer = indexBuffer;
            draw.vertexOffset = vertexBase + command.VtxOffset;
            draw.texture = textured ? device.bindlessIndex(m_textures[slot].texture) : Rhi::c_invalidBindlessIndex;
            const auto record = static_cast<std::uint32_t>(commands.size());
            std::memcpy(draws + std::size_t{record} * sizeof(draw), &draw, sizeof(draw));
            commands.push_back({.scissor = {.x = static_cast<std::int32_t>(left),
                                            .y = static_cast<std::int32_t>(top),
                                            .width = static_cast<std::uint32_t>(std::ceil(right - left)),
                                            .height = static_cast<std::uint32_t>(std::ceil(bottom - top))},
                                .record = record,
                                .firstIndex = indexBase + command.IdxOffset,
                                .indexCount = command.ElemCount});
        }
        vertexBase += static_cast<std::uint32_t>(drawList->VtxBuffer.Size);
        indexBase += static_cast<std::uint32_t>(drawList->IdxBuffer.Size);
    }
    if (commands.empty())
    {
        return;
    }

    const Rhi::PipelineHandle pipeline = m_deps.pipelines.get(*m_pipeline);
    const std::uint32_t drawBuffer = device.bindlessIndex(frame.draws);
    graph.addPass(
        {.name = "ImGui",
         .accesses = {},
         .record = [pipeline, drawBuffer, &swapchain, commands = std::move(commands)](Rhi::IRhiCommandList& list)
         {
             // The scene drew to this image in another list: order those writes before ours.
             list.barrier(swapchain);
             list.beginRendering(swapchain, std::nullopt);
             list.setPipeline(pipeline);
             for (const UiCommand& command : commands)
             {
                 list.setScissor(command.scissor);
                 Shader::PushConstants push{};
                 push.frameBuffer = drawBuffer;
                 push.instance = command.record;
                 push.userData = command.firstIndex;
                 list.setPushConstants(push);
                 list.draw(command.indexCount);
             }
             list.endRendering();
         }});
}

void ImGuiLayer::loadLayout(std::string_view layout)
{
    if (!layout.empty())
    {
        ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
    }
}

std::optional<std::string> ImGuiLayer::takeLayoutChange()
{
    // With no IniFilename, ImGui only raises this flag (io.IniSavingRate seconds after a change) and leaves the saving
    // to us.
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantSaveIniSettings)
    {
        return std::nullopt;
    }
    io.WantSaveIniSettings = false;
    std::size_t size = 0;
    const char* text = ImGui::SaveIniSettingsToMemory(&size);
    return std::string{text, size};
}

void ImGuiLayer::endFrame()
{
    Rhi::IRhiDevice& device = m_deps.device.device();
    for (const Rhi::BufferHandle buffer : m_spentStaging)
    {
        device.destroy(buffer); // deferred: kept until the GPU has run the copy
    }
    m_spentStaging.clear();
}
} // namespace Meow2::RendererApp
