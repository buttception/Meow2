#pragma once

/// @file
/// @brief ViewportController: drives the editor and game viewports each frame, and their commands.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorSettings.h"
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
/// A viewport whose widget isn't visible (a hidden or tabbed-away dock) isn't drawn. Its commands:
/// - `viewport.focus`: frames the scene in the editor viewport (F);
/// - `viewport.setCameraMode {mode}`: `orbit` or `fly`;
/// - `viewport.setRenderScale {percent}`: the game viewport's render scale, 25..100, saved as
///   `editor.gameRenderScale`;
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

    /// @brief The game viewport's render scale, from the settings.
    /// @return 25 to 100.
    [[nodiscard]] int renderScalePercent() const;

private:
    /// @brief Registers `viewport.*`.
    void registerCommands();
    /// @brief Adds the two viewports to the renderer, if their widgets have native windows.
    void addViewports();

    ViewportControllerDeps m_deps;
    EditorCamera m_camera;
    std::optional<ViewportId> m_editorViewport; ///< In the renderer; unset without one (or without native windows).
    std::optional<ViewportId> m_gameViewport;
};
} // namespace Meow2::Editor
