/// @file
/// @brief Tests for EditorRenderer on a real GPU: two offscreen viewports of one scene, the render scale, a hidden
///        viewport, picking and the selection outline (label gpu; `MEOW2_GPU_TESTS=ON`).

#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Math/CameraRig.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/ComponentRegistry.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <QByteArray>
#include <string>
#include <vector>

namespace
{
namespace Math = Meow2::Math;
using Meow2::Editor::ViewportCamera;
using Meow2::Editor::ViewportId;

/// @brief A headless EditorRenderer with the test scene, and an editor and a game viewport drawing offscreen.
class EditorRendererTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("cooked", MEOW2_COOKED_DIR).has_value());
        Meow2::Result<std::unique_ptr<Meow2::Editor::EditorRenderer>> renderer = Meow2::Editor::EditorRenderer::create(
            {.assets = m_assets, .events = m_events, .jobs = m_jobs, .log = m_log},
            {.validation = true, .presentation = presents(), .instanceCapacity = 64, .readBackOffscreen = true});
        ASSERT_TRUE(renderer) << renderer.error().message;
        m_renderer = std::move(*renderer);
        ASSERT_TRUE(m_world.buildDefaultScene(&m_renderer->scene()));
        Meow2::Result<ViewportId> editor = m_renderer->addOffscreenViewport(c_size, c_size, "Editor");
        Meow2::Result<ViewportId> game = m_renderer->addOffscreenViewport(c_size, c_size, "Game");
        ASSERT_TRUE(editor && game);
        m_editor = *editor;
        m_game = *game;
    }

    /// @brief Whether the device can present to windows; only tests with a window viewport need it.
    [[nodiscard]] virtual bool presents() const
    {
        return false;
    }

    void TearDown() override
    {
        if (m_renderer)
        {
            m_renderer->device().device().waitIdle();
            EXPECT_EQ(m_renderer->device().device().validationErrorCount(), 0U);
        }
        m_renderer.reset();
    }

    /// @brief One frame: the editor viewport from an orbit around the scene, the game one from its main camera.
    void runFrame()
    {
        m_world.update(&m_renderer->scene(), m_selection);
        m_renderer->setHasSelection(m_world.anySelectedInstance());
        const Math::Sphere bounds = m_world.bounds();
        std::array<std::optional<ViewportCamera>, 2> cameras{};
        cameras.at(m_editor) =
            ViewportCamera{.transform = Math::orbitTransform(Math::frameScene(bounds.center, bounds.radius)),
                           .lens = Math::sceneLens(bounds.radius)};
        const std::optional<entt::entity> main = Meow2::Scene::findMainCamera(m_world.scene());
        ASSERT_TRUE(main);
        cameras.at(m_game) =
            ViewportCamera{.transform = *Meow2::Scene::cameraTransform(m_world.scene(), *main), .lens = {}};
        ASSERT_TRUE(m_renderer->renderFrame(cameras));
        m_renderer->device().device().waitIdle();
    }

    /// @brief How many pixels of a viewport's image aren't black.
    std::uint32_t litPixels(ViewportId viewport)
    {
        const auto pixels = m_renderer->offscreenTarget(viewport)->pixels();
        std::uint32_t lit = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            lit += (static_cast<int>(pixels[i]) + static_cast<int>(pixels[i + 1]) + static_cast<int>(pixels[i + 2])) > 0
                       ? 1U
                       : 0U;
        }
        return lit;
    }

    static constexpr std::uint32_t c_size = 64;
    Meow2::Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = true}};
    Meow2::Log::Logger m_log{m_logSystem.createLogger("RHI")};
    Meow2::Events::EventBus m_events{Meow2::Memory::globalTracker()};
    Meow2::FileSystem::FileSystem m_files{m_log};
    Meow2::UuidGenerator m_ids;
    Meow2::Assets::AssetManager m_assets{m_files, m_ids, m_log};
    Meow2::Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    /// @brief The engine's and the renderer's components.
    Meow2::Scene::ComponentRegistry m_components = []
    {
        Meow2::Scene::ComponentRegistry registry;
        Meow2::Scene::registerSceneComponents(registry);
        Meow2::Renderer::registerRenderComponents(registry);
        return registry;
    }();
    Meow2::Editor::EditorWorld m_world{
        {.ids = m_ids, .jobs = m_jobs, .components = m_components, .assets = m_assets, .log = m_log}};
    std::unique_ptr<Meow2::Editor::EditorRenderer> m_renderer;
    ViewportId m_editor = 0;
    ViewportId m_game = 0;
    std::vector<Meow2::EntityId> m_selection; ///< Outlined (in viewports with setOutline()).
};

/// @brief The same, on a device that can present: for window viewports.
class EditorRendererWindowTest : public EditorRendererTest
{
protected:
    [[nodiscard]] bool presents() const override
    {
        return true;
    }
};
} // namespace

TEST_F(EditorRendererTest, TwoViewportsAndRenderScale)
{
    runFrame();
    EXPECT_TRUE(m_renderer->wasDrawn(m_editor));
    EXPECT_TRUE(m_renderer->wasDrawn(m_game));
    EXPECT_GT(litPixels(m_editor), c_size * c_size / 2) << "the scene and the sky fill the editor view";
    EXPECT_GT(litPixels(m_game), c_size * c_size / 2) << "and the game view, through its main camera";
    EXPECT_EQ(m_renderer->sceneSize(m_game), (std::pair{c_size, c_size}));

    // At 50 % the game's scene is drawn at half size, then scaled up to fill the whole viewport.
    m_renderer->setRenderScale(m_game, 0.5F);
    runFrame();
    EXPECT_EQ(m_renderer->sceneSize(m_game), (std::pair{c_size / 2, c_size / 2}));
    EXPECT_GT(litPixels(m_game), c_size * c_size / 2) << "still fills the viewport";
    EXPECT_EQ(m_renderer->sceneSize(m_editor), (std::pair{c_size, c_size})) << "the editor view keeps full size";

    // A hidden viewport isn't drawn at all.
    m_renderer->setVisible(m_editor, false);
    runFrame();
    EXPECT_FALSE(m_renderer->wasDrawn(m_editor));
    EXPECT_TRUE(m_renderer->wasDrawn(m_game));
}

// #216: a window viewport whose size changed was resized inside the frame, which the RHI forbids (an assert).
TEST_F(EditorRendererWindowTest, WindowViewportResizes)
{
    Meow2::Result<Meow2::Platform::Platform> platform = Meow2::Platform::Platform::create({}, m_log, m_events);
    ASSERT_TRUE(platform) << platform.error().message;
    // Unfocused and on a secondary display when there is one, so it doesn't get in the way of whoever uses the PC.
    Meow2::Platform::WindowDesc desc{.title = "EditorRendererTests", .width = 160, .height = 120, .focusable = false};
    for (const Meow2::Platform::DisplayInfo& display : platform->displays())
    {
        if (!display.primary)
        {
            desc.x = display.usable.x + 32;
            desc.y = display.usable.y + 32;
            break;
        }
    }
    Meow2::Result<Meow2::Platform::Window> window = Meow2::Platform::Window::create(*platform, m_log, desc);
    ASSERT_TRUE(window) << window.error().message;
    const auto pixels = [&]
    {
        return std::pair{static_cast<std::uint32_t>(window->pixelWidth()),
                         static_cast<std::uint32_t>(window->pixelHeight())};
    };
    const auto [width, height] = pixels();
    Meow2::Result<ViewportId> viewport = m_renderer->addWindowViewport(window->nativeHandle(), width, height, "Window");
    ASSERT_TRUE(viewport) << viewport.error().message;

    const auto drawFrame = [&]
    {
        m_world.update(&m_renderer->scene());
        const Math::Sphere bounds = m_world.bounds();
        std::array<std::optional<ViewportCamera>, 3> cameras{};
        cameras.at(*viewport) =
            ViewportCamera{.transform = Math::orbitTransform(Math::frameScene(bounds.center, bounds.radius)),
                           .lens = Math::sceneLens(bounds.radius)};
        const Meow2::Result<void> drawn = m_renderer->renderFrame(cameras);
        ASSERT_TRUE(drawn) << drawn.error().message;
    };
    drawFrame();
    EXPECT_TRUE(m_renderer->wasDrawn(*viewport));

    // The window grows; the editor tells the renderer the viewport's new size, and the next frame resizes its
    // swapchain before recording.
    window->setSize(240, 180);
    platform->pumpEvents();
    const std::pair<std::uint32_t, std::uint32_t> grown = pixels();
    m_renderer->resize(*viewport, grown.first, grown.second);
    drawFrame();
    EXPECT_TRUE(m_renderer->wasDrawn(*viewport));
    EXPECT_EQ(m_renderer->sceneSize(*viewport), grown);

    m_renderer->device().device().waitIdle();
    EXPECT_EQ(m_renderer->device().device().validationErrorCount(), 0U);
    m_renderer.reset(); // its swapchain goes before the window
}

// #222: a scene with a model, saved and opened again, is drawn from a fresh GPU scene through the model's stable mesh
// IDs.
TEST_F(EditorRendererTest, OpensASavedScene)
{
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / std::format("meow2-editor-scene-{}", Meow2::c_configName);
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::filesystem::create_directories(folder);
    ASSERT_TRUE(m_files.mountDirectory("project", folder, Meow2::FileSystem::MountAccess::ReadWrite).has_value());
    const std::array<float, 9> positions{0.0F, 0.0F, 3.0F, 1.0F, 0.0F, 3.0F, 0.0F, 1.0F, 3.0F};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    QByteArray buffer(42, '\0');
    std::memcpy(buffer.data(), positions.data(), sizeof(positions));
    std::memcpy(buffer.data() + sizeof(positions), indices.data(), sizeof(indices));
    ASSERT_TRUE(m_files
                    .writeText("project/Triangle.gltf", std::format(R"({{"asset": {{"version": "2.0"}},
  "buffers": [{{"byteLength": 42, "uri": "data:application/octet-stream;base64,{}"}}],
  "bufferViews": [{{"buffer": 0, "byteOffset": 0, "byteLength": 36}}, {{"buffer": 0, "byteOffset": 36, "byteLength": 6}}],
  "accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 3], "max": [1, 1, 3]}},
                {{"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}}],
  "meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0}}, "indices": 1}}]}}],
  "nodes": [{{"name": "Child", "mesh": 0}}], "scenes": [{{"nodes": [0]}}], "scene": 0}})",
                                                                    buffer.toBase64().toStdString()))
                    .has_value());

    // A new scene with the model in it, saved.
    m_world.newScene([this] { return m_renderer->resetScene() ? &m_renderer->scene() : nullptr; });
    ASSERT_TRUE(m_world.importModel("project/Triangle.gltf", &m_renderer->scene()));
    ASSERT_TRUE(m_world.saveScene("project/Main.m2scene"));

    // Opened again: an empty GPU scene, filled with the model the scene's mesh IDs point to.
    ASSERT_TRUE(m_world.openScene("project/Main.m2scene",
                                  [this] { return m_renderer->resetScene() ? &m_renderer->scene() : nullptr; }));
    EXPECT_EQ(m_renderer->scene().meshCount(), 1U) << "only the opened scene's model";
    runFrame();
    EXPECT_EQ(m_renderer->scene().instanceCount(), 1U) << "the triangle is drawn";
    EXPECT_TRUE(m_renderer->wasDrawn(m_editor));
    EXPECT_TRUE(m_renderer->wasDrawn(m_game)) << "both viewports have a view in the new GPU scene";
    std::filesystem::remove_all(folder, ignored);
}

namespace
{
/// @brief The name of the entity a picked instance was drawn for, or "" for none.
std::string nameOf(Meow2::Editor::EditorWorld& world, std::uint32_t instance)
{
    const entt::entity entity = world.instanceEntity(instance);
    if (entity == entt::null || !world.scene().registry().valid(entity))
    {
        return {};
    }
    const auto* name = world.scene().registry().try_get<Meow2::Scene::NameComponent>(entity);
    return name != nullptr ? name->name : std::string{};
}
} // namespace

// #226: a pick at a pixel lists every surface under it (and a couple of pixels around), what covers the pixel and is
// nearest first; the readback comes a frame or two later.
TEST_F(EditorRendererTest, PicksWhatIsUnderTheCursor)
{
    const auto pick = [this](std::uint32_t x, std::uint32_t y)
    {
        m_renderer->requestPick(m_editor, x, y);
        EXPECT_TRUE(m_renderer->pickPending());
        std::optional<Meow2::Editor::PickResult> result;
        for (int frame = 0; frame < 4 && !result; ++frame)
        {
            runFrame();
            result = m_renderer->takePickResult();
        }
        EXPECT_FALSE(m_renderer->pickPending());
        return result;
    };

    // The editor view frames the scene around the origin, looking a little down: the middle cube is in the centre,
    // with the ground behind it.
    runFrame();
    const std::optional<Meow2::Editor::PickResult> centre = pick(c_size / 2, c_size / 2);
    ASSERT_TRUE(centre) << "the result came back";
    EXPECT_EQ(centre->viewport, m_editor);
    EXPECT_EQ(centre->x, c_size / 2);
    ASSERT_GE(centre->hits.size(), 2U) << "the cube and the ground behind it";
    EXPECT_EQ(nameOf(m_world, centre->hits[0].instance), "Cube") << "nearest first";
    EXPECT_EQ(centre->hits[0].distance, 0U) << "it covers the cursor's pixel";
    const entt::entity cube = m_world.instanceEntity(centre->hits[0].instance);
    EXPECT_NEAR(m_world.scene().registry().get<Meow2::Scene::LocalTransform>(cube).value.position.x, 0.0F, 1e-4F)
        << "the middle one";
    bool ground = false;
    for (std::size_t i = 1; i < centre->hits.size(); ++i)
    {
        const Meow2::Editor::PickHit& before = centre->hits[i - 1];
        const Meow2::Editor::PickHit& hit = centre->hits[i];
        EXPECT_TRUE(before.distance < hit.distance || (before.distance == hit.distance && before.depth >= hit.depth))
            << "what covers the cursor first, then nearest (reversed Z)";
        EXPECT_NE(centre->hits[i].instance, centre->hits[i - 1].instance) << "each instance once";
        ground = ground || nameOf(m_world, centre->hits[i].instance) == "Ground";
    }
    EXPECT_TRUE(ground);

    // The top row is sky: nothing there.
    const std::optional<Meow2::Editor::PickResult> sky = pick(c_size / 2, 1);
    ASSERT_TRUE(sky);
    EXPECT_TRUE(sky->hits.empty());
}

// #226: a selected instance gets an outline in a viewport with the outline on (the editor's), and nowhere else.
TEST_F(EditorRendererTest, OutlinesTheSelection)
{
    m_renderer->setOutline(m_editor, true);
    runFrame();
    runFrame(); // settled: the shadow caches and the sky are made by the first frame
    const auto copy = [this](ViewportId viewport)
    {
        const auto pixels = m_renderer->offscreenTarget(viewport)->pixels();
        return std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(pixels.data()),
                                         reinterpret_cast<const std::uint8_t*>(pixels.data()) + pixels.size());
    };
    const std::vector<std::uint8_t> editorBefore = copy(m_editor);
    const std::vector<std::uint8_t> gameBefore = copy(m_game);

    // Select the middle cube.
    for (const auto [entity, local] : m_world.scene().registry().view<Meow2::Scene::LocalTransform>().each())
    {
        const auto* name = m_world.scene().registry().try_get<Meow2::Scene::NameComponent>(entity);
        if (name != nullptr && name->name == "Cube" && local.value.position.x == 0.0F)
        {
            m_selection.push_back(m_world.scene().registry().get<Meow2::Scene::IdComponent>(entity).id);
        }
    }
    ASSERT_EQ(m_selection.size(), 1U);
    runFrame();
    EXPECT_TRUE(m_world.anySelectedInstance());
    const std::vector<std::uint8_t> editorAfter = copy(m_editor);
    const std::vector<std::uint8_t> gameAfter = copy(m_game);

    // Pixels that changed in the editor view are the outline: orange (red above green above blue).
    std::uint32_t changed = 0;
    std::uint32_t orange = 0;
    for (std::size_t i = 0; i + 3 < editorAfter.size(); i += 4)
    {
        if (editorAfter[i] != editorBefore[i] || editorAfter[i + 1] != editorBefore[i + 1] ||
            editorAfter[i + 2] != editorBefore[i + 2])
        {
            ++changed;
            orange += (editorAfter[i] > editorAfter[i + 1] && editorAfter[i + 1] > editorAfter[i + 2]) ? 1U : 0U;
        }
    }
    EXPECT_GT(changed, 8U) << "an edge around the cube";
    EXPECT_LT(changed, c_size * c_size / 4) << "an outline, not the whole view";
    EXPECT_EQ(orange, changed);
    EXPECT_EQ(gameAfter, gameBefore) << "the game view has no outline";

    // Nothing selected: the outline goes.
    m_selection.clear();
    runFrame();
    EXPECT_FALSE(m_world.anySelectedInstance());
    EXPECT_EQ(copy(m_editor), editorBefore);
}
