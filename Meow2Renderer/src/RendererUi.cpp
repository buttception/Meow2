/// @file
/// @brief Meow2Renderer's ImGui windows.

#include "RendererUi.h"

#include "SceneView.h"

#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <numeric>
#include <set>
#include <utility>

namespace Meow2::RendererApp
{
namespace
{
/// @brief The scenes suggested for M6 (milestones.md), in their groups.
struct Suggested
{
    std::string_view group; ///< What they test.
    std::string_view name;  ///< The model's folder name.
};

constexpr std::array<Suggested, 25> c_suggested{{
    {"First light", "Box"},
    {"First light", "BoxTextured"},
    {"First light", "Duck"},
    {"First light", "Avocado"},
    {"Coordinates", "OrientationTest"},
    {"Coordinates", "NegativeScaleTest"},
    {"Coordinates", "TextureCoordinateTest"},
    {"PBR", "MetalRoughSpheres"},
    {"PBR", "MetalRoughSpheresNoTextures"},
    {"PBR", "EnvironmentTest"},
    {"Normal maps", "NormalTangentTest"},
    {"Normal maps", "NormalTangentMirrorTest"},
    {"Textures and colour", "TextureSettingsTest"},
    {"Textures and colour", "TextureTransformTest"},
    {"Textures and colour", "VertexColorTest"},
    {"Transparency", "AlphaBlendModeTest"},
    {"Showcase", "DamagedHelmet"},
    {"Showcase", "FlightHelmet"},
    {"Showcase", "WaterBottle"},
    {"Showcase", "BoomBox"},
    {"Showcase", "Lantern"},
    {"Showcase", "Corset"},
    {"Showcase", "SciFiHelmet"},
    {"Stress", "Sponza"},
    {"Stress", "ABeautifulGame"},
}};

/// @brief Bytes as text with a unit.
std::string formatBytes(std::uint64_t bytes)
{
    constexpr std::array<std::string_view, 4> c_units{"B", "KiB", "MiB", "GiB"};
    auto value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < c_units.size())
    {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", value, c_units[unit]);
}

/// @brief The command line that fetches one sample: cmake running the fetch script.
std::string fetchCommand(const std::string& name)
{
#ifdef _WIN32
    // cmd.exe strips the outer quotes of a command that starts with one, so the whole line is quoted once more.
    return std::format(R"(""{}" -P "{}" {}")", MEOW2_CMAKE_COMMAND, MEOW2_FETCH_SCRIPT, name);
#else
    return std::format(R"("{}" -P "{}" {})", MEOW2_CMAKE_COMMAND, MEOW2_FETCH_SCRIPT, name);
#endif
}
} // namespace

bool isModelName(std::string_view name)
{
    return !name.empty() && name.size() < 128 &&
           std::ranges::all_of(
               name,
               [](char c)
               {
                   return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                          c == '_';
               });
}

std::vector<SampleEntry> listSamples(const std::filesystem::path& sampleRoot)
{
    std::vector<SampleEntry> samples;
    std::set<std::string> listed;
    for (const Suggested& suggested : c_suggested)
    {
        const std::string name{suggested.name};
        samples.push_back(
            {.name = name, .group = std::string{suggested.group}, .downloaded = !findSample(sampleRoot, name).empty()});
        listed.insert(name);
    }
    // Every other model the sparse checkout has downloaded.
    std::vector<std::string> others;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(sampleRoot / "Models", error))
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(error) && !listed.contains(name) && !findSample(sampleRoot, name).empty())
        {
            others.push_back(name);
        }
    }
    std::ranges::sort(others);
    for (std::string& name : others)
    {
        samples.push_back({.name = std::move(name), .group = "Downloaded", .downloaded = true});
    }
    return samples;
}

void FrameTimes::add(float milliseconds)
{
    m_times[m_next] = milliseconds;
    m_next = (m_next + 1) % m_times.size();
    m_count = std::min(m_count + 1, m_times.size());
}

float FrameTimes::average() const
{
    if (m_count == 0)
    {
        return 0.0F;
    }
    const std::array<float, 240> times = ordered();
    return std::accumulate(times.end() - static_cast<std::ptrdiff_t>(m_count), times.end(), 0.0F) /
           static_cast<float>(m_count);
}

float FrameTimes::worst() const
{
    return *std::ranges::max_element(m_times);
}

std::array<float, 240> FrameTimes::ordered() const
{
    std::array<float, 240> ordered{};
    for (std::size_t i = 0; i < m_times.size(); ++i)
    {
        ordered[i] = m_times[(m_next + i) % m_times.size()];
    }
    return ordered;
}

StatsActions drawStatsWindow(const StatsInfo& info)
{
    StatsActions actions;
    ImGui::SetNextWindowPos(ImVec2{10.0F, 10.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{340.0F, 0.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Stats"))
    {
        ImGui::End();
        return actions;
    }
    const float average = info.frames != nullptr ? info.frames->average() : 0.0F;
    ImGui::Text("%.2f ms  (%.0f FPS)", static_cast<double>(average),
                average > 0.0F ? 1000.0 / static_cast<double>(average) : 0.0);
    if (info.frames != nullptr)
    {
        const std::array<float, 240> times = info.frames->ordered();
        const float top = std::max(info.frames->worst(), 1.0F);
        ImGui::PlotLines("##frames", times.data(), static_cast<int>(times.size()), 0, nullptr, 0.0F, top * 1.1F,
                         ImVec2{-1.0F, 50.0F});
        ImGui::Text("Worst %.2f ms over the last %zu frames", static_cast<double>(info.frames->worst()), times.size());
    }

    ImGui::SeparatorText("GPU");
    ImGui::Text("%.*s on %.*s", static_cast<int>(info.backend.size()), info.backend.data(),
                static_cast<int>(info.gpu.size()), info.gpu.data());
    ImGui::Text("%u x %u pixels", info.width, info.height);
    ImGui::Text("Memory: %s of %s budget", formatBytes(info.gpuMemory.usedBytes).c_str(),
                formatBytes(info.gpuMemory.budgetBytes).c_str());
    if (info.validationErrors > 0)
    {
        ImGui::TextColored(ImVec4{1.0F, 0.4F, 0.3F, 1.0F}, "Validation errors: %llu",
                           static_cast<unsigned long long>(info.validationErrors));
    }
    if (ImGui::Button("Switch backend (F10)"))
    {
        actions.switchBackend = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Capture (F11)"))
    {
        actions.capture = true;
    }

    ImGui::SeparatorText("Scene");
    ImGui::Text("%.*s", static_cast<int>(info.scene.size()), info.scene.data());
    ImGui::Text("Entities %zu, instances %u", info.entities, info.instances);
    ImGui::Text("Meshes %u, materials %u, textures %u", info.meshes, info.materials, info.textures);
    if (info.importMilliseconds > 0.0)
    {
        ImGui::Text("Opened in %.0f ms", info.importMilliseconds);
    }

    if (ImGui::CollapsingHeader("Memory"))
    {
        if (ImGui::BeginTable("memory", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Tag");
            ImGui::TableSetupColumn("CPU");
            ImGui::TableSetupColumn("GPU");
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < Memory::c_memTagCount; ++i)
            {
                const auto tag = static_cast<Memory::MemTag>(i);
                const std::size_t cpu = Memory::globalTracker().stats(tag).currentBytes;
                const std::uint64_t gpu = info.gpuMemory.bytesByTag[i];
                if (cpu == 0 && gpu == 0)
                {
                    continue;
                }
                const std::string_view name = Memory::memTagName(tag);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatBytes(cpu).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatBytes(gpu).c_str());
            }
            ImGui::EndTable();
        }
        if constexpr (!Memory::c_memoryTrackingAvailable)
        {
            ImGui::TextDisabled("CPU memory isn't tracked in Release builds.");
        }
    }
    ImGui::End();
    return actions;
}

ScenesWindow::ScenesWindow(std::filesystem::path sampleRoot, Jobs::JobSystem& jobs, Log::Logger& log)
    : m_sampleRoot(std::move(sampleRoot))
    , m_jobs(jobs)
    , m_log(log)
    , m_samples(listSamples(m_sampleRoot))
{
}

ScenesWindow::~ScenesWindow() = default; // m_fetchJob's destructor waits for a running fetch

void ScenesWindow::startFetch(const std::string& name)
{
    if (!isModelName(name) || !m_fetching.empty())
    {
        return;
    }
    m_fetching = name;
    m_fetchResult = std::make_shared<std::atomic<int>>(-1);
    // The script prints its progress to the console; the job only waits for it. A command from fixed paths and a
    // checked model name, so nothing in it comes from outside.
    m_fetchJob = m_jobs.run([command = fetchCommand(name), result = m_fetchResult]
                            { result->store(std::system(command.c_str())); });
    MEOW2_LOG_INFO(m_log, "Fetching the {} sample…", name);
}

std::optional<SceneRequest> ScenesWindow::draw(std::string_view current, std::string_view error)
{
    std::optional<SceneRequest> request;

    // A finished fetch: refresh the list, and open the model if that's what the user asked for.
    if (!m_fetching.empty() && m_fetchJob.done())
    {
        const int code = m_fetchResult->load();
        if (code == 0)
        {
            MEOW2_LOG_INFO(m_log, "Fetched {}", m_fetching);
            m_samples = listSamples(m_sampleRoot);
            if (m_openAfterFetch == m_fetching)
            {
                request = SceneRequest{.file = findSample(m_sampleRoot, m_fetching)};
            }
        }
        else
        {
            MEOW2_LOG_ERROR(m_log, "Fetching {} failed (exit code {}); see the console", m_fetching, code);
        }
        m_fetching.clear();
        m_openAfterFetch.clear();
    }

    // Top right, so it never covers the stats window on the left.
    ImGui::SetNextWindowPos(ImVec2{std::max(ImGui::GetIO().DisplaySize.x - 350.0F, 360.0F), 10.0F},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{340.0F, 420.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scenes"))
    {
        ImGui::End();
        return request;
    }
    ImGui::TextWrapped("Open: %.*s", static_cast<int>(current.size()), current.data());
    if (!error.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0F, 0.4F, 0.3F, 1.0F});
        ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        ImGui::PopStyleColor();
    }

    // The sample list, by group. A grey name isn't downloaded yet: opening it fetches it first.
    if (ImGui::BeginListBox("##samples", ImVec2{-1.0F, ImGui::GetTextLineHeightWithSpacing() * 14.0F}))
    {
        std::string_view group;
        for (std::size_t i = 0; i < m_samples.size(); ++i)
        {
            const SampleEntry& sample = m_samples[i];
            if (sample.group != group)
            {
                group = sample.group;
                ImGui::SeparatorText(sample.group.c_str());
            }
            if (!sample.downloaded)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            const bool open = sample.name + ".gltf" == current || sample.name + ".glb" == current;
            std::string label = sample.name;
            if (open)
            {
                label += "  (open)";
            }
            if (ImGui::Selectable(label.c_str(), m_selected == i, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && sample.downloaded)
                {
                    request = SceneRequest{.file = findSample(m_sampleRoot, sample.name)};
                }
            }
            if (!sample.downloaded)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndListBox();
    }

    if (m_selected < m_samples.size())
    {
        const SampleEntry& selected = m_samples[m_selected];
        if (selected.downloaded)
        {
            if (ImGui::Button("Open"))
            {
                request = SceneRequest{.file = findSample(m_sampleRoot, selected.name)};
            }
        }
        else
        {
            ImGui::BeginDisabled(!m_fetching.empty());
            if (ImGui::Button("Fetch and open"))
            {
                m_openAfterFetch = selected.name;
                startFetch(selected.name);
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cube grid"))
        {
            request = SceneRequest{};
        }
    }
    if (!m_fetching.empty())
    {
        ImGui::Text("Fetching %s…", m_fetching.c_str());
    }

    ImGui::SeparatorText("Any file");
    ImGui::SetNextItemWidth(-60.0F);
    const bool entered = ImGui::InputTextWithHint("##path", ".gltf or .glb path", m_path.data(), m_path.size(),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Open##path") || entered) && m_path[0] != '\0')
    {
        // UTF-8 from ImGui: as a char8_t string, so non-ASCII paths work on Windows too.
        request = SceneRequest{.file = std::filesystem::path{reinterpret_cast<const char8_t*>(m_path.data())}};
    }
    ImGui::TextDisabled("Samples: %s", m_sampleRoot.string().c_str());
    ImGui::End();
    return request;
}
} // namespace Meow2::RendererApp
