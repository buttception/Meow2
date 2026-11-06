/// @file
/// @brief Meow2ShaderCompiler: cooks every shader asset of a folder. Run by the build (`meow2_add_shaders()`).
///
///     Meow2ShaderCompiler --source <folder> --output <folder> [--debug] [--force]
///     Meow2ShaderCompiler --source <folder> --import <file>
///
/// `--import` writes a `.meta` with a new AssetId next to a source file; add its `[[entry]]` by hand. The exit code is
/// 0 on success, and 1 with the compiler's messages (`shaders/Sky.hlsl:12:5: error: …`) on stderr otherwise.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderCooker.h>
#include <Meow2/Core/CpuFeatures.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace fs = std::filesystem;

/// @brief The command line. Plain data.
struct Options
{
    fs::path source;        ///< Folder of the shader sources, mounted as `shaders`.
    fs::path output;        ///< Folder of the cooked files.
    std::string importPath; ///< File to import, relative to `source`.
    bool debug = false;     ///< Cook with debug info, without optimisation.
    bool force = false;     ///< Cook even what's up to date.
};

std::optional<Options> parseArguments(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--source" && hasValue)
        {
            options.source = argv[++i];
        }
        else if (argument == "--output" && hasValue)
        {
            options.output = argv[++i];
        }
        else if (argument == "--import" && hasValue)
        {
            options.importPath = argv[++i];
        }
        else if (argument == "--debug")
        {
            options.debug = true;
        }
        else if (argument == "--force")
        {
            options.force = true;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (options.source.empty() || (options.output.empty() && options.importPath.empty()))
    {
        return std::nullopt;
    }
    return options;
}

int reportError(const Meow2::Error& error)
{
    std::fputs(std::format("{}\n", error.message).c_str(), stderr);
    return 1;
}

/// @brief Cooks every asset the scan found.
int cookAll(const Options& options, Meow2::FileSystem::FileSystem& files, Meow2::Assets::AssetManager& assets,
            Meow2::Log::Logger& log)
{
    using FileSystemAccess = Meow2::FileSystem::MountAccess;
    std::error_code ignored;
    fs::create_directories(options.output, ignored);
    const Meow2::Result<void> mounted = files.mountDirectory("cooked", options.output, FileSystemAccess::ReadWrite);
    if (!mounted)
    {
        return reportError(mounted.error());
    }
    Meow2::Result<Meow2::Shader::ShaderCompiler> compiler = Meow2::Shader::ShaderCompiler::create(files);
    if (!compiler)
    {
        return reportError(compiler.error());
    }
    Meow2::Jobs::JobSystem jobs{{}, log};
    Meow2::Assets::ShaderCooker cooker{files, *compiler, jobs, log};

    // Sorted, so the build log reads the same on every run.
    std::vector<Meow2::Assets::AssetRecord> records = assets.records();
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.sourcePath < b.sourcePath; });

    Meow2::Assets::ShaderCookOptions cookOptions;
    cookOptions.optimization =
        options.debug ? Meow2::Shader::ShaderOptimization::Debug : Meow2::Shader::ShaderOptimization::Release;
    cookOptions.force = options.force;

    std::uint32_t cookedShaders = 0;
    std::uint32_t compiled = 0;
    for (const Meow2::Assets::AssetRecord& record : records)
    {
        const Meow2::Result<Meow2::Assets::ShaderCookResult> result = cooker.cook(record, cookOptions);
        if (!result)
        {
            return reportError(result.error());
        }
        cookedShaders += result->upToDate ? 0U : 1U;
        compiled += result->compiled;
    }
    std::fputs(std::format("Meow2ShaderCompiler: {} shaders, {} recooked, {} compiles\n", records.size(), cookedShaders,
                           compiled)
                   .c_str(),
               stdout);
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (!Meow2::checkCpuFeatures("Meow2ShaderCompiler"))
    {
        return 1;
    }
    const std::optional<Options> options = parseArguments(argc, argv);
    if (!options)
    {
        std::fputs("usage: Meow2ShaderCompiler --source <folder> --output <folder> [--debug] [--force]\n"
                   "       Meow2ShaderCompiler --source <folder> --import <file>\n",
                   stderr);
        return 2;
    }

    // Composition root (techstack.md §18). Logs go next to the cooked files; stdout stays one line per run.
    const fs::path logDirectory =
        options->output.empty() ? fs::temp_directory_path() / "meow2-shader-compiler" : options->output / "logs";
    Meow2::Log::LogSystem logSystem{{.appName = "ShaderCompiler", .directory = logDirectory, .console = false}};
    Meow2::Log::Logger log = logSystem.createLogger("Engine");
    Meow2::FileSystem::FileSystem files{log};
    Meow2::UuidGenerator ids;
    Meow2::Assets::AssetManager assets{files, ids, log};

    const Meow2::Result<void> mounted =
        files.mountDirectory("shaders", options->source, Meow2::FileSystem::MountAccess::ReadWrite);
    if (!mounted)
    {
        return reportError(mounted.error());
    }

    if (!options->importPath.empty())
    {
        const Meow2::Result<Meow2::AssetId> id = assets.importSource(std::format("shaders/{}", options->importPath));
        if (!id)
        {
            return reportError(id.error());
        }
        std::fputs(std::format("{}\n", id->uuid()).c_str(), stdout);
        return 0;
    }

    const Meow2::Result<std::size_t> found = assets.scan("shaders");
    if (!found)
    {
        return reportError(found.error());
    }
    return cookAll(*options, files, assets, log);
}
