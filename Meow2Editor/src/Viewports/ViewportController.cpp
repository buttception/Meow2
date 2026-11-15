/// @file
/// @brief ViewportController (ViewportController.h).

#include "Viewports/ViewportController.h"

#include "Commands/CommandSchema.h"
#include "Viewports/QtInputSource.h"

#include <Meow2/Scene/Camera.h>

#include <array>
#include <cmath>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief A widget's size in pixels (Qt sizes are logical; the swapchain needs pixels).
std::pair<std::uint32_t, std::uint32_t> pixelSize(const QWidget& widget)
{
    const double ratio = widget.devicePixelRatioF();
    return {static_cast<std::uint32_t>(std::lround(widget.width() * ratio)),
            static_cast<std::uint32_t>(std::lround(widget.height() * ratio))};
}

/// @brief Whether any of a widget can be seen: shown, and not covered by a tab or a collapsed dock.
bool canBeSeen(const QWidget& widget)
{
    return widget.isVisible() && !widget.visibleRegion().isEmpty();
}
} // namespace

ViewportController::ViewportController(const ViewportControllerDeps& deps)
    : m_deps(deps)
{
    registerCommands();
    addViewports();
    m_camera.focus(m_deps.world.bounds().center, m_deps.world.bounds().radius);
}

ViewportController::~ViewportController()
{
    for (const auto* name :
         {u"viewport.focus", u"viewport.setCameraMode", u"viewport.setRenderScale", u"viewport.info"})
    {
        m_deps.commands.remove(QStringView{name});
    }
}

int ViewportController::renderScalePercent() const
{
    return static_cast<int>(m_deps.settings.intValue(QString::fromLatin1(c_gameRenderScaleKey)));
}

void ViewportController::addViewports()
{
    if (m_deps.renderer == nullptr)
    {
        return;
    }
    const auto add = [this](ViewportWidget& widget, const char* name) -> std::optional<ViewportId>
    {
        const Platform::NativeWindowHandle handle = widget.nativeHandle();
        if (handle.type == Platform::NativeWindowType::None)
        {
            return std::nullopt; // the offscreen platform: nothing to draw into
        }
        const auto [width, height] = pixelSize(widget);
        Result<ViewportId> viewport = m_deps.renderer->addWindowViewport(handle, width, height, name);
        if (!viewport)
        {
            MEOW2_LOG_ERROR(m_deps.log, "The {} viewport can't draw: {}", name, viewport.error());
            return std::nullopt;
        }
        return *viewport;
    };
    m_editorViewport = add(m_deps.editorViewport, "Editor");
    m_gameViewport = add(m_deps.gameViewport, "Game");
}

void ViewportController::tick(float deltaSeconds)
{
    // The editor camera moves only while its viewport has focus: input meant for the game view or a dock stays there.
    m_deps.input.beginFrame();
    const bool editorFocused = m_deps.inputSource.activeViewport() == &m_deps.editorViewport;
    m_camera.update(editorFocused ? cameraInputFrom(m_deps.input) : Math::CameraInput{}, deltaSeconds);

    EditorRenderer* const renderer = m_deps.renderer;
    m_deps.world.update(renderer != nullptr ? &renderer->scene() : nullptr);
    if (renderer == nullptr)
    {
        return;
    }
    std::array<std::optional<ViewportCamera>, 2> cameras{};
    const auto prepare = [renderer](const std::optional<ViewportId>& id, const QWidget& widget)
    {
        if (!id)
        {
            return false;
        }
        const auto [width, height] = pixelSize(widget);
        renderer->resize(*id, width, height);
        renderer->setVisible(*id, canBeSeen(widget));
        return true;
    };
    if (prepare(m_editorViewport, m_deps.editorViewport))
    {
        cameras.at(*m_editorViewport) = ViewportCamera{.transform = m_camera.transform(), .lens = m_camera.lens()};
    }
    if (prepare(m_gameViewport, m_deps.gameViewport))
    {
        renderer->setRenderScale(*m_gameViewport, static_cast<float>(renderScalePercent()) / 100.0F);
        Scene::Scene& scene = m_deps.world.scene();
        if (const std::optional<entt::entity> main = Scene::findMainCamera(scene))
        {
            if (const std::optional<Math::Transform> transform = Scene::cameraTransform(scene, *main))
            {
                cameras.at(*m_gameViewport) =
                    ViewportCamera{.transform = *transform, .lens = scene.registry().get<Scene::Camera>(*main).lens};
            }
        }
    }
    if (Result<void> drawn = renderer->renderFrame(cameras); !drawn)
    {
        MEOW2_LOG_ERROR(m_deps.log, "Drawing the viewports failed: {}", drawn.error());
    }
}

void ViewportController::registerCommands()
{
    EditorCommandRegistry& commands = m_deps.commands;
    [[maybe_unused]] const Result<void> focus =
        commands.add({.name = u"viewport.focus"_s, .description = u"Frames the whole scene in the editor viewport."_s},
                     [this](const QJsonObject&) -> Result<CommandOutcome>
                     {
                         const Math::Sphere bounds = m_deps.world.bounds();
                         m_camera.focus(bounds.center, bounds.radius);
                         return CommandOutcome{};
                     });

    QJsonObject mode = propertySchema(u"string"_s, u"orbit: turn around a point; fly: move with WASD."_s);
    mode.insert(u"enum", QJsonArray{u"orbit"_s, u"fly"_s});
    [[maybe_unused]] const Result<void> setMode = commands.add(
        {.name = u"viewport.setCameraMode"_s,
         .description = u"How the editor viewport's camera moves."_s,
         .parameters = objectSchema({{.name = u"mode"_s, .schema = mode, .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            m_camera.setMode(parameters.value(u"mode").toString() == u"fly" ? CameraMode::Fly : CameraMode::Orbit);
            return CommandOutcome{};
        });

    QJsonObject percent = propertySchema(u"integer"_s, u"Percent of the viewport's size the scene is drawn at."_s);
    percent.insert(u"minimum", 25);
    percent.insert(u"maximum", 100);
    [[maybe_unused]] const Result<void> setScale = commands.add(
        {.name = u"viewport.setRenderScale"_s,
         .description = u"The game viewport's render scale: drawn smaller, then scaled up, for less GPU load."_s,
         .parameters = objectSchema({{.name = u"percent"_s, .schema = percent, .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            // A preference, saved with the others; the viewport picks it up at the next frame.
            if (Result<void> stored =
                    m_deps.settings.set(QString::fromLatin1(c_gameRenderScaleKey), parameters.value(u"percent"));
                !stored)
            {
                return Unexpected<Error>{stored.error()};
            }
            return CommandOutcome{.result = renderScalePercent()};
        });

    [[maybe_unused]] const Result<void> info = commands.add(
        {.name = u"viewport.info"_s,
         .description =
             u"The viewports: size, whether they're visible and drew last frame; camera mode; render scale."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            const auto describe = [this](const std::optional<ViewportId>& id, const QWidget& widget)
            {
                const auto [width, height] = pixelSize(widget);
                QJsonObject viewport{{u"width"_s, static_cast<double>(width)},
                                     {u"height"_s, static_cast<double>(height)},
                                     {u"visible"_s, canBeSeen(widget)},
                                     {u"drawn"_s, id && m_deps.renderer != nullptr && m_deps.renderer->wasDrawn(*id)}};
                if (id && m_deps.renderer != nullptr)
                {
                    const auto [sceneWidth, sceneHeight] = m_deps.renderer->sceneSize(*id);
                    viewport.insert(u"sceneWidth", static_cast<double>(sceneWidth));
                    viewport.insert(u"sceneHeight", static_cast<double>(sceneHeight));
                }
                return viewport;
            };
            const Math::Transform camera = m_camera.transform();
            return CommandOutcome{
                .result = QJsonObject{{u"editor"_s, describe(m_editorViewport, m_deps.editorViewport)},
                                      {u"game"_s, describe(m_gameViewport, m_deps.gameViewport)},
                                      {u"cameraMode"_s, cameraModeName(m_camera.mode())},
                                      {u"cameraPosition"_s, QJsonArray{static_cast<double>(camera.position.x),
                                                                       static_cast<double>(camera.position.y),
                                                                       static_cast<double>(camera.position.z)}},
                                      {u"renderScale"_s, renderScalePercent()}}};
        });
}
} // namespace Meow2::Editor
