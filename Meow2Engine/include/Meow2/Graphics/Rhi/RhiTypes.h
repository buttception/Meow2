/// @file
/// @brief Plain data shared by the RHI interfaces: backends, handles, descriptions and GPU information.

#pragma once

#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Graphics/Rhi/RhiHandle.h>
#include <Meow2/Platform/Window/Window.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Meow2::Rhi
{
/// @brief Frames the CPU may record ahead of the GPU. More adds latency, fewer makes the CPU wait for the GPU.
inline constexpr std::uint32_t c_maxFramesInFlight = 2;

/// @brief The bindless index that names nothing. Slot 0 of the heap is never handed out, so a zeroed index is caught.
inline constexpr std::uint32_t c_invalidBindlessIndex = 0;

/// @brief Sampler heap slot of the sampler every device creates: linear filtering, repeat addressing.
inline constexpr std::uint32_t c_defaultSamplerIndex = 0;

/// @brief The graphics APIs behind the RHI, chosen at runtime (techstack.md §11).
enum class RhiBackend : std::uint8_t
{
    Vulkan, ///< Vulkan 1.3; every platform.
    D3D12   ///< Direct3D 12; Windows only.
};

/// @brief Pixel formats of swapchains and textures.
enum class RhiFormat : std::uint8_t
{
    Unknown,     ///< Not set.
    Bgra8Srgb,   ///< 8-bit BGRA, sRGB-encoded.
    Rgba8Srgb,   ///< 8-bit RGBA, sRGB-encoded.
    Rgba8Unorm,  ///< 8-bit RGBA, linear.
    Rgba16Float, ///< 16-bit float RGBA.
    R32Float,    ///< 32-bit float, one channel.
    D32Float     ///< 32-bit float depth (techstack.md §10).
};

/// @brief Where a buffer's memory lives, and so who can reach it.
enum class RhiMemory : std::uint8_t
{
    GpuOnly, ///< Fastest for the GPU; the CPU can't reach it. Fill it by copying from an Upload buffer.
    Upload,  ///< The CPU writes, the GPU reads. Stays mapped.
    Readback ///< The GPU writes, the CPU reads. Stays mapped.
};

/// @brief What a resource is being used for. Barriers move it from one state to another.
enum class ResourceState : std::uint8_t
{
    Undefined,    ///< Contents don't matter; only valid as the state a barrier starts from.
    ColorTarget,  ///< Written as a colour attachment.
    DepthTarget,  ///< Read and written as a depth attachment.
    ShaderRead,   ///< Read by shaders.
    ShaderWrite,  ///< Read and written by shaders.
    CopySource,   ///< Read by a copy.
    CopyTarget,   ///< Written by a copy.
    IndirectArgs, ///< Read as indirect draw arguments.
    HostRead      ///< Read by the CPU.
};

/// @brief Tag of BufferHandle.
struct BufferTag
{
};
/// @brief Tag of TextureHandle.
struct TextureTag
{
};
/// @brief Tag of SamplerHandle.
struct SamplerTag
{
};
/// @brief Tag of PipelineHandle.
struct PipelineTag
{
};

using BufferHandle = Handle<BufferTag>;     ///< A buffer on a device.
using TextureHandle = Handle<TextureTag>;   ///< A texture on a device.
using SamplerHandle = Handle<SamplerTag>;   ///< A sampler on a device.
using PipelineHandle = Handle<PipelineTag>; ///< A graphics or compute pipeline on a device.

/// @brief What to create a device with. Plain data.
struct RhiDeviceDesc
{
    RhiBackend backend = RhiBackend::Vulkan; ///< API to use.
    std::string appName = "Meow2";           ///< Reported to the driver and to tools such as RenderDoc.
    bool validation = false;                 ///< Enable the API's validation layer, if it's installed.
    bool presentation = true;                ///< False for a device that never opens a swapchain (headless).
    std::uint32_t recordingThreads = 1;      ///< Threads that may record at once: a command pool each, per frame slot.
    std::span<const std::byte> pipelineCache{}; ///< From IRhiDevice::pipelineCacheData(); ignored if from another GPU.
};

/// @brief What to create a swapchain with. Plain data.
struct RhiSwapchainDesc
{
    Platform::NativeWindowHandle window{}; ///< The window to draw to. Not kept after creation.
    std::uint32_t width = 0;               ///< Drawable width in pixels; 0 while minimised.
    std::uint32_t height = 0;              ///< Drawable height in pixels; 0 while minimised.
    bool vsync = true;                     ///< Wait for the display; false shows frames as soon as they're ready.
    std::string name = "Swapchain";        ///< Shown in debugging tools.
};

/// @brief What to create a buffer with. Plain data. Every buffer can be used for everything: storage, copies, indices
///        and indirect arguments.
struct BufferDesc
{
    std::uint64_t size = 0;                   ///< Size in bytes; > 0.
    RhiMemory memory = RhiMemory::GpuOnly;    ///< Where it lives.
    Memory::MemTag tag = Memory::MemTag::Rhi; ///< What it counts under in GpuMemoryStats.
    std::string name = "Buffer";              ///< Shown in debugging tools.
};

/// @brief What to create a texture with. Plain data. Every texture can be sampled and copied to and from.
struct TextureDesc
{
    std::uint32_t width = 0;                  ///< Width in pixels; > 0.
    std::uint32_t height = 0;                 ///< Height in pixels; > 0.
    RhiFormat format = RhiFormat::Rgba8Unorm; ///< Pixel format.
    std::uint32_t mipLevels = 1;              ///< Number of mip levels; >= 1.
    bool renderTarget = false;                ///< Can be drawn to (colour or depth, by its format).
    bool storage = false;                     ///< Can be written by shaders; gets a second heap slot.
    Memory::MemTag tag = Memory::MemTag::Rhi; ///< What it counts under in GpuMemoryStats.
    std::string name = "Texture";             ///< Shown in debugging tools.
};

/// @brief How a sampler filters.
enum class SamplerFilter : std::uint8_t
{
    Nearest, ///< The closest texel.
    Linear   ///< A blend of the closest texels and mip levels.
};

/// @brief What a sampler does outside 0..1.
enum class SamplerAddress : std::uint8_t
{
    Repeat, ///< Wraps around.
    Clamp,  ///< Keeps the edge texel.
    Mirror  ///< Wraps around, flipping every other repeat (glTF's MIRRORED_REPEAT).
};

/// @brief What to create a sampler with. Plain data.
struct SamplerDesc
{
    SamplerFilter filter = SamplerFilter::Linear;     ///< Filtering.
    SamplerAddress addressU = SamplerAddress::Repeat; ///< Addressing across (U, and W for 3D textures).
    SamplerAddress addressV = SamplerAddress::Repeat; ///< Addressing down (V).
    float maxAnisotropy = 1.0F;                       ///< 1 turns anisotropic filtering off.
    std::string name = "Sampler";                     ///< Shown in debugging tools.
};

/// @brief Which triangles a graphics pipeline skips.
enum class CullMode : std::uint8_t
{
    None, ///< Draw both sides.
    Back, ///< Skip triangles facing away. Front faces are clockwise on screen, as in D3D.
    Front ///< Skip triangles facing the viewer: for meshes mirrored by a negative scale, whose winding is reversed.
};

/// @brief How a graphics pipeline combines its output with what's in the target.
enum class BlendMode : std::uint8_t
{
    None, ///< Overwrite.
    Alpha ///< Straight alpha: `src × srcAlpha + dst × (1 − srcAlpha)`; alpha accumulates the same way. For UI.
};

/// @brief A rectangle drawing is clipped to, in pixels from the target's top-left corner. Plain data.
struct ScissorRect
{
    std::int32_t x = 0;       ///< Left edge.
    std::int32_t y = 0;       ///< Top edge.
    std::uint32_t width = 0;  ///< Width in pixels.
    std::uint32_t height = 0; ///< Height in pixels.
};

/// @brief What to create a graphics pipeline with. Plain data. Depth uses GREATER_EQUAL, for reversed Z (techstack.md
///        §10). The bytecode is only read during the call.
struct GraphicsPipelineDesc
{
    std::span<const std::byte> vertexCode{};    ///< Vertex shader bytecode for this device's backend.
    std::string vertexEntry = "vsMain";         ///< Its entry point.
    std::span<const std::byte> pixelCode{};     ///< Pixel shader bytecode.
    std::string pixelEntry = "psMain";          ///< Its entry point.
    RhiFormat colorFormat = RhiFormat::Unknown; ///< Format of the colour target.
    RhiFormat depthFormat = RhiFormat::Unknown; ///< Format of the depth target; Unknown for none.
    bool depthWrite = true;                     ///< Whether drawing updates depth.
    CullMode cull = CullMode::None;             ///< Which triangles to skip.
    BlendMode blend = BlendMode::None;          ///< How output combines with the target.
    std::string name = "Pipeline";              ///< Shown in debugging tools.
};

/// @brief What to create a compute pipeline with. Plain data. The bytecode is only read during the call.
struct ComputePipelineDesc
{
    std::span<const std::byte> code{}; ///< Compute shader bytecode for this device's backend.
    std::string entry = "csMain";      ///< Its entry point.
    std::string name = "Pipeline";     ///< Shown in debugging tools.
};

/// @brief How much GPU memory is in use. Plain data.
struct GpuMemoryStats
{
    std::uint64_t usedBytes = 0;   ///< Everything this process uses, as the driver reports it.
    std::uint64_t budgetBytes = 0; ///< What the OS says this process can use before things slow down.
    std::array<std::uint64_t, Memory::c_memTagCount> bytesByTag{}; ///< Bytes of live buffers and textures, per MemTag.
};

/// @brief The GPU a device runs on. Plain data.
struct RhiAdapterInfo
{
    std::string name;       ///< Product name.
    std::string apiVersion; ///< API version the driver supports, e.g. `1.3.280`.
    bool discrete = false;  ///< A dedicated GPU rather than one built into the CPU.
    bool software = false;  ///< Runs on the CPU, e.g. Mesa's lavapipe.
};
} // namespace Meow2::Rhi
