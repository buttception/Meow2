/// @file
/// @brief PipelineLibrary: pipelines built from shader assets, rebuilt when a shader is hot reloaded.

#pragma once

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderHotReload.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Renderer/RenderDevice.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Meow2::Renderer
{
/// @brief A graphics pipeline to build from a shader asset. Plain data.
struct GraphicsPipelineRequest
{
    AssetId shader{};                                     ///< The shader.
    std::vector<Assets::ShaderOptionChoice> options{};    ///< The variant; unset options use their default.
    std::string vertexEntry = "vsMain";                   ///< Vertex entry point.
    std::string pixelEntry = "psMain";                    ///< Pixel entry point.
    Rhi::RhiFormat colorFormat = Rhi::RhiFormat::Unknown; ///< Format of the colour target.
    Rhi::RhiFormat depthFormat = Rhi::RhiFormat::Unknown; ///< Format of the depth target; Unknown for none.
    bool depthWrite = true;                               ///< Whether drawing updates depth.
    Rhi::CullMode cull = Rhi::CullMode::None;             ///< Which triangles to skip.
    Rhi::BlendMode blend = Rhi::BlendMode::None;          ///< How output combines with the target.
    Rhi::FillMode fill = Rhi::FillMode::Solid;            ///< Filled triangles, or only their edges (debug views).
    std::string name = "Pipeline";                        ///< Shown in debugging tools.

    /// @brief Requests are equal when every field is.
    friend bool operator==(const GraphicsPipelineRequest&, const GraphicsPipelineRequest&) = default;
};

/// @brief A compute pipeline to build from a shader asset. Plain data.
struct ComputePipelineRequest
{
    AssetId shader{};                                  ///< The shader.
    std::vector<Assets::ShaderOptionChoice> options{}; ///< The variant; unset options use their default.
    std::string entry = "csMain";                      ///< Entry point.
    std::string name = "Pipeline";                     ///< Shown in debugging tools.

    /// @brief Requests are equal when every field is.
    friend bool operator==(const ComputePipelineRequest&, const ComputePipelineRequest&) = default;
};

/// @brief A pipeline in a PipelineLibrary. Stays valid across rebuilds, unlike the device's handle behind it.
struct PipelineId
{
    std::uint32_t index = 0; ///< Position in the library.
};

/// @brief What PipelineLibrary uses. Plain data; everything must outlive it.
struct PipelineLibraryDeps
{
    RenderDevice& device;         ///< Creates the pipelines, on whichever backend is running.
    Assets::AssetManager& assets; ///< Holds the shaders.
    Events::EventBus& events;     ///< Sends Assets::ShaderReloaded.
    Log::Logger& log;             ///< Receives rebuild errors.
};

/// @brief Builds pipelines from cooked shaders and keeps them up to date.
///
/// A pipeline is built from bytecode, so a reloaded shader changes nothing until its pipelines are built again. The
/// library remembers each request, and on Assets::ShaderReloaded it rebuilds everything that uses that shader. A
/// rebuild that fails logs the error and keeps the old pipeline, so a running app never loses a pass. Callers keep a
/// PipelineId and ask for the current handle each frame.
///
/// It's a RenderDevice user: when the backend changes it builds every pipeline again from its request, with that
/// backend's bytecode.
/// @par Thread safety
/// Not thread-safe. Use on the thread that dispatches events.
class PipelineLibrary
{
public:
    /// @brief An empty library, listening for reloads.
    /// @param deps What it uses.
    explicit PipelineLibrary(const PipelineLibraryDeps& deps);

    /// @brief Destroys its pipelines.
    ~PipelineLibrary();

    PipelineLibrary(const PipelineLibrary&) = delete;            ///< Not copyable: owns pipelines.
    PipelineLibrary& operator=(const PipelineLibrary&) = delete; ///< Not copyable: owns pipelines.

    /// @brief Builds a graphics pipeline, or finds the one an identical earlier request built.
    /// @param request What to build.
    /// @return Its ID, or the error from loading the shader or creating the pipeline.
    [[nodiscard]] Result<PipelineId> add(GraphicsPipelineRequest request);

    /// @brief Builds a compute pipeline, or finds the one an identical earlier request built.
    /// @param request What to build.
    /// @return Its ID, or the error from loading the shader or creating the pipeline.
    [[nodiscard]] Result<PipelineId> add(ComputePipelineRequest request);

    /// @brief The current pipeline.
    /// @param id The ID.
    /// @return The device's handle, for IRhiCommandList::setPipeline(). Fetch it each frame: rebuilds change it.
    /// @pre `id` came from this library (asserted).
    [[nodiscard]] Rhi::PipelineHandle get(PipelineId id) const;

    /// @brief Rebuilds everything that uses a shader. Runs by itself on Assets::ShaderReloaded.
    /// @param shader The shader.
    void rebuild(AssetId shader);

private:
    /// @brief One pipeline and how to build it again.
    struct Entry
    {
        bool compute = false;                  ///< Which request applies.
        GraphicsPipelineRequest graphics;      ///< The request, for graphics pipelines.
        ComputePipelineRequest computeRequest; ///< The request, for compute pipelines.
        Rhi::PipelineHandle handle;            ///< The current pipeline.
    };

    /// @brief Builds an entry's pipeline.
    /// @param entry The entry.
    /// @return The new pipeline.
    [[nodiscard]] Result<Rhi::PipelineHandle> build(const Entry& entry);

    PipelineLibraryDeps m_deps;
    std::vector<Entry> m_entries;
    DeviceUser m_deviceUser;         ///< Releases and rebuilds the pipelines when the backend changes.
    Events::Subscription m_onReload; ///< Last: it stops calling back before the members it uses go away.
};
} // namespace Meow2::Renderer
