/// @file
/// @brief RHI tests that run real shaders on a real GPU with no window: resources, the heap, pipelines and the pipeline
///        cache (`MEOW2_GPU_TESTS=ON`). Results are read back and checked pixel by pixel.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderHotReload.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Math/Color.h>
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
        Result<std::unique_ptr<IRhiDevice>> device =
            createRhiDevice({.appName = "Meow2 tests", .validation = true, .presentation = false}, m_log);
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
        const Result<std::span<const std::byte>> code = asset->bytecode(*key, entry, Shader::ShaderTarget::Spirv);
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
            list.setPushConstants(
                {.instanceBuffer = m_device->bindlessIndex(instances), .userData = m_device->bindlessIndex(output)});
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
    Result<SamplerHandle> sampler =
        m_device->createSampler({.filter = SamplerFilter::Nearest, .address = SamplerAddress::Clamp});
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
    Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice(
        {.appName = "Meow2 tests", .validation = true, .presentation = false, .recordingThreads = c_lists}, m_log);
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
        m_device.reset(); // one Vulkan device at a time
        Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice(
            {.appName = "Meow2 tests", .validation = true, .presentation = false, .pipelineCache = data}, m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_device = std::move(*device);
        m_device->destroy(graphicsPipeline(c_triangleShader));
        EXPECT_EQ(m_device->validationErrorCount(), 0U);
    }
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
    }

    void TearDown() override
    {
        m_library.reset();
        m_device->waitIdle();
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
        m_library.emplace(Renderer::PipelineLibraryDeps{m_device.operator*(), m_assets, m_events, m_log});
    }

    fs::path m_folder;
    std::optional<Renderer::PipelineLibrary> m_library;
};

TEST_F(PipelineLibraryTest, BuildsFromAShaderAsset)
{
    makeLibrary();
    const Result<Renderer::PipelineId> id = m_library->add(Renderer::GraphicsPipelineRequest{
        .shader = assetId(c_triangleShader), .colorFormat = RhiFormat::Rgba8Unorm, .name = "Triangle"});
    ASSERT_TRUE(id) << id.error().message;
    EXPECT_TRUE(m_library->get(*id).isSet());
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
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
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
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
    EXPECT_EQ(m_device->validationErrorCount(), 0U);
}

TEST_F(PipelineLibraryTest, UnknownShaderIsAnError)
{
    makeLibrary();
    const Result<Renderer::PipelineId> id = m_library->add(Renderer::GraphicsPipelineRequest{
        .shader = m_ids.next<AssetId::c_kind>(), .colorFormat = RhiFormat::Rgba8Unorm});
    EXPECT_FALSE(id);
}
} // namespace Meow2::Rhi
