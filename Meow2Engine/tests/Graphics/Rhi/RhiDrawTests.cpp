/// @file
/// @brief RHI tests that run real shaders on a real GPU with no window: resources, the heap, pipelines and the pipeline
///        cache (`MEOW2_GPU_TESTS=ON`). Results are read back and checked pixel by pixel.

#include "Graphics/Rhi/RhiTestBackend.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderHotReload.h>
#include <Meow2/Assets/Texture/CookedTexture.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Math/Color.h>
#include <Meow2/Math/Packing.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Meow2::Rhi
{
namespace
{
namespace fs = std::filesystem;

constexpr std::string_view c_triangleShader = "01a1b5e2-3c10-8000-80a4-7b19d2e6c051";
constexpr std::string_view c_textureShader = "01a1b5e2-3c11-8000-80a4-7b19d2e6c052";
constexpr std::string_view c_probeShader = "01a1b158-0216-8000-808e-12fd3223a2ac";
constexpr std::string_view c_statesShader = "01a1b5e2-3c13-8000-80a4-7b19d2e6c054";  // RhiStates.hlsl
constexpr std::string_view c_storageShader = "01a1b5e2-3c15-8000-80a4-7b19d2e6c056"; // RhiStorage.hlsl
constexpr std::uint32_t c_targetSize = 64;

/// @brief What BindlessTriangle.hlsl reads from the heap.
struct TriangleVertex
{
    Math::Vec4 position;
    Math::Vec4 color;
};

/// @brief One RGBA8 pixel.
struct Pixel
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

AssetId assetId(std::string_view text)
{
    const Result<Uuid> uuid = Uuid::parse(text);
    EXPECT_TRUE(uuid);
    const Result<AssetId> id = AssetId::fromUuid(*uuid);
    EXPECT_TRUE(id);
    return *id;
}

/// @brief A headless device with validation, and the cooked shaders of the repository.
class RhiDrawTest : public ::testing::Test
{
protected:
    RhiDrawTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("RHI"))
        , m_files(m_log)
        , m_assets(m_files, m_ids, m_log)
    {
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice(
            {.backend = c_testBackend, .appName = "Meow2 tests", .validation = true, .presentation = false}, m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_device = std::move(*device);
        ASSERT_TRUE(m_device->validationActive());
    }

    /// @brief The bytecode of an entry point of a shader's default variant.
    std::vector<std::byte> bytecode(std::string_view shader, std::string_view entry)
    {
        Result<Assets::AssetHandle<Assets::ShaderAsset>> handle = m_assets.load<Assets::ShaderAsset>(assetId(shader));
        EXPECT_TRUE(handle) << handle.error().message;
        const std::shared_ptr<const Assets::ShaderAsset> asset = handle->get();
        const Result<Assets::ShaderVariantKey> key = Assets::variantKey(asset->meta(), {});
        EXPECT_TRUE(key);
        const Result<std::span<const std::byte>> code = asset->bytecode(*key, entry, c_testTarget);
        EXPECT_TRUE(code) << code.error().message;
        return {code->begin(), code->end()};
    }

    PipelineHandle graphicsPipeline(std::string_view shader)
    {
        const std::vector<std::byte> vertex = bytecode(shader, "vsMain");
        const std::vector<std::byte> pixel = bytecode(shader, "psMain");
        Result<PipelineHandle> pipeline = m_device->createGraphicsPipeline(
            {.vertexCode = vertex, .pixelCode = pixel, .colorFormat = RhiFormat::Rgba8Unorm, .name = "Test pipeline"});
        EXPECT_TRUE(pipeline) << pipeline.error().message;
        return *pipeline;
    }

    BufferHandle makeBuffer(std::uint64_t size, RhiMemory memory)
    {
        Result<BufferHandle> buffer = m_device->createBuffer({.size = size, .memory = memory});
        EXPECT_TRUE(buffer) << buffer.error().message;
        return *buffer;
    }

    TextureHandle makeTarget()
    {
        Result<TextureHandle> texture = m_device->createTexture(
            {.width = c_targetSize, .height = c_targetSize, .format = RhiFormat::Rgba8Unorm, .renderTarget = true});
        EXPECT_TRUE(texture) << texture.error().message;
        return *texture;
    }

    /// @brief Runs one frame and waits for the GPU.
    template <class Record>
    void runFrame(Record record)
    {
        Result<IRhiCommandList*> list = m_device->beginFrame();
        ASSERT_TRUE(list) << list.error().message;
        record(**list);
        const Result<void> ended = m_device->endFrame();
        ASSERT_TRUE(ended) << ended.error().message;
        m_device->waitIdle();
    }

    /// @brief Draws into a target, then copies it to a readback buffer.
    template <class Draw>
    void renderToReadback(TextureHandle target, BufferHandle readback, Draw draw)
    {
        runFrame(
            [&](IRhiCommandList& list)
            {
                list.barrier(target, ResourceState::Undefined, ResourceState::ColorTarget);
                list.beginRendering(RenderTargets{.color = target, .clearColor = Math::Color{.a = 1.0F}});
                draw(list);
                list.endRendering();
                list.barrier(target, ResourceState::ColorTarget, ResourceState::CopySource);
                list.copyTextureToBuffer(target, readback);
                list.barrier(readback, ResourceState::CopyTarget, ResourceState::HostRead);
            });
    }

    Pixel pixelAt(BufferHandle readback, std::uint32_t x, std::uint32_t y)
    {
        Pixel pixel;
        const std::span<std::byte> memory = m_device->mappedMemory(readback);
        std::memcpy(&pixel, memory.data() + (static_cast<std::size_t>(y) * c_targetSize + x) * sizeof(Pixel),
                    sizeof(Pixel));
        return pixel;
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    Assets::AssetManager m_assets;
    std::unique_ptr<IRhiDevice> m_device;
};
} // namespace

TEST_F(RhiDrawTest, BuffersMapAndCopy)
{
    constexpr std::array<std::uint32_t, 4> c_values{1, 22, 333, 4444};
    const BufferHandle upload = makeBuffer(sizeof(c_values), RhiMemory::Upload);
    const BufferHandle gpu = makeBuffer(sizeof(c_values), RhiMemory::GpuOnly);
    const BufferHandle readback = makeBuffer(sizeof(c_values), RhiMemory::Readback);
    EXPECT_TRUE(m_device->mappedMemory(gpu).empty());
    EXPECT_NE(m_device->bindlessIndex(gpu), c_invalidBindlessIndex);

    std::memcpy(m_device->mappedMemory(upload).data(), c_values.data(), sizeof(c_values));
    runFrame(
        [&](IRhiCommandList& list)
        {
            list.copyBuffer(upload, gpu, sizeof(c_values));
            list.barrier(gpu, ResourceState::CopyTarget, ResourceState::CopySource);
            list.copyBuffer(gpu, readback, sizeof(c_values));
            list.barrier(readback, ResourceState::CopyTarget, ResourceState::HostRead);
        });

    std::array<std::uint32_t, 4> result{};
    std::memcpy(result.data(), m_device->mappedMemory(readback).data(), sizeof(result));
    EXPECT_EQ(result, c_values);

    m_device->destroy(upload);
    m_device->destroy(gpu);
    m_device->destroy(readback);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, DestroyWaitsForTheGpu)
{
    const PipelineHandle pipeline = graphicsPipeline(c_triangleShader);
    const TextureHandle target = makeTarget();
    const BufferHandle vertices = makeBuffer(sizeof(TriangleVertex) * 3, RhiMemory::Upload);
    const std::uint32_t slot = m_device->bindlessIndex(vertices);

    // Destroyed while the frame that uses it is still recording: the GPU object must outlive that frame.
    Result<IRhiCommandList*> list = m_device->beginFrame();
    ASSERT_TRUE(list);
    (*list)->barrier(target, ResourceState::Undefined, ResourceState::ColorTarget);
    (*list)->beginRendering(RenderTargets{.color = target, .clearColor = Math::Color{}});
    (*list)->setPipeline(pipeline);
    (*list)->setPushConstants({.userData = slot});
    (*list)->draw(3);
    (*list)->endRendering();
    m_device->destroy(vertices);
    EXPECT_EQ(m_device->bindlessIndex(vertices), c_invalidBindlessIndex); // the handle is stale at once
    ASSERT_TRUE(m_device->endFrame());

    // The slot isn't reused until the GPU has finished with it.
    const BufferHandle meanwhile = makeBuffer(16, RhiMemory::GpuOnly);
    EXPECT_NE(m_device->bindlessIndex(meanwhile), slot);

    m_device->waitIdle();
    const BufferHandle after = makeBuffer(16, RhiMemory::GpuOnly);
    EXPECT_EQ(m_device->bindlessIndex(after), slot);

    m_device->destroy(meanwhile);
    m_device->destroy(after);
    m_device->destroy(target);
    m_device->destroy(pipeline);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, ComputeWritesThroughTheHeap)
{
    constexpr std::uint32_t c_count = 64; // the probe's thread group size
    const std::vector<std::byte> code = bytecode(c_probeShader, "csMain");
    Result<PipelineHandle> pipeline = m_device->createComputePipeline({.code = code, .name = "Probe"});
    ASSERT_TRUE(pipeline) << pipeline.error().message;

    const BufferHandle instances = makeBuffer(sizeof(Shader::InstanceData) * c_count, RhiMemory::Upload);
    const BufferHandle output = makeBuffer(sizeof(std::uint32_t) * c_count, RhiMemory::GpuOnly);
    const BufferHandle readback = makeBuffer(sizeof(std::uint32_t) * c_count, RhiMemory::Readback);
    std::vector<Shader::InstanceData> data(c_count);
    for (std::uint32_t i = 0; i < c_count; ++i)
    {
        data[i].material = i * 3;
    }
    std::memcpy(m_device->mappedMemory(instances).data(), data.data(), sizeof(Shader::InstanceData) * c_count);

    runFrame(
        [&](IRhiCommandList& list)
        {
            list.setPipeline(*pipeline);
            list.setPushConstants({.instanceBuffer = m_device->bindlessIndex(instances),
                                   .userData = m_device->bindlessStorageIndex(output)});
            list.dispatch(1);
            list.barrier(output, ResourceState::ShaderWrite, ResourceState::CopySource);
            list.copyBuffer(output, readback, sizeof(std::uint32_t) * c_count);
            list.barrier(readback, ResourceState::CopyTarget, ResourceState::HostRead);
        });

    std::vector<std::uint32_t> result(c_count);
    std::memcpy(result.data(), m_device->mappedMemory(readback).data(), sizeof(std::uint32_t) * c_count);
    for (std::uint32_t i = 0; i < c_count; ++i)
    {
        EXPECT_EQ(result[i], i * 3) << "at " << i;
    }

    m_device->destroy(*pipeline);
    m_device->destroy(instances);
    m_device->destroy(output);
    m_device->destroy(readback);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, TriangleDrawsThroughTheHeap)
{
    const PipelineHandle pipeline = graphicsPipeline(c_triangleShader);
    const TextureHandle target = makeTarget();
    const BufferHandle readback = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
    const BufferHandle vertices = makeBuffer(sizeof(TriangleVertex) * 3, RhiMemory::Upload);
    // Pointing up in clip space, all red.
    const std::array<TriangleVertex, 3> triangle{TriangleVertex{{0.0F, 0.5F, 0.5F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F}},
                                                 TriangleVertex{{0.5F, -0.5F, 0.5F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F}},
                                                 TriangleVertex{{-0.5F, -0.5F, 0.5F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F}}};
    std::memcpy(m_device->mappedMemory(vertices).data(), triangle.data(), sizeof(triangle));

    renderToReadback(target, readback,
                     [&](IRhiCommandList& list)
                     {
                         list.setPipeline(pipeline);
                         list.setPushConstants({.userData = m_device->bindlessIndex(vertices)});
                         list.draw(3);
                     });

    // The tip is at the top of the image: if Y weren't flipped, (19, 44) would miss and (19, 19) would hit.
    EXPECT_EQ(pixelAt(readback, 32, 18).r, 255) << "just below the tip";
    EXPECT_EQ(pixelAt(readback, 32, 12).r, 0) << "above the tip";
    EXPECT_EQ(pixelAt(readback, 19, 44).r, 255) << "the wide end's left corner";
    EXPECT_EQ(pixelAt(readback, 19, 19).r, 0) << "outside, beside the tip";
    EXPECT_EQ(pixelAt(readback, 2, 2).r, 0) << "the clear colour";

    m_device->destroy(pipeline);
    m_device->destroy(target);
    m_device->destroy(readback);
    m_device->destroy(vertices);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, TextureSamplesThroughTheHeap)
{
    constexpr Pixel c_color{.r = 200, .g = 100, .b = 50, .a = 255};
    constexpr std::uint32_t c_size = 4;
    Result<TextureHandle> texture =
        m_device->createTexture({.width = c_size, .height = c_size, .format = RhiFormat::Rgba8Unorm});
    ASSERT_TRUE(texture) << texture.error().message;
    Result<SamplerHandle> sampler = m_device->createSampler(
        {.filter = SamplerFilter::Nearest, .addressU = SamplerAddress::Clamp, .addressV = SamplerAddress::Clamp});
    ASSERT_TRUE(sampler) << sampler.error().message;
    EXPECT_NE(m_device->bindlessIndex(*sampler), c_defaultSamplerIndex);

    const BufferHandle upload = makeBuffer(sizeof(Pixel) * c_size * c_size, RhiMemory::Upload);
    const std::vector<Pixel> pixels(c_size * c_size, c_color);
    std::memcpy(m_device->mappedMemory(upload).data(), pixels.data(), sizeof(Pixel) * pixels.size());
    runFrame(
        [&](IRhiCommandList& list)
        {
            list.barrier(*texture, ResourceState::Undefined, ResourceState::CopyTarget);
            list.copyBufferToTexture(upload, *texture);
            list.barrier(*texture, ResourceState::CopyTarget, ResourceState::ShaderRead);
        });

    const PipelineHandle pipeline = graphicsPipeline(c_textureShader);
    const TextureHandle target = makeTarget();
    const BufferHandle readback = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
    renderToReadback(target, readback,
                     [&](IRhiCommandList& list)
                     {
                         list.setPipeline(pipeline);
                         list.setPushConstants({.instance = m_device->bindlessIndex(*sampler),
                                                .userData = m_device->bindlessIndex(*texture)});
                         list.draw(3);
                     });

    const Pixel center = pixelAt(readback, 32, 32);
    EXPECT_EQ(center.r, c_color.r);
    EXPECT_EQ(center.g, c_color.g);
    EXPECT_EQ(center.b, c_color.b);

    m_device->destroy(*texture);
    m_device->destroy(*sampler);
    m_device->destroy(upload);
    m_device->destroy(pipeline);
    m_device->destroy(target);
    m_device->destroy(readback);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, CopiesEveryMipLevel)
{
    // Two textures of two levels each, red on level 0 and green on level 1, uploaded from one buffer at offsets. Drawn
    // across the 64-pixel target, the 256-pixel texture is minified past level 1 (nearest mip: level 1, green) and the
    // 16-pixel one magnified (level 0, red).
    constexpr Pixel c_red{.r = 255, .g = 0, .b = 0, .a = 255};
    constexpr Pixel c_green{.r = 0, .g = 255, .b = 0, .a = 255};
    struct Case
    {
        std::uint32_t size;
        Pixel expected;
    };
    const Result<SamplerHandle> sampler = m_device->createSampler({.filter = SamplerFilter::Nearest});
    ASSERT_TRUE(sampler) << sampler.error().message;
    const PipelineHandle pipeline = graphicsPipeline(c_textureShader);
    for (const Case& test : {Case{256, c_green}, Case{16, c_red}})
    {
        Result<TextureHandle> texture = m_device->createTexture(
            {.width = test.size, .height = test.size, .format = RhiFormat::Rgba8Unorm, .mipLevels = 2});
        ASSERT_TRUE(texture) << texture.error().message;
        const std::size_t level0 = std::size_t{test.size} * test.size;
        const std::size_t level1 = level0 / 4;
        std::vector<Pixel> pixels(level0, c_red);
        pixels.insert(pixels.end(), level1, c_green);
        const BufferHandle upload = makeBuffer(sizeof(Pixel) * pixels.size(), RhiMemory::Upload);
        std::memcpy(m_device->mappedMemory(upload).data(), pixels.data(), sizeof(Pixel) * pixels.size());
        runFrame(
            [&](IRhiCommandList& list)
            {
                list.barrier(*texture, ResourceState::Undefined, ResourceState::CopyTarget);
                list.copyBufferToTexture(upload, *texture, 0, 0);
                list.copyBufferToTexture(upload, *texture, 1, sizeof(Pixel) * level0);
                list.barrier(*texture, ResourceState::CopyTarget, ResourceState::ShaderRead);
            });

        const TextureHandle target = makeTarget();
        const BufferHandle readback = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
        renderToReadback(target, readback,
                         [&](IRhiCommandList& list)
                         {
                             list.setPipeline(pipeline);
                             list.setPushConstants({.instance = m_device->bindlessIndex(*sampler),
                                                    .userData = m_device->bindlessIndex(*texture)});
                             list.draw(3);
                         });
        const Pixel center = pixelAt(readback, 32, 32);
        EXPECT_EQ(center.r, test.expected.r) << test.size << " pixels";
        EXPECT_EQ(center.g, test.expected.g) << test.size << " pixels";
        m_device->destroy(*texture);
        m_device->destroy(upload);
        m_device->destroy(target);
        m_device->destroy(readback);
    }
    m_device->destroy(pipeline);
    m_device->destroy(*sampler);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, SamplesBlockCompressedMips)
{
    // As CopiesEveryMipLevel, with BC7 blocks cooked by DirectXTex: red on level 0, green on level 1. The 4-pixel
    // texture's level 1 is 2 × 2, less than one block: its copy covers a whole block.
    constexpr Pixel c_red{.r = 255, .g = 0, .b = 0, .a = 255};
    constexpr Pixel c_green{.r = 0, .g = 255, .b = 0, .a = 255};
    const auto flat = [](std::uint32_t size, Pixel colour)
    {
        Assets::Image image{.width = size, .height = size, .pixels = {}};
        for (std::uint32_t i = 0; i < size * size; ++i)
        {
            image.pixels.insert(image.pixels.end(),
                                {std::byte{colour.r}, std::byte{colour.g}, std::byte{colour.b}, std::byte{colour.a}});
        }
        return image;
    };
    struct Case
    {
        std::uint32_t size;
        Pixel expected;
    };
    const Result<SamplerHandle> sampler = m_device->createSampler({.filter = SamplerFilter::Nearest});
    ASSERT_TRUE(sampler) << sampler.error().message;
    const PipelineHandle pipeline = graphicsPipeline(c_textureShader);
    for (const Case& test : {Case{256, c_green}, Case{16, c_red}, Case{4, c_red}})
    {
        const Result<Assets::CookedTexture> level0 =
            Assets::cookTexture(flat(test.size, c_red), Assets::TextureKind::Data, true);
        const Result<Assets::CookedTexture> level1 =
            Assets::cookTexture(flat(test.size / 2, c_green), Assets::TextureKind::Data, true);
        ASSERT_TRUE(level0) << level0.error().message;
        ASSERT_TRUE(level1) << level1.error().message;
        // A 2-pixel image isn't whole blocks, so it cooks as RGBA8: its level is compressed as a small mip instead.
        const std::vector<std::byte>& level1Bytes =
            level1->format == Assets::CookedFormat::Bc7Unorm ? level1->mips[0].bytes : level0->mips[1].bytes;
        const std::vector<std::byte>& level0Bytes = level0->mips[0].bytes;
        ASSERT_EQ(level0->format, Assets::CookedFormat::Bc7Unorm);
        Result<TextureHandle> texture = m_device->createTexture(
            {.width = test.size, .height = test.size, .format = RhiFormat::Bc7Unorm, .mipLevels = 2});
        ASSERT_TRUE(texture) << texture.error().message;
        const BufferHandle upload = makeBuffer(level0Bytes.size() + level1Bytes.size(), RhiMemory::Upload);
        std::memcpy(m_device->mappedMemory(upload).data(), level0Bytes.data(), level0Bytes.size());
        std::memcpy(m_device->mappedMemory(upload).data() + level0Bytes.size(), level1Bytes.data(), level1Bytes.size());
        runFrame(
            [&](IRhiCommandList& list)
            {
                list.barrier(*texture, ResourceState::Undefined, ResourceState::CopyTarget);
                list.copyBufferToTexture(upload, *texture, 0, 0);
                list.copyBufferToTexture(upload, *texture, 1, level0Bytes.size());
                list.barrier(*texture, ResourceState::CopyTarget, ResourceState::ShaderRead);
            });

        const TextureHandle target = makeTarget();
        const BufferHandle readback = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
        renderToReadback(target, readback,
                         [&](IRhiCommandList& list)
                         {
                             list.setPipeline(pipeline);
                             list.setPushConstants({.instance = m_device->bindlessIndex(*sampler),
                                                    .userData = m_device->bindlessIndex(*texture)});
                             list.draw(3);
                         });
        const Pixel center = pixelAt(readback, 32, 32);
        EXPECT_NEAR(center.r, test.expected.r, 2) << test.size << " pixels";
        EXPECT_NEAR(center.g, test.expected.g, 2) << test.size << " pixels";
        m_device->destroy(*texture);
        m_device->destroy(upload);
        m_device->destroy(target);
        m_device->destroy(readback);
    }
    m_device->destroy(pipeline);
    m_device->destroy(*sampler);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, WritesEachMipThroughItsStorageView)
{
    // A 256-pixel, two-level float texture: compute writes red into level 0 and green into level 1, each through
    // its own storage slot. Level 0 is copied back; drawing the texture minified past level 1 shows level 1.
    constexpr std::uint32_t c_size = 256;
    Result<TextureHandle> texture = m_device->createTexture({.width = c_size,
                                                             .height = c_size,
                                                             .format = RhiFormat::Rgba16Float,
                                                             .mipLevels = 2,
                                                             .storage = true,
                                                             .name = "Storage mips"});
    ASSERT_TRUE(texture) << texture.error().message;
    const std::uint32_t level0 = m_device->bindlessStorageIndex(*texture, 0);
    const std::uint32_t level1 = m_device->bindlessStorageIndex(*texture, 1);
    EXPECT_NE(level0, c_invalidBindlessIndex);
    EXPECT_NE(level1, c_invalidBindlessIndex);
    EXPECT_NE(level0, level1) << "one slot per level";
    EXPECT_EQ(m_device->bindlessStorageIndex(*texture, 2), c_invalidBindlessIndex) << "past the last level";

    const std::vector<std::byte> code = bytecode(c_storageShader, "csFill");
    Result<PipelineHandle> fill = m_device->createComputePipeline({.code = code, .entry = "csFill", .name = "Fill"});
    ASSERT_TRUE(fill) << fill.error().message;
    constexpr std::uint32_t c_group = 8; // RhiStorage.hlsl's thread group
    const BufferHandle readback = makeBuffer(std::uint64_t{c_size} * c_size * 8, RhiMemory::Readback);
    runFrame(
        [&](IRhiCommandList& list)
        {
            list.barrier(*texture, ResourceState::Undefined, ResourceState::ShaderWrite);
            list.setPipeline(*fill);
            list.setPushConstants({.instance = 0xFF0000FFU, .userData = level0}); // red
            list.dispatch(c_size / c_group, c_size / c_group);
            list.setPushConstants({.instance = 0xFF00FF00U, .userData = level1}); // green
            list.dispatch(c_size / 2 / c_group, c_size / 2 / c_group);
            list.barrier(*texture, ResourceState::ShaderWrite, ResourceState::CopySource);
            list.copyTextureToBuffer(*texture, readback);
            list.barrier(*texture, ResourceState::CopySource, ResourceState::ShaderRead);
            list.barrier(readback, ResourceState::CopyTarget, ResourceState::HostRead);
        });
    std::array<std::uint16_t, 4> texel{};
    std::memcpy(texel.data(), m_device->mappedMemory(readback).data() + (std::size_t{c_size} * 10 + 10) * 8, 8);
    EXPECT_FLOAT_EQ(Math::fromHalf(texel[0]), 1.0F) << "level 0 is red";
    EXPECT_FLOAT_EQ(Math::fromHalf(texel[1]), 0.0F);

    const Result<SamplerHandle> sampler = m_device->createSampler({.filter = SamplerFilter::Nearest});
    ASSERT_TRUE(sampler) << sampler.error().message;
    const PipelineHandle draw = graphicsPipeline(c_textureShader);
    const TextureHandle target = makeTarget();
    const BufferHandle pixels = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
    renderToReadback(target, pixels,
                     [&](IRhiCommandList& list)
                     {
                         list.setPipeline(draw);
                         list.setPushConstants({.instance = m_device->bindlessIndex(*sampler),
                                                .userData = m_device->bindlessIndex(*texture)});
                         list.draw(3);
                     });
    const Pixel center = pixelAt(pixels, 32, 32);
    EXPECT_EQ(center.r, 0) << "minified past level 1: green";
    EXPECT_EQ(center.g, 255);

    for (const BufferHandle buffer : {readback, pixels})
    {
        m_device->destroy(buffer);
    }
    m_device->destroy(*texture);
    m_device->destroy(*sampler);
    m_device->destroy(*fill);
    m_device->destroy(draw);
    m_device->destroy(target);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, MarkersAndMemoryStats)
{
    const GpuMemoryStats before = m_device->memoryStats();
    const auto tag = static_cast<std::size_t>(Memory::MemTag::Physics);
    const BufferHandle buffer =
        *m_device->createBuffer({.size = 4096, .tag = Memory::MemTag::Physics, .name = "Tagged"});
    const GpuMemoryStats during = m_device->memoryStats();
    EXPECT_GE(during.bytesByTag[tag], before.bytesByTag[tag] + 4096);
    EXPECT_GT(during.usedBytes, 0U);

    runFrame(
        [&](IRhiCommandList& list)
        {
            const ScopedMarker outer{list, "Outer"};
            const ScopedMarker inner{list, "Inner"};
            list.barrier(buffer, ResourceState::Undefined, ResourceState::ShaderWrite);
        });

    m_device->destroy(buffer);
    m_device->waitIdle();
    EXPECT_EQ(m_device->memoryStats().bytesByTag[tag], before.bytesByTag[tag]);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, ListsRecordedOnJobsSubmitInOrder)
{
    constexpr std::uint32_t c_lists = 8;
    m_device.reset();
    Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice({.backend = c_testBackend,
                                                                  .appName = "Meow2 tests",
                                                                  .validation = true,
                                                                  .presentation = false,
                                                                  .recordingThreads = c_lists},
                                                                 m_log);
    ASSERT_TRUE(device) << device.error().message;
    m_device = std::move(*device);

    const PipelineHandle pipeline = graphicsPipeline(c_triangleShader);
    const TextureHandle target = makeTarget();
    const BufferHandle readback = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
    // One full-screen triangle per list, each a different shade of red.
    std::vector<BufferHandle> vertices;
    for (std::uint32_t i = 0; i < c_lists; ++i)
    {
        vertices.push_back(makeBuffer(sizeof(TriangleVertex) * 3, RhiMemory::Upload));
        const float red = static_cast<float>(i + 1) / static_cast<float>(c_lists);
        const std::array<TriangleVertex, 3> triangle{
            TriangleVertex{{-1.0F, 3.0F, 0.5F, 1.0F}, {red, 0.0F, 0.0F, 1.0F}},
            TriangleVertex{{3.0F, -1.0F, 0.5F, 1.0F}, {red, 0.0F, 0.0F, 1.0F}},
            TriangleVertex{{-1.0F, -1.0F, 0.5F, 1.0F}, {red, 0.0F, 0.0F, 1.0F}}};
        std::memcpy(m_device->mappedMemory(vertices[i]).data(), triangle.data(), sizeof(triangle));
    }

    Result<IRhiCommandList*> first = m_device->beginFrame();
    ASSERT_TRUE(first);
    (*first)->barrier(target, ResourceState::Undefined, ResourceState::ColorTarget);

    Jobs::JobSystem jobs{{.threadCount = c_lists}, m_log};
    std::vector<IRhiCommandList*> lists(c_lists);
    Jobs::Job job = jobs.parallelFor(c_lists, 1,
                                     [&](std::uint32_t begin, std::uint32_t end, std::uint32_t threadIndex)
                                     {
                                         for (std::uint32_t i = begin; i < end; ++i)
                                         {
                                             Result<IRhiCommandList*> list = m_device->commandList(threadIndex);
                                             ASSERT_TRUE(list) << list.error().message;
                                             lists[i] = *list;
                                             const ScopedMarker marker{**list, "Job"};
                                             (*list)->beginRendering(RenderTargets{.color = target});
                                             (*list)->setPipeline(pipeline);
                                             Shader::PushConstants push{};
                                             push.userData = m_device->bindlessIndex(vertices[i]);
                                             (*list)->setPushConstants(push);
                                             (*list)->draw(3);
                                             (*list)->endRendering();
                                         }
                                     });
    jobs.wait(job);

    Result<IRhiCommandList*> last = m_device->commandList(0);
    ASSERT_TRUE(last);
    lists.push_back(*last);
    (*last)->barrier(target, ResourceState::ColorTarget, ResourceState::CopySource);
    (*last)->copyTextureToBuffer(target, readback);
    (*last)->barrier(readback, ResourceState::CopyTarget, ResourceState::HostRead);
    ASSERT_TRUE(m_device->endFrame(lists));
    m_device->waitIdle();

    EXPECT_EQ(pixelAt(readback, 32, 32).r, 255) << "the last list in the order, whichever job finished last";

    for (const BufferHandle buffer : vertices)
    {
        m_device->destroy(buffer);
    }
    m_device->destroy(pipeline);
    m_device->destroy(target);
    m_device->destroy(readback);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiDrawTest, PipelineCacheRoundTrips)
{
    const PipelineHandle pipeline = graphicsPipeline(c_triangleShader);
    m_device->destroy(pipeline);
    const std::vector<std::byte> cache = m_device->pipelineCacheData();
    EXPECT_GT(cache.size(), 32U);

    const std::vector<std::byte> foreign(256, std::byte{0xAB});
    for (const std::span<const std::byte> data :
         {std::span<const std::byte>{cache}, std::span<const std::byte>{foreign}})
    {
        m_device.reset(); // one Vulkan device at a time; the same on D3D12, for symmetry
        Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice({.backend = c_testBackend,
                                                                      .appName = "Meow2 tests",
                                                                      .validation = true,
                                                                      .presentation = false,
                                                                      .pipelineCache = data},
                                                                     m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_device = std::move(*device);
        m_device->destroy(graphicsPipeline(c_triangleShader));
        EXPECT_EQ(m_device->validationErrorCount(), 0U);
    }
}

namespace
{
/// @brief A colour for RhiStates.hlsl, packed into PushConstants::userData.
std::uint32_t packColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    return std::uint32_t{r} | (std::uint32_t{g} << 8U) | (std::uint32_t{b} << 16U) | (std::uint32_t{a} << 24U);
}

/// @brief A fixture with the states pipeline.
class RhiStatesTest : public RhiDrawTest
{
protected:
    PipelineHandle makePipeline(BlendMode blend, CullMode cull = CullMode::None)
    {
        const std::vector<std::byte> vertex = bytecode(c_statesShader, "vsMain");
        const std::vector<std::byte> pixel = bytecode(c_statesShader, "psMain");
        Result<PipelineHandle> pipeline = m_device->createGraphicsPipeline({.vertexCode = vertex,
                                                                            .pixelCode = pixel,
                                                                            .colorFormat = RhiFormat::Rgba8Unorm,
                                                                            .cull = cull,
                                                                            .blend = blend,
                                                                            .name = "States"});
        EXPECT_TRUE(pipeline) << pipeline.error().message;
        return *pipeline;
    }

    /// @brief Draws the full-screen triangle into a cleared black target and reads it back.
    BufferHandle drawStates(PipelineHandle pipeline, std::uint32_t color, std::optional<ScissorRect> scissor)
    {
        const TextureHandle target = makeTarget();
        const BufferHandle readback = makeBuffer(sizeof(Pixel) * c_targetSize * c_targetSize, RhiMemory::Readback);
        renderToReadback(target, readback,
                         [&](IRhiCommandList& list)
                         {
                             if (scissor)
                             {
                                 list.setScissor(*scissor);
                             }
                             Shader::PushConstants push{};
                             push.userData = color;
                             list.setPipeline(pipeline);
                             list.setPushConstants(push);
                             list.draw(3);
                         });
        m_device->destroy(target);
        return readback;
    }
};
} // namespace

TEST_F(RhiStatesTest, ScissorClipsDrawing)
{
    const PipelineHandle pipeline = makePipeline(BlendMode::None);
    constexpr std::uint32_t c_half = c_targetSize / 2;
    const BufferHandle readback =
        drawStates(pipeline, packColor(255, 0, 0, 255), ScissorRect{.x = 0, .y = 0, .width = c_half, .height = c_half});

    EXPECT_EQ(pixelAt(readback, 1, 1).r, 255) << "inside the scissor, top left";
    EXPECT_EQ(pixelAt(readback, c_half - 1, c_half - 1).r, 255);
    EXPECT_EQ(pixelAt(readback, c_half + 1, 1).r, 0) << "right of the scissor";
    EXPECT_EQ(pixelAt(readback, 1, c_half + 1).r, 0) << "below the scissor: top-left origin on both backends";
    m_device->destroy(readback);
    m_device->destroy(pipeline);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiStatesTest, AlphaBlendMixesWithTheTarget)
{
    const PipelineHandle pipeline = makePipeline(BlendMode::Alpha);
    // Half-transparent white over the black clear colour: half grey.
    const BufferHandle readback = drawStates(pipeline, packColor(255, 255, 255, 128), std::nullopt);

    const Pixel pixel = pixelAt(readback, c_targetSize / 2, c_targetSize / 2);
    EXPECT_NEAR(pixel.r, 128, 2);
    EXPECT_NEAR(pixel.g, 128, 2);
    m_device->destroy(readback);
    m_device->destroy(pipeline);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiStatesTest, FrontCullingSkipsFrontFaces)
{
    // The full-screen triangle runs clockwise on screen: a front face, as in D3D, on both backends.
    const PipelineHandle front = makePipeline(BlendMode::None, CullMode::Front);
    const BufferHandle culled = drawStates(front, packColor(255, 0, 0, 255), std::nullopt);
    EXPECT_EQ(pixelAt(culled, c_targetSize / 2, c_targetSize / 2).r, 0) << "front face culled";

    const PipelineHandle back = makePipeline(BlendMode::None, CullMode::Back);
    const BufferHandle drawn = drawStates(back, packColor(255, 0, 0, 255), std::nullopt);
    EXPECT_EQ(pixelAt(drawn, c_targetSize / 2, c_targetSize / 2).r, 255) << "back culling keeps it";
    for (const BufferHandle buffer : {culled, drawn})
    {
        m_device->destroy(buffer);
    }
    m_device->destroy(front);
    m_device->destroy(back);
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(RhiStatesTest, WithoutBlendingAlphaOverwrites)
{
    const PipelineHandle pipeline = makePipeline(BlendMode::None);
    const BufferHandle readback = drawStates(pipeline, packColor(255, 255, 255, 128), std::nullopt);

    EXPECT_EQ(pixelAt(readback, c_targetSize / 2, c_targetSize / 2).r, 255);
    m_device->destroy(readback);
    m_device->destroy(pipeline);
}

/// @brief A PipelineLibrary over a private copy of the cooked triangle, so a test can damage it.
class PipelineLibraryTest : public RhiDrawTest
{
protected:
    void SetUp() override
    {
        RhiDrawTest::SetUp();
        m_folder = fs::temp_directory_path() / std::format("meow2-pipelines-{}", c_configName);
        fs::remove_all(m_folder);
        fs::create_directories(m_folder / "shaders");
        const std::string file =
            Assets::cookedShaderPath(assetId(c_triangleShader)).substr(std::string{"cooked/"}.size());
        fs::copy_file(fs::path{MEOW2_COOKED_DIR} / file, m_folder / file);
        ASSERT_TRUE(m_files.unmount("cooked"));
        ASSERT_TRUE(m_files.mountDirectory("cooked", m_folder).has_value());
        // The library builds through a RenderDevice, which takes over the fixture's device.
        m_render = std::make_unique<Renderer::RenderDevice>(
            RhiDeviceDesc{
                .backend = c_testBackend, .appName = "Meow2 tests", .validation = true, .presentation = false},
            m_log, std::move(m_device));
    }

    void TearDown() override
    {
        m_library.reset();
        m_render->device().waitIdle();
    }

    [[nodiscard]] IRhiDevice& device()
    {
        return m_render->device();
    }

    /// @brief Replaces the cooked shader with one that has no bytecode, and reloads it.
    void damageShader()
    {
        const AssetId id = assetId(c_triangleShader);
        const std::string path = Assets::cookedShaderPath(id);
        Result<std::vector<std::byte>> bytes = m_files.readFile(path);
        ASSERT_TRUE(bytes);
        Result<Assets::CookedShader> cooked = Assets::deserializeShader(*bytes);
        ASSERT_TRUE(cooked);
        cooked->blobs.clear();
        const std::vector<std::byte> damaged = Assets::serializeShader(*cooked);
        std::ofstream file{m_folder / path.substr(std::string{"cooked/"}.size()), std::ios::binary};
        file.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
        file.close();
        ASSERT_TRUE(m_assets.reload<Assets::ShaderAsset>(id));
    }

    void makeLibrary()
    {
        m_library.emplace(Renderer::PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
    }

    fs::path m_folder;
    std::unique_ptr<Renderer::RenderDevice> m_render;
    std::optional<Renderer::PipelineLibrary> m_library;
};

TEST_F(PipelineLibraryTest, BuildsFromAShaderAsset)
{
    makeLibrary();
    const Result<Renderer::PipelineId> id = m_library->add(Renderer::GraphicsPipelineRequest{
        .shader = assetId(c_triangleShader), .colorFormat = RhiFormat::Rgba8Unorm, .name = "Triangle"});
    ASSERT_TRUE(id) << id.error().message;
    EXPECT_TRUE(m_library->get(*id).isSet());
    EXPECT_EQ(device().validationErrorCount(), 0U);
}

TEST_F(PipelineLibraryTest, ReloadRebuilds)
{
    makeLibrary();
    const AssetId shader = assetId(c_triangleShader);
    const Result<Renderer::PipelineId> id = m_library->add(
        Renderer::GraphicsPipelineRequest{.shader = shader, .colorFormat = RhiFormat::Rgba8Unorm, .name = "Triangle"});
    ASSERT_TRUE(id) << id.error().message;
    const PipelineHandle before = m_library->get(*id);

    m_events.publish(Assets::ShaderReloaded{shader});

    EXPECT_NE(m_library->get(*id), before);
    EXPECT_EQ(device().validationErrorCount(), 0U);
}

TEST_F(PipelineLibraryTest, FailedRebuildKeepsTheOldPipeline)
{
    makeLibrary();
    const AssetId shader = assetId(c_triangleShader);
    const Result<Renderer::PipelineId> id = m_library->add(
        Renderer::GraphicsPipelineRequest{.shader = shader, .colorFormat = RhiFormat::Rgba8Unorm, .name = "Triangle"});
    ASSERT_TRUE(id) << id.error().message;
    const PipelineHandle before = m_library->get(*id);

    damageShader();
    m_library->rebuild(shader);

    EXPECT_EQ(m_library->get(*id), before);
    EXPECT_EQ(device().validationErrorCount(), 0U);
}

TEST_F(PipelineLibraryTest, UnknownShaderIsAnError)
{
    makeLibrary();
    const Result<Renderer::PipelineId> id = m_library->add(Renderer::GraphicsPipelineRequest{
        .shader = m_ids.next<AssetId::c_kind>(), .colorFormat = RhiFormat::Rgba8Unorm});
    EXPECT_FALSE(id);
}
} // namespace Meow2::Rhi
