#pragma once

/// @file
/// @brief ShaderCooker: turns a shader's source and `.meta` into a `.m2shader`, skipping work that's already done.
/// @code
/// Meow2::Assets::ShaderCooker cooker{files, compiler, jobs, log};
/// Meow2::Result<Meow2::Assets::ShaderCookResult> cooked = cooker.cook(*assets.find(id), {});
/// @endcode
/// The cached result is reused when nothing it came from changed: the source, the `.meta`, every included file, the
/// compile options and the compiler version all feed its cache key.
/// @see techstack.md §12, docs/wiki/Shaders.md

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Meow2::FileSystem
{
class FileSystem;
}

namespace Meow2::Jobs
{
class JobSystem;
}

namespace Meow2::Log
{
class Logger;
}

namespace Meow2::Assets
{
/// @brief The targets a platform cooks by default: DXIL and SPIR-V on Windows, SPIR-V elsewhere (D3D12 is
/// Windows-only).
/// @return The targets.
[[nodiscard]] std::span<const Shader::ShaderTarget> defaultShaderTargets();

/// @brief How to cook. Plain data.
struct ShaderCookOptions
{
    Shader::ShaderOptimization optimization = Shader::ShaderOptimization::Release; ///< Debuggability against speed.
    std::span<const Shader::ShaderTarget> targets = defaultShaderTargets();        ///< What to compile for.
    bool force = false;                                                            ///< Cook even if up to date.
};

/// @brief What a cook did. Plain data.
struct ShaderCookResult
{
    bool upToDate = false;             ///< The cached file was reused; nothing was compiled.
    std::uint32_t compiled = 0;        ///< How many entry point × variant × target combinations were compiled.
    std::vector<std::string> includes; ///< Virtual paths of every included file.
};

/// @brief Compiles shader assets into the `cooked` mount, which must be writable.
/// @par Thread safety
/// Not thread-safe: cook one shader at a time, from a job thread (each cook runs its compiles as jobs).
/// @par Lifetime
/// Everything passed to the constructor must outlive it.
class ShaderCooker
{
public:
    /// @brief A cooker.
    /// @param files    Reads sources, writes cooked files. Must outlive the cooker.
    /// @param compiler Compiles. Must outlive the cooker.
    /// @param jobs     Runs the compiles in parallel. Must outlive the cooker.
    /// @param log      Receives warnings. Must outlive the cooker.
    ShaderCooker(FileSystem::FileSystem& files, const Shader::ShaderCompiler& compiler, Jobs::JobSystem& jobs,
                 Log::Logger& log);

    /// @brief Cooks a shader, or does nothing if its cooked file is up to date.
    /// @param record  The asset.
    /// @param options How to cook.
    /// @return What happened; a compile error with file and line; nothing is written on failure, so the previous cooked
    ///         file stays.
    [[nodiscard]] Result<ShaderCookResult> cook(const AssetRecord& record, const ShaderCookOptions& options);

private:
    FileSystem::FileSystem& m_files;
    const Shader::ShaderCompiler& m_compiler;
    Jobs::JobSystem& m_jobs;
    Log::Logger& m_log;
};
} // namespace Meow2::Assets
