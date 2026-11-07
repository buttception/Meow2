#pragma once

/// @file
/// @brief ShaderHotReload: recompiles shaders when their files change while an app runs.
/// @code
/// Meow2::Assets::ShaderHotReload hotReload{{.assets = assets, .cooker = cooker, .jobs = jobs,
///                                           .events = events, .log = log}};
/// hotReload.watchAll();
/// // in the game loop, a couple of times per second:
/// hotReload.poll();
/// // the renderer rebuilds its pipelines:
/// auto subscription = events.subscribe<Meow2::Assets::ShaderReloaded>([&](const auto& e) { rebuild(e.id); });
/// @endcode
/// Needs DXC, so it exists in Renderer and Editor, never in Meow2Player.
/// @see techstack.md §12, docs/wiki/Shaders.md

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Result.h>

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Meow2::Events
{
class EventBus;
}

namespace Meow2::Log
{
class Logger;
}

namespace Meow2::Assets
{
class ShaderCooker;

/// @brief A shader was recompiled and reloaded: rebuild what uses it. Plain data; sent through the EventBus.
struct ShaderReloaded
{
    AssetId id; ///< The shader.
};

/// @brief A changed shader didn't compile; the old one stays in use. Plain data; sent through the EventBus.
struct ShaderReloadFailed
{
    AssetId id;          ///< The shader.
    std::string message; ///< The compiler's messages, `path:line:column: error: …`.
};

/// @brief What ShaderHotReload uses. Plain data; everything must outlive it.
struct ShaderHotReloadDeps
{
    AssetManager& assets;     ///< Holds the shaders.
    ShaderCooker& cooker;     ///< Compiles them.
    Jobs::JobSystem& jobs;    ///< Runs the compiles away from the main thread.
    Events::EventBus& events; ///< Receives ShaderReloaded and ShaderReloadFailed.
    Log::Logger& log;         ///< Receives compile errors.
};

/// @brief Watches shader sources, `.meta` files and includes, and recooks what changed on a job.
///
/// It polls file times instead of using OS change notifications: that's the same code on every platform, and it works
/// with editors that save by replacing a file. A failed compile logs its messages and keeps the old shader, so a typo
/// can't stop a running app.
/// @par Thread safety
/// Call everything on one thread (the main thread). The cook runs on a job, and events arrive at the next
/// EventBus::dispatchQueued().
class ShaderHotReload
{
public:
    /// @brief A hot reloader that watches nothing yet.
    /// @param deps What it uses.
    explicit ShaderHotReload(const ShaderHotReloadDeps& deps);

    /// @brief Waits for a reload in progress.
    ~ShaderHotReload();

    ShaderHotReload(const ShaderHotReload&) = delete;            ///< Not copyable: owns a job.
    ShaderHotReload& operator=(const ShaderHotReload&) = delete; ///< Not copyable: owns a job.

    /// @brief Starts watching a shader, cooking it first if needed, to learn its includes.
    /// @param id The asset; must be registered with the AssetManager.
    /// @return Nothing, or the cook's error; the shader isn't watched then.
    [[nodiscard]] Result<void> watch(AssetId id);

    /// @brief Watches every registered asset.
    /// @return Nothing, or the first error.
    [[nodiscard]] Result<void> watchAll();

    /// @brief Checks the watched files, and starts a reload job if any changed.
    ///
    /// Does nothing while a reload is running; what changes meanwhile is found by a later poll. It reads one timestamp
    /// per watched file, so call it a couple of times per second, not every frame.
    /// @return How many shaders the job will recook.
    std::size_t poll();

    /// @brief Waits until the running reload is finished, if there is one.
    void wait();

private:
    /// @brief The files of one watched shader, and when they last changed.
    struct WatchedShader
    {
        AssetRecord record;
        std::vector<std::string> files;
        std::vector<std::filesystem::file_time_type> times;
    };

    [[nodiscard]] std::filesystem::file_time_type fileTime(const std::string& path) const;
    void setFiles(WatchedShader& shader, const std::vector<std::string>& includes) const;
    void reload(const std::vector<AssetRecord>& changed);

    ShaderHotReloadDeps m_deps;
    std::unordered_map<AssetId, WatchedShader> m_shaders;
    mutable std::mutex m_mutex;                                          ///< Guards m_newIncludes.
    std::unordered_map<AssetId, std::vector<std::string>> m_newIncludes; ///< From the job, applied by the next poll().
    Jobs::Job m_job; ///< Last, so it finishes before the members it uses go away.
};
} // namespace Meow2::Assets
