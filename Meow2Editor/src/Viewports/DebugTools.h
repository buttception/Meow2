#pragma once

/// @file
/// @brief DebugTools: the editor viewport's rendering debug tools as commands (debug views, wireframes, frozen
///        culling, the grid) and the frame and memory figures of the Stats dock.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorSettings.h"

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Time/FrameTimes.h>
#include <Meow2/Graphics/Renderer/RenderDebug.h>

#include <QJsonObject>

namespace Meow2::Editor
{
class EditorRenderer;
class EditorWorld;

/// @brief What DebugTools uses. Plain data (references); everything must outlive it.
struct DebugToolsDeps
{
    EditorCommandRegistry& commands; ///< Where its commands are registered.
    EditorSettings& settings;        ///< Its config file: the choices are saved there (`editor.debug.*`).
    const EditorWorld& world;        ///< The scene, for its entity count.
    EditorRenderer* renderer;        ///< Draws the debug views; null without a GPU (the choices are still kept).
    Log::Logger& log;                ///< Receives settings that couldn't be saved.
};

/// @brief The editor viewport's debug tools and the Stats dock's figures, behind commands so the Debug and Stats docks
///        and (PR 6) MCP all go the same way.
///
/// - `debug.setView` {view}: a debug view by key (`shaded`, `baseColor`, …, Renderer::debugViewEntries());
/// - `debug.setWireframe` {mode}: `off`, `overlay` or `only`;
/// - `debug.setCulling` {freeze?, showFrustum?}: keep culling with the camera of the moment, and draw that frustum;
/// - `debug.setGrid` {on}: the ground grid;
/// - `debug.state` (query): all of the above, and whether a debug pipeline failed;
/// - `stats.frame` (query): frame times, the GPU and its memory, the scene's counts, memory per tag.
///
/// Nothing here is undoable: these change how the scene is looked at, not the scene. The choices are saved to the
/// editor's config (except freezing, which only means something while the editor runs) and applied to the renderer
/// every frame (apply()).
/// @par Thread safety
/// Not thread-safe; the main thread only.
/// @par Lifetime
/// Registers its commands on construction and removes them on destruction.
class DebugTools
{
public:
    /// @brief Reads the saved choices and registers the commands.
    /// @param deps What it uses.
    explicit DebugTools(const DebugToolsDeps& deps);

    /// @brief Removes the commands.
    ~DebugTools();

    DebugTools(const DebugTools&) = delete;            ///< Not copyable: registered commands point at it.
    DebugTools& operator=(const DebugTools&) = delete; ///< Not copyable.

    /// @brief Records a frame's time for the Stats dock.
    /// @param seconds How long the frame took.
    void addFrame(float seconds);

    /// @brief Hands the current choices to the renderer, before it draws.
    void apply();

    /// @brief The current choices.
    /// @return Them.
    [[nodiscard]] const Renderer::RenderDebugState& state() const
    {
        return m_state;
    }

    /// @brief Whether the grid is on.
    /// @return True if it's drawn.
    [[nodiscard]] bool grid() const
    {
        return m_grid;
    }

    /// @brief The frame times kept for the Stats dock.
    /// @return Them.
    [[nodiscard]] const Time::FrameTimes& frameTimes() const
    {
        return m_frames;
    }

private:
    /// @brief Registers the commands.
    void registerCommands();

    /// @brief The choices as JSON, as `debug.state` and every debug command return them.
    [[nodiscard]] QJsonObject stateJson() const;

    /// @brief The figures as JSON, as `stats.frame` returns them.
    [[nodiscard]] QJsonObject statsJson() const;

    /// @brief Writes the choices to the config file and saves it; a failed save is logged (the choice still holds).
    void save();

    DebugToolsDeps m_deps;
    Renderer::RenderDebugState m_state;
    bool m_grid = false;
    Time::FrameTimes m_frames;
};

/// @brief `editor.debug.view`: the debug view's key (DebugViewEntry::key).
inline constexpr const char* c_debugViewKey = "editor.debug.view";
/// @brief `editor.debug.wireframe`: `off`, `overlay` or `only`.
inline constexpr const char* c_debugWireframeKey = "editor.debug.wireframe";
/// @brief `editor.debug.showFrustum`: draw the frozen camera's frustum while culling is frozen.
inline constexpr const char* c_debugShowFrustumKey = "editor.debug.showFrustum";
/// @brief `editor.debug.grid`: the ground grid (on by default).
inline constexpr const char* c_debugGridKey = "editor.debug.grid";
} // namespace Meow2::Editor
