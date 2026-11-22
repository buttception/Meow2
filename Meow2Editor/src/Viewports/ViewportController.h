#pragma once

/// @file
/// @brief ViewportController: drives the editor and game viewports each frame, and their commands.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorSettings.h"
#include "Editing/ClickSelection.h"
#include "Editing/Gizmo.h"
#include "Editing/SceneEditor.h"
#include "Viewports/EditorCamera.h"
#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"
#include "ViewportWidget.h"

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Platform/Input/InputSystem.h>

#include <optional>

namespace Meow2::Editor
{
class QtInputSource;

/// @brief `editor.gameRenderScale`: the game viewport's render scale in percent.
inline constexpr const char* c_gameRenderScaleKey = "editor.gameRenderScale";

/// @brief What a ViewportController works with. Plain data (references); everything must outlive it.
struct ViewportControllerDeps
{
    EditorCommandRegistry& commands; ///< Where its commands are registered.
    EditorSettings& settings;        ///< The game view's render scale (`editor.gameRenderScale`).
    EditorWorld& world;              ///< The scene both viewports show.
    SceneEditor& sceneEditor;        ///< The selection: outlined, and set by clicking in the editor viewport.
    Input::InputSystem& input;       ///< Keyboard and mouse, fed by the QtInputSource.
    QtInputSource& inputSource;      ///< Says which viewport has focus.
    EditorRenderer* renderer;        ///< Draws the viewports; null without a GPU (offscreen platform, tests).
    ViewportWidget& editorViewport;  ///< The editor viewport's widget.
    ViewportWidget& gameViewport;    ///< The game viewport's widget.
    Log::Logger& log;                ///< Receives drawing failures.
};

/// @brief Runs the two viewports: the editor camera from the input, each viewport's size and visibility, the scene's
///        transforms, and one frame of both through the EditorRenderer.
///
/// The editor viewport shows the EditorCamera; the game viewport shows the scene's main camera (`Scene::Camera`).
/// A left click (without Alt, which orbits) in the editor viewport picks what's under the cursor on the GPU and
/// runs `selection.set` with it when the result comes back (ClickSelection: the nearest, the next one behind on a
/// second click at the same spot, Ctrl to add or remove, nothing to clear); the selection is outlined there.
/// The last selected entity gets a gizmo there (Gizmo): hovering highlights a handle, and dragging one runs
/// `entity.setTransform` previews while the button is held and one recorded step on release, so a drag is one undo
/// step; a gizmo under the cursor wins over picking. Snapping is on with `viewport.setGizmo {snap}` or while Ctrl is
/// held, in the steps of `editor.snap.*`.
/// A viewport whose widget isn't visible (a hidden or tabbed-away dock) isn't drawn. Its commands:
/// - `viewport.focus {id}`: frames an entity and everything under it (a double-click in the Hierarchy), or the
///   selection, or the whole scene (F);
/// - `viewport.setCameraMode {mode}`: `orbit` or `fly`;
/// - `viewport.setRenderScale {percent}`: the game viewport's render scale, 25..100, saved as
///   `editor.gameRenderScale`;
/// - `viewport.viewFrom {side}`: looks from +x, -x, +y, -y, +z or -z (the view-axis gizmo in the top-right corner);
/// - `viewport.setGizmo {mode, space, snap}`: the gizmo's mode (`translate`, `rotate`, `scale`), axes (`world`,
///   `local`) and snapping (each optional);
/// - query `viewport.info`: each viewport's size, visibility, whether it drew, the camera mode and the render scale.
/// @par Thread safety
/// Not thread-safe; GUI thread only.
class ViewportController
{
public:
    /// @brief Registers the viewport commands and adds the two viewports to the renderer (if there is one).
    /// @param deps What it works with.
    explicit ViewportController(const ViewportControllerDeps& deps);

    /// @brief Removes its commands.
    ~ViewportController();

    ViewportController(const ViewportController&) = delete;            ///< Holds references and registered commands.
    ViewportController& operator=(const ViewportController&) = delete; ///< Holds references and registered commands.

    /// @brief One frame: input → camera (only while the editor viewport has focus), sizes and visibility, the scene's
    ///        transforms and instances, then drawing.
    /// @param deltaSeconds The frame's length.
    void tick(float deltaSeconds);

    /// @brief The editor viewport's camera.
    /// @return It.
    [[nodiscard]] EditorCamera& camera()
    {
        return m_camera;
    }

    /// @brief The editor viewport's gizmo (its mode and space).
    /// @return It.
    [[nodiscard]] const Gizmo& gizmo() const
    {
        return m_gizmo;
    }

    /// @brief Whether the last tick() drew a viewport. Drawing waits for the display (vsync), so the editor's frame
    ///        timer can run as fast as it likes then; without it, the timer must pace frames itself.
    /// @return True if a viewport was drawn.
    [[nodiscard]] bool drewLastFrame() const
    {
        return m_drew;
    }

    /// @brief Whether snapping is switched on (Ctrl snaps while held either way).
    /// @return True if it is.
    [[nodiscard]] bool snapping() const
    {
        return m_snap;
    }

    /// @brief The game viewport's render scale, from the settings.
    /// @return 25 to 100.
    [[nodiscard]] int renderScalePercent() const;

private:
    /// @brief Registers `viewport.*`.
    void registerCommands();
    /// @brief Adds the two viewports to the renderer, if their widgets have native windows.
    void addViewports();
    /// @brief Turns a left click (press and release without moving, no Alt) into a pick request.
    void handleClicks();
    /// @brief Applies a pick's result to the selection through `selection.set`.
    /// @param result What the pick found.
    void applyPick(const PickResult& result);
    /// @brief Hovering and dragging the gizmo.
    /// @param editorFocused Whether the editor viewport has the keyboard and mouse.
    /// @return True if the gizmo took the mouse this frame (a press on a handle, or a drag), so it isn't a click.
    bool handleGizmo(bool editorFocused);
    /// @brief Ends a drag: one recorded `entity.setTransform` from where it began, if it moved anything.
    void finishGizmoDrag();
    /// @brief Hands this frame's gizmos to the renderer: the transform gizmo (none without a target) and the
    ///        view-axis gizmo in the corner.
    void updateGizmoLines();
    /// @brief The entity the gizmo is on: the last selected one, if it's there and has a transform.
    /// @return It, or nothing.
    [[nodiscard]] std::optional<entt::entity> gizmoTarget() const;
    /// @brief The editor camera as the gizmo sees it this frame.
    /// @return It, or nothing while the editor viewport has no size.
    [[nodiscard]] std::optional<GizmoView> gizmoView() const;
    /// @brief The cursor in the editor viewport's pixels.
    /// @return x and y.
    [[nodiscard]] std::pair<float, float> cursorPixels() const;

    ViewportControllerDeps m_deps;
    EditorCamera m_camera;
    std::optional<ViewportId> m_editorViewport; ///< In the renderer; unset without one (or without native windows).
    std::optional<ViewportId> m_gameViewport;
    ClickSelection m_clicks;                   ///< Steps through the surfaces under repeated clicks.
    std::optional<Input::Vec2> m_press;        ///< Where the left button went down (logical pixels), for a click.
    bool m_pressToggle = false;                ///< Ctrl was held at that press.
    bool m_pickToggle = false;                 ///< Ctrl was held for the pick in flight.
    Gizmo m_gizmo;                             ///< The editor viewport's gizmo.
    bool m_drew = false;                       ///< drewLastFrame().
    bool m_snap = false;                       ///< `viewport.setGizmo {snap}`.
    GizmoHandle m_hover = GizmoHandle::None;   ///< The handle under the cursor this frame.
    std::optional<Math::Vec3> m_axisHover;     ///< The view-axis gizmo's ball under the cursor.
    EntityId m_dragEntity{};                   ///< What the drag moves.
    Math::Transform m_dragFrom{};              ///< Its local transform when the drag began.
    std::optional<Math::Transform> m_dragLast; ///< The last local transform the drag previewed.
};
} // namespace Meow2::Editor
