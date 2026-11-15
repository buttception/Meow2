/// @file
/// @brief Tests for EditorRenderer on a real GPU: two offscreen viewports of one scene, the render scale and a hidden
///        viewport (label gpu; `MEOW2_GPU_TESTS=ON`).

#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Math/CameraRig.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Platform/Platform.h>
#include <Meow2/Platform/Window/Window.h>
#include <Meow2/Scene/Camera.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

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
        m_world.update(&m_renderer->scene());
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
    Meow2::Editor::EditorWorld m_world{m_ids, m_jobs};
    std::unique_ptr<Meow2::Editor::EditorRenderer> m_renderer;
    ViewportId m_editor = 0;
    ViewportId m_game = 0;
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
