/// @file
/// @brief ViewportController (ViewportController.h).

#include "Viewports/ViewportController.h"

#include "Commands/CommandSchema.h"
#include "Viewports/QtInputSource.h"

#include <Meow2/Scene/Camera.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <utility>
#include <vector>

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
    for (const auto* name : {u"viewport.focus", u"viewport.setCameraMode", u"viewport.setRenderScale", u"viewport.info",
                             u"viewport.setGizmo", u"viewport.viewFrom"})
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
    if (m_editorViewport)
    {
        m_deps.renderer->setOutline(*m_editorViewport, true); // the game view shows the game, without editor marks
    }
}

void ViewportController::handleClicks()
{
    using Input::Key;
    using Input::MouseButton;
    const Input::InputSystem& input = m_deps.input;
    const bool alt = input.isDown(Key::LeftAlt) || input.isDown(Key::RightAlt);
    if (input.wasPressed(MouseButton::Left) && !alt)
    {
        m_press = input.mousePosition();
        m_pressToggle = input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl);
    }
    if (!m_press || !input.wasReleased(MouseButton::Left))
    {
        return;
    }
    // A click, not a drag: released within a few pixels of where it went down.
    const Input::Vec2 at = input.mousePosition();
    const Input::Vec2 from = *std::exchange(m_press, std::nullopt);
    constexpr float c_clickSlop = 4.0F; // logical pixels
    if (std::abs(at.x - from.x) > c_clickSlop || std::abs(at.y - from.y) > c_clickSlop || m_deps.renderer == nullptr ||
        !m_editorViewport)
    {
        return;
    }
    // Qt's positions are logical; the viewport's pixels are scaled by the display.
    const double ratio = m_deps.editorViewport.devicePixelRatioF();
    const auto pixel = [ratio](float logical)
    {
        return static_cast<std::uint32_t>(std::max(0L, std::lround(static_cast<double>(logical) * ratio)));
    };
    m_deps.renderer->requestPick(*m_editorViewport, pixel(at.x), pixel(at.y));
    m_pickToggle = m_pressToggle;
}

void ViewportController::applyPick(const PickResult& result)
{
    std::vector<entt::entity> entities;
    entities.reserve(result.hits.size());
    for (const PickHit& hit : result.hits)
    {
        entities.push_back(m_deps.world.instanceEntity(hit.instance));
    }
    const std::vector<EntityId> hits = ClickSelection::resolve(m_deps.world.scene(), entities);
    const std::vector<EntityId> current = m_deps.sceneEditor.selection();
    const std::vector<EntityId> next = m_clicks.click(current, hits, result.x, result.y, m_pickToggle);
    if (next == current)
    {
        return;
    }
    QJsonArray ids;
    for (const EntityId id : next)
    {
        ids.append(QString::fromStdString(id.uuid().toString()));
    }
    if (const Result<QJsonValue> set = m_deps.commands.execute(u"selection.set", {{u"ids"_s, ids}}); !set)
    {
        MEOW2_LOG_WARN(m_deps.log, "selection.set: {}", set.error());
    }
}

std::pair<float, float> ViewportController::cursorPixels() const
{
    // Qt's positions are logical; the viewport's pixels are scaled by the display.
    const auto ratio = static_cast<float>(m_deps.editorViewport.devicePixelRatioF());
    const Input::Vec2 at = m_deps.input.mousePosition();
    return {at.x * ratio, at.y * ratio};
}

std::optional<entt::entity> ViewportController::gizmoTarget() const
{
    const std::vector<EntityId> selection = m_deps.sceneEditor.selection();
    if (selection.empty())
    {
        return std::nullopt;
    }
    const Scene::Scene& scene = m_deps.world.scene();
    const std::optional<entt::entity> entity = scene.find(selection.back());
    if (!entity || !scene.registry().all_of<Scene::LocalTransform, Scene::WorldTransform>(*entity))
    {
        return std::nullopt;
    }
    return entity;
}

std::optional<GizmoView> ViewportController::gizmoView() const
{
    const auto [width, height] = pixelSize(m_deps.editorViewport);
    if (width == 0 || height == 0)
    {
        return std::nullopt;
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    return GizmoView{.matrices = Math::cameraMatrices(m_camera.transform(), m_camera.lens(), aspect),
                     .viewport = {.width = static_cast<float>(width), .height = static_cast<float>(height)}};
}

namespace
{
/// @brief An entity's world transform, from its world matrix.
std::optional<Math::Transform> worldTransformOf(const Scene::Scene& scene, entt::entity entity)
{
    return Math::fromMatrix(Math::load(scene.registry().get<Scene::WorldTransform>(entity).matrix));
}

/// @brief The local transform that puts an entity at a world transform: through its parent's world matrix
///        (local = world × parent⁻¹, row vectors); exact unless the parent's scale is non-uniform and rotated.
std::optional<Math::Transform> localFromWorld(const Scene::Scene& scene, entt::entity entity,
                                              const Math::Transform& world)
{
    const auto& registry = scene.registry();
    const auto* parent = registry.try_get<Scene::Parent>(entity);
    if (parent == nullptr || parent->entity == entt::null || !registry.valid(parent->entity) ||
        !registry.all_of<Scene::WorldTransform>(parent->entity))
    {
        return world;
    }
    const DirectX::XMMATRIX parentWorld = Math::load(registry.get<Scene::WorldTransform>(parent->entity).matrix);
    DirectX::XMVECTOR determinant;
    const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, parentWorld);
    if (std::abs(DirectX::XMVectorGetX(determinant)) < 1e-12F)
    {
        return std::nullopt; // a parent scaled to nothing: no local transform can undo it
    }
    return Math::fromMatrix(DirectX::XMMatrixMultiply(Math::toMatrix(world), inverse));
}

/// @brief The sides the camera can view from (viewport.viewFrom), and the direction each stands for.
constexpr std::array<std::pair<const char16_t*, Math::Vec3>, 6> c_sides{{{u"+x", {1.0F, 0.0F, 0.0F}},
                                                                         {u"-x", {-1.0F, 0.0F, 0.0F}},
                                                                         {u"+y", {0.0F, 1.0F, 0.0F}},
                                                                         {u"-y", {0.0F, -1.0F, 0.0F}},
                                                                         {u"+z", {0.0F, 0.0F, 1.0F}},
                                                                         {u"-z", {0.0F, 0.0F, -1.0F}}}};

/// @brief A side's name for a direction from the view-axis gizmo.
QString sideName(const Math::Vec3& direction)
{
    for (const auto& [name, side] : c_sides)
    {
        if (direction.x * side.x + direction.y * side.y + direction.z * side.z > 0.999F)
        {
            return QString::fromUtf16(name);
        }
    }
    return u"+z"_s;
}

/// @brief A transform as `entity.setTransform` takes it.
QJsonObject transformJson(const Math::Transform& transform)
{
    const auto d = [](float value)
    {
        return static_cast<double>(value);
    };
    return {{u"position"_s, QJsonArray{d(transform.position.x), d(transform.position.y), d(transform.position.z)}},
            {u"rotation"_s, QJsonArray{d(transform.rotation.x), d(transform.rotation.y), d(transform.rotation.z),
                                       d(transform.rotation.w)}},
            {u"scale"_s, QJsonArray{d(transform.scale.x), d(transform.scale.y), d(transform.scale.z)}}};
}
} // namespace

bool ViewportController::handleGizmo(bool editorFocused)
{
    using Input::Key;
    using Input::MouseButton;
    const Input::InputSystem& input = m_deps.input;
    m_hover = GizmoHandle::None;
    if (m_gizmo.dragged() != GizmoHandle::None)
    {
        const std::optional<entt::entity> target = m_deps.world.scene().find(m_dragEntity);
        const std::optional<GizmoView> view = gizmoView();
        if (!editorFocused || !input.isDown(MouseButton::Left) || !target || !view)
        {
            finishGizmoDrag(); // released, focus lost, or the entity went away (an undo, an agent)
            return true;
        }
        const bool ctrl = input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl);
        const GizmoSnap snap{.enabled = m_snap || ctrl,
                             .translate =
                                 static_cast<float>(m_deps.settings.intValue(u"editor.snap.translate"_s)) / 100.0F,
                             .angleDegrees = static_cast<float>(m_deps.settings.intValue(u"editor.snap.angle"_s)),
                             .scale = static_cast<float>(m_deps.settings.intValue(u"editor.snap.scale"_s)) / 100.0F};
        const auto [x, y] = cursorPixels();
        if (const std::optional<Math::Transform> world = m_gizmo.drag(*view, x, y, snap))
        {
            const std::optional<Math::Transform> local = localFromWorld(m_deps.world.scene(), *target, *world);
            if (local && (!m_dragLast || !Math::nearlyEqual(*local, *m_dragLast)))
            {
                QJsonObject parameters = transformJson(*local);
                parameters.insert(u"id", QString::fromStdString(m_dragEntity.uuid().toString()));
                parameters.insert(u"preview", true);
                if (const Result<QJsonValue> set = m_deps.commands.execute(u"entity.setTransform", parameters); !set)
                {
                    MEOW2_LOG_WARN(m_deps.log, "entity.setTransform: {}", set.error());
                }
                m_dragLast = local;
            }
        }
        return true;
    }
    m_axisHover.reset();
    if (!editorFocused)
    {
        return false;
    }
    // The view-axis gizmo in the corner first: a click on one of its balls turns the camera.
    if (const std::optional<GizmoView> corner = gizmoView())
    {
        const auto [cornerX, cornerY] = cursorPixels();
        m_axisHover = ViewAxes::hitTest(*corner, cornerX, cornerY);
        if (m_axisHover && input.wasPressed(MouseButton::Left))
        {
            if (const Result<QJsonValue> turned =
                    m_deps.commands.execute(u"viewport.viewFrom", {{u"side"_s, sideName(*m_axisHover)}});
                !turned)
            {
                MEOW2_LOG_WARN(m_deps.log, "viewport.viewFrom: {}", turned.error());
            }
            return true;
        }
    }
    const std::optional<entt::entity> target = gizmoTarget();
    const std::optional<GizmoView> view = gizmoView();
    if (!target || !view)
    {
        return false;
    }
    const std::optional<Math::Transform> world = worldTransformOf(m_deps.world.scene(), *target);
    if (!world)
    {
        return false;
    }
    const auto [x, y] = cursorPixels();
    m_hover = m_gizmo.hitTest(*view, *world, x, y);
    const bool alt = input.isDown(Key::LeftAlt) || input.isDown(Key::RightAlt);
    if (m_hover == GizmoHandle::None || alt || !input.wasPressed(MouseButton::Left) ||
        !m_gizmo.beginDrag(*view, *world, m_hover, x, y))
    {
        return false;
    }
    const Scene::Scene& scene = m_deps.world.scene();
    m_dragEntity = scene.idOf(*target);
    m_dragFrom = scene.registry().get<Scene::LocalTransform>(*target).value;
    m_dragLast.reset();
    return true;
}

void ViewportController::finishGizmoDrag()
{
    m_gizmo.endDrag();
    if (!m_dragLast || Math::nearlyEqual(*m_dragLast, m_dragFrom) || !m_deps.world.scene().find(m_dragEntity))
    {
        return; // nothing moved, or nothing left to move
    }
    QJsonObject parameters = transformJson(*m_dragLast);
    parameters.insert(u"id", QString::fromStdString(m_dragEntity.uuid().toString()));
    parameters.insert(u"from", transformJson(m_dragFrom));
    if (const Result<QJsonValue> set = m_deps.commands.execute(u"entity.setTransform", parameters); !set)
    {
        MEOW2_LOG_WARN(m_deps.log, "entity.setTransform: {}", set.error());
    }
    m_dragLast.reset();
}

void ViewportController::updateGizmoLines()
{
    if (m_deps.renderer == nullptr || !m_editorViewport)
    {
        return;
    }
    const std::optional<entt::entity> target =
        m_gizmo.dragged() != GizmoHandle::None ? m_deps.world.scene().find(m_dragEntity) : gizmoTarget();
    const std::optional<GizmoView> view = gizmoView();
    std::optional<Math::Transform> world;
    if (target && view)
    {
        world = worldTransformOf(m_deps.world.scene(), *target);
    }
    const GizmoHandle highlight = m_gizmo.dragged() != GizmoHandle::None ? m_gizmo.dragged() : m_hover;
    ViewportOverlay overlay;
    if (world)
    {
        GizmoShapes shapes = m_gizmo.shapes(*view, *world, highlight);
        overlay.lines = std::move(shapes.lines);
        overlay.triangles = std::move(shapes.triangles);
    }
    if (view)
    {
        GizmoShapes axes = ViewAxes::shapes(*view, m_axisHover);
        overlay.screenLines = std::move(axes.lines);
        overlay.screenTriangles = std::move(axes.triangles);
    }
    m_deps.renderer->setOverlay(*m_editorViewport, std::move(overlay));
}

void ViewportController::tick(float deltaSeconds)
{
    // The editor camera moves only while its viewport has focus: input meant for the game view or a dock stays there.
    m_deps.input.beginFrame();
    m_drew = false;
    const bool editorFocused = m_deps.inputSource.activeViewport() == &m_deps.editorViewport;
    m_camera.update(editorFocused ? cameraInputFrom(m_deps.input) : Math::CameraInput{}, deltaSeconds);
    // The gizmo first: a press on one of its handles is a drag, not a click that picks.
    if (handleGizmo(editorFocused))
    {
        m_press.reset();
    }
    else if (editorFocused)
    {
        handleClicks();
    }
    else
    {
        m_press.reset(); // a press that started in the viewport and ended elsewhere isn't a click
    }

    EditorRenderer* const renderer = m_deps.renderer;
    // Models edited on disk (a texture repainted, a model re-exported) are reloaded while the editor runs.
    m_deps.world.reloadChangedModels(renderer != nullptr ? &renderer->scene() : nullptr);
    const std::vector<EntityId> selection = m_deps.sceneEditor.selection();
    m_deps.world.update(renderer != nullptr ? &renderer->scene() : nullptr, selection);
    if (renderer != nullptr)
    {
        if (const std::optional<Scene::Environment> environment = m_deps.world.environment())
        {
            renderer->setEnvironment(*environment); // the scene's sun and exposure
        }
        renderer->setHasSelection(m_deps.world.anySelectedInstance());
        // A pick from a frame or two ago: the instance slots still name last update's entities, or ones checked gone.
        if (const std::optional<PickResult> picked = renderer->takePickResult())
        {
            applyPick(*picked);
        }
        updateGizmoLines(); // after the update: the gizmo sits where the entity is this frame
    }
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
    m_drew = (m_editorViewport && renderer->wasDrawn(*m_editorViewport)) ||
             (m_gameViewport && renderer->wasDrawn(*m_gameViewport));
}

void ViewportController::registerCommands()
{
    EditorCommandRegistry& commands = m_deps.commands;
    [[maybe_unused]] const Result<void> focus = commands.add(
        {.name = u"viewport.focus"_s,
         .description = u"Frames an entity (id) in the editor viewport, with everything under it; without "
                        u"an id the selection, and with nothing selected the whole scene."_s,
         .parameters = objectSchema({{.name = u"id"_s, .schema = propertySchema(u"string"_s, u"The entity's ID."_s)}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            // Which entity: the one named, else the last selected one.
            std::optional<EntityId> target;
            if (parameters.contains(u"id"))
            {
                const Result<Uuid> uuid = Uuid::parse(parameters.value(u"id").toString().toStdString());
                if (!uuid)
                {
                    return fail(ErrorCode::InvalidArgument, "id: " + uuid.error().message);
                }
                const Result<EntityId> id = EntityId::fromUuid(*uuid);
                if (!id)
                {
                    return fail(ErrorCode::InvalidArgument, "id: " + id.error().message);
                }
                target = *id;
            }
            else if (const std::vector<EntityId> selection = m_deps.sceneEditor.selection(); !selection.empty())
            {
                target = selection.back();
            }
            Math::Sphere bounds = m_deps.world.bounds();
            if (target)
            {
                const std::optional<entt::entity> entity = m_deps.world.scene().find(*target);
                const std::optional<Math::Sphere> around = entity ? m_deps.world.boundsOf(*entity) : std::nullopt;
                if (!around)
                {
                    return fail(ErrorCode::NotFound, "no entity with a transform has that ID");
                }
                bounds = *around;
            }
            // The orbit stands far enough back for the whole sphere to fit the view (EditorCamera::focus).
            m_camera.focus(bounds.center, bounds.radius);
            return CommandOutcome{};
        });

    QJsonObject side = propertySchema(u"string"_s, u"+x: from the +X side looking towards -X, and so on."_s);
    QJsonArray sides;
    for (const auto& [name, direction] : c_sides)
    {
        sides.append(QString::fromUtf16(name));
    }
    side.insert(u"enum", sides);
    [[maybe_unused]] const Result<void> viewFrom = commands.add(
        {.name = u"viewport.viewFrom"_s,
         .description = u"Turns the editor camera to look at what it looks at from one side (the view-axis gizmo): "
                        u"from +x it looks along -x, from +y down, and so on."_s,
         .parameters = objectSchema({{.name = u"side"_s, .schema = side, .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const QString name = parameters.value(u"side").toString();
            for (const auto& [candidate, direction] : c_sides)
            {
                if (name == QString::fromUtf16(candidate))
                {
                    m_camera.viewAlong({-direction.x, -direction.y, -direction.z});
                    return CommandOutcome{};
                }
            }
            return fail(ErrorCode::InvalidArgument, "side must be one of +x, -x, +y, -y, +z, -z");
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

    QJsonObject gizmoMode = propertySchema(u"string"_s, u"translate: arrows; rotate: rings; scale: boxes."_s);
    gizmoMode.insert(u"enum", QJsonArray{u"translate"_s, u"rotate"_s, u"scale"_s});
    QJsonObject gizmoSpace = propertySchema(u"string"_s, u"world: the world's axes; local: the entity's own."_s);
    gizmoSpace.insert(u"enum", QJsonArray{u"world"_s, u"local"_s});
    [[maybe_unused]] const Result<void> setGizmo = commands.add(
        {.name = u"viewport.setGizmo"_s,
         .description = u"The editor viewport's gizmo: mode, axes for translate and rotate (scale is always "
                        u"local), and snapping (Ctrl snaps while held either way). Each is optional."_s,
         .parameters = objectSchema(
             {{.name = u"mode"_s, .schema = gizmoMode},
              {.name = u"space"_s, .schema = gizmoSpace},
              {.name = u"snap"_s, .schema = propertySchema(u"boolean"_s, u"Snap to the editor.snap.* steps."_s)}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if ((parameters.contains(u"mode") || parameters.contains(u"space")) &&
                m_gizmo.dragged() != GizmoHandle::None)
            {
                finishGizmoDrag(); // a drag in progress keeps what it did, as one step
            }
            if (parameters.contains(u"mode"))
            {
                const QString chosen = parameters.value(u"mode").toString();
                m_gizmo.setMode(chosen == u"rotate"  ? GizmoMode::Rotate
                                : chosen == u"scale" ? GizmoMode::Scale
                                                     : GizmoMode::Translate);
            }
            if (parameters.contains(u"space"))
            {
                m_gizmo.setSpace(parameters.value(u"space").toString() == u"local" ? GizmoSpace::Local
                                                                                   : GizmoSpace::World);
            }
            if (parameters.contains(u"snap"))
            {
                m_snap = parameters.value(u"snap").toBool();
            }
            return CommandOutcome{};
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
                .result = QJsonObject{
                    {u"editor"_s, describe(m_editorViewport, m_deps.editorViewport)},
                    {u"game"_s, describe(m_gameViewport, m_deps.gameViewport)},
                    {u"cameraMode"_s, cameraModeName(m_camera.mode())},
                    {u"cameraPosition"_s,
                     QJsonArray{static_cast<double>(camera.position.x), static_cast<double>(camera.position.y),
                                static_cast<double>(camera.position.z)}},
                    {u"renderScale"_s, renderScalePercent()},
                    {u"gizmo"_s,
                     QJsonObject{{u"mode"_s, QString::fromLatin1(gizmoModeName(m_gizmo.mode()))},
                                 {u"space"_s, m_gizmo.space() == GizmoSpace::Local ? u"local"_s : u"world"_s},
                                 {u"snap"_s, m_snap}}}}};
        });
}
} // namespace Meow2::Editor
