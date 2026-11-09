/// @file
/// @brief Meow2Renderer's ImGui windows: the scene picker and the stats.

#pragma once

#include "UiTheme.h"

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Rhi/RhiTypes.h>
#include <Meow2/Math/Types.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief One Khronos sample in the scene picker. Plain data.
struct SampleEntry
{
    std::string name{};      ///< The model's folder name, e.g. `DamagedHelmet`.
    std::string group{};     ///< What it tests ("First light", "PBR", …), or "Downloaded" for others.
    bool downloaded = false; ///< Whether its glTF file is on disk.
    bool hasGltf = false;    ///< `glTF/<name>.gltf` is on disk (with its buffers and images).
    bool hasGlb = false;     ///< `glTF-Binary/<name>.glb` is on disk.
};

/// @brief The suggested M6 samples in their groups, then every other downloaded model in alphabetical order.
/// @param sampleRoot `assets/gltf-sample-assets`.
/// @return The list.
[[nodiscard]] std::vector<SampleEntry> listSamples(const std::filesystem::path& sampleRoot);

/// @brief Whether a name is a plain model folder name (letters, digits, `-`, `_`), safe to pass to the fetch script.
/// @param name The name.
/// @return True if it is.
[[nodiscard]] bool isModelName(std::string_view name);

/// @brief A scene the user picked. Plain data.
struct SceneRequest
{
    std::filesystem::path file{}; ///< A `.gltf`/`.glb`; empty for the cube grid.
};

/// @brief The last few seconds of frame times, for the stats window.
/// @par Thread safety
/// Not thread-safe.
class FrameTimes
{
public:
    /// @brief Adds a frame.
    /// @param milliseconds How long it took.
    void add(float milliseconds);

    /// @brief The average of the kept frames.
    /// @return Milliseconds; 0 before the first frame.
    [[nodiscard]] float average() const;

    /// @brief The slowest kept frame.
    /// @return Milliseconds.
    [[nodiscard]] float worst() const;

    /// @brief The kept frames in order, oldest first, for a graph.
    /// @return A copy.
    [[nodiscard]] std::array<float, 240> ordered() const;

private:
    std::array<float, 240> m_times{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};

/// @brief What the stats window shows. Plain data, gathered by the app each frame.
struct StatsInfo
{
    std::string_view backend{};         ///< "Vulkan" or "D3D12".
    std::string_view gpu{};             ///< The adapter's name.
    const FrameTimes* frames = nullptr; ///< Frame times.
    std::uint32_t width = 0;            ///< Swapchain width in pixels.
    std::uint32_t height = 0;           ///< Swapchain height in pixels.
    std::string_view scene{};           ///< The open scene's name.
    std::size_t entities = 0;           ///< Entities in the scene.
    std::uint32_t instances = 0;        ///< Instances submitted (before culling).
    std::uint32_t meshes = 0;           ///< GPU meshes.
    std::uint32_t materials = 0;        ///< Materials.
    std::uint32_t textures = 0;         ///< Textures.
    double importMilliseconds = 0.0;    ///< How long opening the scene took.
    Rhi::GpuMemoryStats gpuMemory{};    ///< GPU memory.
    std::uint64_t validationErrors = 0; ///< From the API's validation layer.
};

/// @brief Buttons in the stats window the app acts on. Plain data.
struct StatsActions
{
    bool switchBackend = false; ///< The same as F10.
    bool capture = false;       ///< The same as F11.
};

/// @brief The sun, as the stats window's sliders set it. Plain data.
struct SunControls
{
    float elevationDegrees = 35.0F; ///< Above the horizon: -10 (just set) to 90 (overhead).
    float azimuthDegrees = 30.0F;   ///< Around +Y from +Z (where glTF models face), towards +X.
    float intensity = 3.0F;         ///< The sun's illuminance in the scene's units.
    float exposure = 1.0F;          ///< Scales the final colour before the tone curve.
};

/// @brief The direction towards the sun for some controls.
/// @param sun The controls.
/// @return A unit vector, world space (+Y up, +Z forward).
[[nodiscard]] Math::Vec3 sunDirection(const SunControls& sun);

/// @brief Draws the stats window.
/// @param info  What to show.
/// @param fonts The theme's fonts: numbers are drawn in the monospace one.
/// @param sun   The sun's sliders, edited in place; null to leave them out.
/// @return The buttons pressed this frame.
StatsActions drawStatsWindow(const StatsInfo& info, const UiFonts& fonts = {}, SunControls* sun = nullptr);

/// @brief The scene picker: Khronos samples (fetching ones that aren't downloaded) and any file by path.
/// @par Thread safety
/// Not thread-safe; use on the main thread. A fetch runs on a job and only touches the atomic status.
class ScenesWindow
{
public:
    /// @brief An empty picker.
    /// @param sampleRoot Where the samples are (renderer.sampleAssets).
    /// @param jobs       Runs fetches. Must outlive the window.
    /// @param log        Receives fetch results. Must outlive the window.
    ScenesWindow(std::filesystem::path sampleRoot, Jobs::JobSystem& jobs, Log::Logger& log);

    /// @brief Waits for a running fetch.
    ~ScenesWindow();

    ScenesWindow(const ScenesWindow&) = delete;            ///< Not copyable: owns a job.
    ScenesWindow& operator=(const ScenesWindow&) = delete; ///< Not copyable: owns a job.

    /// @brief Draws the window.
    /// @param current The open scene's name, to highlight it.
    /// @param error   Why the last open failed, or empty.
    /// @return A scene to open, when the user asked for one.
    [[nodiscard]] std::optional<SceneRequest> draw(std::string_view current, std::string_view error,
                                                   const UiFonts& fonts = {});

private:
    /// @brief Starts fetching a sample on a job.
    /// @param name The model's folder name; checked with isModelName().
    void startFetch(const std::string& name);

    std::filesystem::path m_sampleRoot;
    Jobs::JobSystem& m_jobs;
    Log::Logger& m_log;
    std::vector<SampleEntry> m_samples;
    std::size_t m_selected = 0;
    std::array<char, 1024> m_path{};
    std::string m_fetching;                          ///< The model being fetched; empty when idle.
    std::shared_ptr<std::atomic<int>> m_fetchResult; ///< The fetch's exit code; -1 while it runs.
    Jobs::Job m_fetchJob; ///< Last: waited for first, before the members its job uses go away.
};
} // namespace Meow2::RendererApp
