#pragma once

/// @file
/// @brief PlayMode: the editor's Play and Stop. Physics runs on a snapshot's worth of scene, and Stop puts it back.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/UndoStack.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Physics/PhysicsLibrary.h>
#include <Meow2/Physics/PhysicsScene.h>
#include <Meow2/Physics/PhysicsWorld.h>

#include <memory>
#include <string>
#include <vector>

namespace Meow2::Editor
{
/// @brief What PlayMode works with. Plain data; everything must outlive it.
struct PlayModeDeps
{
    EditorCommandRegistry& commands;  ///< Where `play.*` are registered.
    UndoStack& history;               ///< Paused while playing.
    EditorWorld& world;               ///< The scene that plays.
    Jobs::JobSystem& jobs;            ///< Runs the physics step.
    Physics::PhysicsLibrary* physics; ///< The physics library; null: Play is refused (no physics in this run).
    Log::Logger& log;                 ///< Receives what couldn't be simulated.
};

/// @brief Play and Stop (owner decision 2026-11-27: Stop restores the scene, like Unity and Unreal).
///
/// - **Play** (`play.start`, Ctrl+P): the scene is written to text (EditorWorld::snapshot()), a PhysicsWorld and a
///   PhysicsScene are made from its RigidBody and Collider components, and from then on every fixed step
///   (fixedUpdate(), called by the game loop) steps the physics, which moves the dynamic bodies' entities.
/// - **While playing:** the undo history is paused (nothing recorded, nothing undone: whatever happens is thrown
///   away), and the scene file commands are off (EditorWorld::playing()). Selecting, moving the camera, inspecting and
///   even editing still work; edits are dropped at Stop like everything else.
/// - **Stop** (`play.stop`): the physics goes, and the scene is read back from the snapshot, with the same entity IDs,
///   so the selection survives. The GPU keeps its meshes.
/// - `play.state` (query): whether it's playing, for how long, how many bodies, and what couldn't be built.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class PlayMode
{
public:
    /// @brief Registers `play.*`.
    /// @param deps What it works with.
    explicit PlayMode(const PlayModeDeps& deps);

    /// @brief Stops if playing, and removes its commands.
    ~PlayMode();

    PlayMode(const PlayMode&) = delete;            ///< Not copyable: registers commands.
    PlayMode& operator=(const PlayMode&) = delete; ///< Not copyable.

    /// @brief Starts playing.
    /// @return Nothing, or why not (already playing, no physics in this run, the scene can't be written).
    [[nodiscard]] Result<void> start();

    /// @brief Stops playing and restores the scene.
    /// @return Nothing, or why not (not playing; the snapshot can't be read, which keeps the played scene).
    Result<void> stop();

    /// @brief Whether it's playing.
    /// @return True between start() and stop().
    [[nodiscard]] bool playing() const
    {
        return m_physicsScene != nullptr;
    }

    /// @brief One fixed step of the game while playing (nothing otherwise): steps the physics.
    /// @param seconds The step (the game loop's fixed step).
    void fixedUpdate(float seconds);

private:
    /// @brief Registers `play.start`, `play.stop` and `play.state`.
    void registerCommands();

    PlayModeDeps m_deps;
    std::string m_snapshot;                                ///< The scene as it was at Play.
    std::unique_ptr<Physics::PhysicsWorld> m_physicsWorld; ///< While playing.
    std::unique_ptr<Physics::PhysicsScene> m_physicsScene; ///< While playing; goes before the world.
    std::vector<std::string> m_warnings;                   ///< From the last build.
    double m_seconds = 0.0;                                ///< Played so far.
};
} // namespace Meow2::Editor
