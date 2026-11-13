/// @file
/// @brief Golden images (techstack.md §13): each suggested Khronos sample is drawn headlessly on each backend, the way
///        Meow2Renderer first shows it, and compared with an owner-approved PNG in `tests/visual/goldens/`.
///
/// Needs a GPU (`MEOW2_GPU_TESTS=ON`) and the samples (`cmake -P cmake/FetchGltfSample.cmake <Model>`); a sample that
/// isn't downloaded is skipped. With `MEOW2_UPDATE_GOLDENS=1` in the environment, the images are written into the
/// goldens folder instead of compared: new goldens are only committed after the owner has looked at them.

#include "CameraControls.h"
#include "RendererUi.h"
#include "SceneView.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Texture/Image.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Graphics/Renderer/PipelineLibrary.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Renderer/RenderGraph.h>
#include <Meow2/Graphics/Renderer/SkyAtmosphere.h>
#include <Meow2/Graphics/Renderer/ToneMapPass.h>
#include <Meow2/Math/Camera.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::RendererApp
{
namespace
{
namespace fs = std::filesystem;

/// @brief Width and height of every golden image, in pixels (owner decision 2026-11-13).
constexpr std::uint32_t c_size = 512;

/// @brief Frames drawn before the one that's kept: the first uploads textures and builds the sky's tables.
constexpr int c_frames = 3;

/// @brief A pixel differs when one of its colour channels is further than this from the golden's (of 255): enough for
///        a driver update's rounding, far less than any visible change.
constexpr int c_channelTolerance = 8;

/// @brief The share of pixels that may differ before the test fails: 0.1 %, about 260 of 512 × 512. Edges of thin
///        geometry can flip between neighbouring pixels after a driver update; a real regression moves far more.
constexpr double c_pixelTolerance = 0.001;

/// @brief The suggested scenes of milestones.md M6, in its order.
constexpr std::array<std::string_view, 25> c_scenes{"Box",
                                                    "BoxTextured",
                                                    "Duck",
                                                    "Avocado",
                                                    "OrientationTest",
                                                    "NegativeScaleTest",
                                                    "TextureCoordinateTest",
                                                    "MetalRoughSpheres",
                                                    "MetalRoughSpheresNoTextures",
                                                    "EnvironmentTest",
                                                    "NormalTangentTest",
                                                    "NormalTangentMirrorTest",
                                                    "TextureSettingsTest",
                                                    "TextureTransformTest",
                                                    "VertexColorTest",
                                                    "AlphaBlendModeTest",
                                                    "DamagedHelmet",
                                                    "FlightHelmet",
                                                    "WaterBottle",
                                                    "BoomBox",
                                                    "Lantern",
                                                    "Corset",
                                                    "SciFiHelmet",
                                                    "Sponza",
                                                    "ABeautifulGame"};

/// @brief A camera for a golden where the Renderer's first view would show little. Plain data.
struct FixedCamera
{
    std::string_view scene{}; ///< The sample.
    Math::Vec3 position{};    ///< Where the camera is, in the engine's space (glTF's X is negated on import).
    Math::Vec3 forward{};     ///< Which way it looks; any length.
};

/// @brief Sponza is an interior: from outside, as the front orbit sees it, only its outer wall shows. This looks
///        down the long hall from near one end at eye height, like Khronos' screenshot.
constexpr std::array<FixedCamera, 1> c_fixedCameras{
    {{.scene = "Sponza", .position = {11.0F, 1.6F, -0.3F}, .forward = {-1.0F, 0.12F, 0.0F}}}};

/// @brief One golden: a scene on a backend. Plain data.
struct GoldenCase
{
    std::string_view scene{};                          ///< The sample's name.
    Rhi::RhiBackend backend = Rhi::RhiBackend::Vulkan; ///< The API it's drawn with.
};

/// @brief The name of a backend in file and test names.
std::string_view backendName(Rhi::RhiBackend backend)
{
    return backend == Rhi::RhiBackend::D3D12 ? "d3d12" : "vulkan";
}

/// @brief Prints a case as `Scene_backend`. CTest's test discovery names parameterised tests after the printed value,
///        so this keeps the names readable and the same from build to build.
void PrintTo(const GoldenCase& golden, std::ostream* out)
{
    *out << golden.scene << '_' << backendName(golden.backend);
}

/// @brief Every scene on every backend this build has.
std::vector<GoldenCase> allCases()
{
    std::vector<GoldenCase> cases;
    for (const std::string_view scene : c_scenes)
    {
        cases.push_back({.scene = scene, .backend = Rhi::RhiBackend::Vulkan});
#if MEOW2_RHI_D3D12
        cases.push_back({.scene = scene, .backend = Rhi::RhiBackend::D3D12});
#endif
    }
    return cases;
}

/// @brief Whether MEOW2_UPDATE_GOLDENS=1 is in the environment: write goldens instead of comparing.
bool updateRequested()
{
#if MEOW2_PLATFORM_WINDOWS
    char* value = nullptr; // MSVC deprecates getenv(); _dupenv_s copies the value
    std::size_t length = 0;
    const bool set =
        _dupenv_s(&value, &length, "MEOW2_UPDATE_GOLDENS") == 0 && value != nullptr && std::string_view{value} == "1";
    std::free(value);
    return set;
#else
    const char* value = std::getenv("MEOW2_UPDATE_GOLDENS");
    return value != nullptr && std::string_view{value} == "1";
#endif
}

/// @brief Reads a whole file.
/// @return Its bytes; empty if it can't be read.
std::vector<std::byte> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return {};
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(bytes.size());
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

/// @brief Writes an image as a PNG, creating the folder.
/// @return False if it couldn't.
bool writePng(const fs::path& path, const Assets::Image& image)
{
    const Result<std::vector<std::byte>> png = Assets::encodePng(image);
    if (!png)
    {
        return false;
    }
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    return static_cast<bool>(file);
}

/// @brief How two images of the same size differ. Plain data.
struct Comparison
{
    std::size_t differing = 0;  ///< Pixels with a colour channel further apart than c_channelTolerance.
    int largest = 0;            ///< The largest channel difference anywhere.
    Assets::Image difference{}; ///< Black where they match, the difference ×4 elsewhere (red where it's over).
};

Comparison compare(const Assets::Image& actual, const Assets::Image& golden)
{
    Comparison result;
    result.difference = {.width = actual.width, .height = actual.height, .pixels = {}};
    result.difference.pixels.resize(actual.pixels.size());
    for (std::size_t i = 0; i + 3 < actual.pixels.size(); i += 4)
    {
        int worst = 0;
        for (std::size_t c = 0; c < 3; ++c)
        {
            const int delta =
                std::abs(std::to_integer<int>(actual.pixels[i + c]) - std::to_integer<int>(golden.pixels[i + c]));
            worst = std::max(worst, delta);
            result.difference.pixels[i + c] = static_cast<std::byte>(std::min(delta * 4, 255));
        }
        result.largest = std::max(result.largest, worst);
        if (worst > c_channelTolerance)
        {
            ++result.differing;
            result.difference.pixels[i] = std::byte{255};
        }
        result.difference.pixels[i + 3] = std::byte{255};
    }
    return result;
}

/// @brief A headless Meow2Renderer: the device, the sky, the tone curve and one scene, drawn at c_size × c_size.
class GoldenImageTest : public ::testing::TestWithParam<GoldenCase>
{
protected:
    GoldenImageTest()
        : m_logSystem({.appName = "Test", .directory = {}, .console = true})
        , m_log(m_logSystem.createLogger("Renderer"))
        , m_files(m_log)
        , m_assets(m_files, m_ids, m_log)
        , m_jobs({.threadCount = 4}, m_log)
    {
    }

    void TearDown() override
    {
        if (!m_render)
        {
            return;
        }
        m_render->device().waitIdle();
        m_view.reset();
        m_toneMap.reset();
        m_sky.reset();
        m_pipelines.reset();
        for (const Rhi::TextureHandle texture : {m_hdr, m_depth, m_target})
        {
            m_render->device().destroy(texture);
        }
        m_render->device().destroy(m_readback);
        EXPECT_EQ(m_render->device().validationErrorCount(), 0U);
    }

    static AssetId assetId(std::string_view text)
    {
        return Uuid::parse(text).and_then([](const Uuid& uuid) { return AssetId::fromUuid(uuid); }).value_or(AssetId{});
    }

    /// @brief Creates everything Meow2Renderer has for a scene, on a backend, and opens the scene.
    void open(Rhi::RhiBackend backend, const fs::path& file)
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Result<std::unique_ptr<Renderer::RenderDevice>> device =
            Renderer::RenderDevice::create({.backend = backend,
                                            .appName = "Meow2 golden images",
                                            .validation = true,
                                            .presentation = false,
                                            .recordingThreads = 4},
                                           m_log);
        ASSERT_TRUE(device) << device.error().message;
        m_render = std::move(*device);
        Rhi::IRhiDevice& rhi = m_render->device();
        m_pipelines = std::make_unique<Renderer::PipelineLibrary>(
            Renderer::PipelineLibraryDeps{*m_render, m_assets, m_events, m_log});
        Result<std::unique_ptr<Renderer::SkyAtmosphere>> sky = Renderer::SkyAtmosphere::create(
            *m_render, *m_pipelines,
            {.shader = assetId("01a1b5e2-3c16-8000-80a4-7b19d2e6c057"), .colorFormat = c_hdrFormat});
        ASSERT_TRUE(sky) << sky.error().message;
        m_sky = std::move(*sky);
        Result<std::unique_ptr<Renderer::ToneMapPass>> toneMap = Renderer::ToneMapPass::create(
            *m_pipelines, assetId("01a1b5e2-3c17-8000-80a4-7b19d2e6c058"), Rhi::RhiFormat::Rgba8Srgb);
        ASSERT_TRUE(toneMap) << toneMap.error().message;
        m_toneMap = std::move(*toneMap);

        // RGBA8 textures (no BC7): the same pixels on every machine, and no slow encoder in Debug.
        Result<std::unique_ptr<SceneView>> view =
            SceneView::openModel({.device = *m_render,
                                  .pipelines = *m_pipelines,
                                  .files = m_files,
                                  .ids = m_ids,
                                  .jobs = m_jobs,
                                  .log = m_log,
                                  .shader = assetId("01a1b5e2-3c12-8000-80a4-7b19d2e6c053"),
                                  .colorFormat = c_hdrFormat,
                                  .sky = m_sky.get(),
                                  .textures = {.compress = false, .cache = nullptr, .jobs = &m_jobs}},
                                 file);
        ASSERT_TRUE(view) << view.error().message;
        m_view = std::move(*view);

        const auto texture = [&rhi](Rhi::RhiFormat format, const char* name) -> Rhi::TextureHandle
        {
            Result<Rhi::TextureHandle> made = rhi.createTexture(
                {.width = c_size, .height = c_size, .format = format, .renderTarget = true, .name = name});
            EXPECT_TRUE(made) << made.error().message;
            return made.value_or(Rhi::TextureHandle{});
        };
        m_hdr = texture(c_hdrFormat, "Scene colour");
        m_depth = texture(Rhi::RhiFormat::D32Float, "Depth");
        m_target = texture(Rhi::RhiFormat::Rgba8Srgb, "Golden");
        Result<Rhi::BufferHandle> readback = rhi.createBuffer({.size = std::uint64_t{c_size} * c_size * 4,
                                                               .memory = Rhi::RhiMemory::Readback,
                                                               .name = "Golden readback"});
        ASSERT_TRUE(readback) << readback.error().message;
        m_readback = *readback;
    }

    /// @brief Draws the scene as Meow2Renderer first shows it: the file's first camera if it has one, else the front
    ///        orbit (or c_fixedCameras); the default sun; the sky; the tone curve. Keeps the last of c_frames frames.
    /// @param scene The sample's name, for c_fixedCameras.
    Assets::Image draw(std::string_view scene)
    {
        Rhi::IRhiDevice& rhi = m_render->device();
        Math::CameraLens lens = sceneLens(m_view->radius());
        Math::Transform camera = orbitTransform(frameScene(m_view->center(), m_view->radius()));
        if (!m_view->cameras().empty())
        {
            const SceneCameraEntry& entry = m_view->cameras().front();
            camera = sceneCameraTransform(m_view->nodeWorld(entry.node));
            lens = sceneCameraLens(entry.lens, m_view->radius());
        }
        for (const FixedCamera& fixed : c_fixedCameras)
        {
            if (fixed.scene == scene)
            {
                camera.position = fixed.position;
                camera.rotation = Math::lookRotation(fixed.forward);
                lens = Math::CameraLens{};
            }
        }
        const Math::CameraMatrices matrices = Math::cameraMatrices(camera, lens, 1.0F);
        const SunControls sun{};
        const Math::Vec3 sunDirection = RendererApp::sunDirection(sun);
        Rhi::ResourceState targetState = Rhi::ResourceState::Undefined;

        for (int frameIndex = 0; frameIndex < c_frames; ++frameIndex)
        {
            Result<Rhi::IRhiCommandList*> first = rhi.beginFrame();
            EXPECT_TRUE(first);
            const std::uint32_t slot = rhi.frameSlot();
            Shader::FrameConstants frame{};
            frame.view = matrices.view;
            frame.projection = matrices.projection;
            frame.viewProjection = matrices.viewProjection;
            frame.cameraPosition = {camera.position.x, camera.position.y, camera.position.z, 0.0F};
            m_view->gpu().setLighting(
                {.sunDirection = sunDirection, .sunIntensity = sun.intensity, .exposure = sun.exposure});

            Renderer::RenderGraph graph;
            m_sky->setView(slot,
                           {.sunDirection = sunDirection,
                            .sunIlluminance = {sun.intensity, sun.intensity, sun.intensity, 1.0F},
                            .cameraAltitudeKm = 0.05F + std::max(camera.position.y, 0.0F) * 0.001F},
                           matrices.viewProjection);
            m_sky->addPasses(graph, slot);
            m_view->gpu().setView(slot, frame, Math::frustumFromCamera(matrices));
            const Renderer::GraphResource hdr =
                m_view->gpu().addPasses(graph, slot, nullptr, m_hdr, m_depth, c_sceneClearColor);
            const Renderer::GraphResource target = graph.import(m_target, targetState, "Golden");
            m_toneMap->addPass(graph, hdr, m_hdr, target, m_target, rhi);
            graph.compile();
            Result<std::vector<Rhi::IRhiCommandList*>> lists = graph.execute(rhi, m_jobs);
            EXPECT_TRUE(lists) << lists.error().message;
            m_view->gpu().endFrame(graph);
            m_sky->endFrame();
            targetState = graph.finalState(target);

            if (frameIndex == c_frames - 1)
            {
                Result<Rhi::IRhiCommandList*> last = rhi.commandList(0);
                EXPECT_TRUE(last);
                lists->push_back(*last);
                (*last)->barrier(m_target, targetState, Rhi::ResourceState::CopySource);
                (*last)->copyTextureToBuffer(m_target, m_readback);
                (*last)->barrier(m_readback, Rhi::ResourceState::CopyTarget, Rhi::ResourceState::HostRead);
            }
            EXPECT_TRUE(rhi.endFrame(*lists));
        }
        rhi.waitIdle();

        Assets::Image image{.width = c_size, .height = c_size, .pixels = {}};
        const std::span<std::byte> memory = rhi.mappedMemory(m_readback);
        image.pixels.assign(memory.begin(),
                            memory.begin() + static_cast<std::ptrdiff_t>(std::size_t{c_size} * c_size * 4));
        return image;
    }

    static constexpr Rhi::RhiFormat c_hdrFormat = Rhi::RhiFormat::Rgba16Float; ///< Meow2Renderer's scene colour.

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Events::EventBus m_events{Memory::globalTracker()};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    Assets::AssetManager m_assets;
    Jobs::JobSystem m_jobs;
    std::unique_ptr<Renderer::RenderDevice> m_render;
    std::unique_ptr<Renderer::PipelineLibrary> m_pipelines;
    std::unique_ptr<Renderer::SkyAtmosphere> m_sky; ///< Before the view, which refers to it.
    std::unique_ptr<Renderer::ToneMapPass> m_toneMap;
    std::unique_ptr<SceneView> m_view;
    Rhi::TextureHandle m_hdr;
    Rhi::TextureHandle m_depth;
    Rhi::TextureHandle m_target;
    Rhi::BufferHandle m_readback;
};
} // namespace

TEST_P(GoldenImageTest, MatchesGolden)
{
    const GoldenCase& golden = GetParam();
    const fs::path file = findSample(MEOW2_SAMPLE_ASSETS_DIR, std::string{golden.scene});
    if (file.empty())
    {
        GTEST_SKIP() << golden.scene << " isn't downloaded: cmake -P cmake/FetchGltfSample.cmake " << golden.scene;
    }
    open(golden.backend, file);
    if (HasFatalFailure())
    {
        return;
    }
    const Assets::Image actual = draw(golden.scene);
    const std::string name = std::format("{}-{}.png", golden.scene, backendName(golden.backend));
    const fs::path goldenPath = fs::path{MEOW2_GOLDEN_DIR} / name;
    const fs::path outputDir = fs::path{MEOW2_VISUAL_OUTPUT_DIR};

    if (updateRequested())
    {
        ASSERT_TRUE(writePng(goldenPath, actual)) << "can't write " << goldenPath.string();
        std::printf("Wrote %s: commit it only after the owner has looked at it\n", goldenPath.string().c_str());
        return;
    }

    const std::vector<std::byte> goldenFile = readFile(goldenPath);
    if (goldenFile.empty())
    {
        const fs::path candidate = outputDir / name;
        EXPECT_TRUE(writePng(candidate, actual));
        FAIL() << "No golden " << goldenPath.string() << "; this run's image is " << candidate.string()
               << " (MEOW2_UPDATE_GOLDENS=1 writes it into the goldens folder for review)";
    }
    const Result<Assets::Image> expected = Assets::decodeImage(goldenFile);
    ASSERT_TRUE(expected) << expected.error().message;
    ASSERT_EQ(expected->width, actual.width);
    ASSERT_EQ(expected->height, actual.height);

    const Comparison result = compare(actual, *expected);
    const auto allowed = static_cast<std::size_t>(static_cast<double>(std::size_t{c_size} * c_size) * c_pixelTolerance);
    if (result.differing > allowed)
    {
        // What it drew and where it differs, next to the build, for a look.
        const fs::path stem = outputDir / std::format("{}-{}", golden.scene, backendName(golden.backend));
        EXPECT_TRUE(writePng(stem.string() + ".png", actual));
        EXPECT_TRUE(writePng(stem.string() + "-diff.png", result.difference));
        ADD_FAILURE() << result.differing << " pixels differ by more than " << c_channelTolerance << " (allowed "
                      << allowed << "; largest difference " << result.largest << "). See " << stem.string()
                      << ".png and -diff.png";
    }
}

INSTANTIATE_TEST_SUITE_P(Samples, GoldenImageTest, ::testing::ValuesIn(allCases()),
                         [](const ::testing::TestParamInfo<GoldenCase>& info)
                         { return std::format("{}_{}", info.param.scene, backendName(info.param.backend)); });
} // namespace Meow2::RendererApp
