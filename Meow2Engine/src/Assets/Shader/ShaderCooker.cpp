/// @file
/// @brief ShaderCooker: the cache key, and compiling every combination as jobs.

#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderCooker.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace Meow2::Assets
{
namespace
{
/// @brief FNV-1a, 64 bits: small and good enough to notice a changed file. Not a security hash.
class Hasher
{
public:
    void add(std::string_view text)
    {
        for (const char c : text)
        {
            m_hash = (m_hash ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
        }
        // A separator, so "ab" + "c" and "a" + "bc" differ.
        m_hash = (m_hash ^ 0xFFU) * 0x100000001B3ULL;
    }

    [[nodiscard]] std::uint64_t value() const
    {
        return m_hash;
    }

private:
    std::uint64_t m_hash = 0xCBF29CE484222325ULL;
};

/// @brief One compile of the cook.
struct CookTask
{
    std::uint32_t variant = 0;
    std::uint32_t entry = 0;
    Shader::ShaderTarget target = Shader::ShaderTarget::Spirv;
};
} // namespace

std::span<const Shader::ShaderTarget> defaultShaderTargets()
{
#if defined(_WIN32)
    static constexpr std::array c_targets{Shader::ShaderTarget::Dxil, Shader::ShaderTarget::Spirv};
#else
    static constexpr std::array c_targets{Shader::ShaderTarget::Spirv};
#endif
    return c_targets;
}

ShaderCooker::ShaderCooker(FileSystem::FileSystem& files, const Shader::ShaderCompiler& compiler, Jobs::JobSystem& jobs,
                           Log::Logger& log)
    : m_files(files)
    , m_compiler(compiler)
    , m_jobs(jobs)
    , m_log(log)
{
}

Result<ShaderCookResult> ShaderCooker::cook(const AssetRecord& record, const ShaderCookOptions& options)
{
    MEOW2_PROFILE_ZONE();
    Result<std::string> metaText = m_files.readText(record.metaPath);
    if (!metaText)
    {
        return Unexpected<Error>{metaText.error()};
    }
    Result<std::string> source = m_files.readText(record.sourcePath);
    if (!source)
    {
        return Unexpected<Error>{source.error()};
    }
    Result<ShaderMeta> meta = parseShaderMeta(*metaText, record.metaPath);
    if (!meta)
    {
        return Unexpected<Error>{meta.error()};
    }

    // Everything the bytecode depends on. A missing include hashes differently, so its shader recooks and reports it.
    const auto cacheKey = [&](std::span<const std::string> dependencies)
    {
        Hasher hasher;
        hasher.add(m_compiler.version());
        hasher.add(std::to_string(c_shaderFileVersion));
        hasher.add(std::to_string(static_cast<int>(options.optimization)));
        for (const Shader::ShaderTarget target : options.targets)
        {
            hasher.add(std::to_string(static_cast<int>(target)));
        }
        hasher.add(*metaText);
        hasher.add(*source);
        for (const std::string& dependency : dependencies)
        {
            hasher.add(dependency);
            Result<std::string> text = m_files.readText(dependency);
            hasher.add(text ? std::string_view{*text} : std::string_view{"<missing>"});
        }
        return hasher.value();
    };

    const std::string outputPath = cookedShaderPath(record.id);
    if (!options.force && m_files.exists(outputPath))
    {
        // An unreadable or old-format file is simply not up to date.
        Result<std::vector<std::byte>> bytes = m_files.readFile(outputPath);
        if (bytes)
        {
            const Result<CookedShader> previous = deserializeShader(*bytes);
            if (previous && previous->cacheKey == cacheKey(previous->dependencies))
            {
                return ShaderCookResult{true, 0, previous->dependencies};
            }
        }
    }

    const std::uint64_t variants = variantCount(*meta);
    if (variants > c_shaderVariantWarning)
    {
        MEOW2_LOG_WARN(m_log, "{}: {} variants; every option multiplies the count", record.sourcePath, variants);
    }

    std::vector<CookTask> tasks;
    tasks.reserve(static_cast<std::size_t>(variants) * meta->entries.size() * options.targets.size());
    for (std::uint32_t variant = 0; variant < variants; ++variant)
    {
        for (std::uint32_t entry = 0; entry < meta->entries.size(); ++entry)
        {
            for (const Shader::ShaderTarget target : options.targets)
            {
                tasks.push_back({variant, entry, target});
            }
        }
    }

    // One result per task: each job writes only its own slots, so there's nothing to lock.
    std::vector<std::optional<Result<Shader::ShaderCompileOutput>>> results(tasks.size());
    m_jobs.wait(m_jobs.parallelFor(static_cast<std::uint32_t>(tasks.size()), 1,
                                   [&](std::uint32_t begin, std::uint32_t end, std::uint32_t /*threadIndex*/)
                                   {
                                       for (std::uint32_t i = begin; i < end; ++i)
                                       {
                                           const CookTask& task = tasks[i];
                                           const ShaderEntry& entry = meta->entries[task.entry];
                                           const std::vector<Shader::ShaderDefine> defines =
                                               variantDefines(*meta, {task.variant});
                                           results[i] = m_compiler.compile({
                                               .path = record.sourcePath,
                                               .entryPoint = entry.name,
                                               .stage = entry.stage,
                                               .target = task.target,
                                               .optimization = options.optimization,
                                               .defines = defines,
                                           });
                                       }
                                   }));

    CookedShader cooked;
    cooked.metaText = *metaText;
    cooked.blobs.reserve(tasks.size());
    for (std::size_t i = 0; i < tasks.size(); ++i)
    {
        Result<Shader::ShaderCompileOutput>& result = *results[i];
        if (!result)
        {
            return Unexpected<Error>{result.error()};
        }
        if (!result->warnings.empty())
        {
            MEOW2_LOG_WARN(m_log, "{}", result->warnings);
        }
        cooked.dependencies.insert(cooked.dependencies.end(), result->includes.begin(), result->includes.end());
        cooked.blobs.push_back({tasks[i].variant, tasks[i].entry, tasks[i].target, std::move(result->bytecode)});
    }
    std::sort(cooked.dependencies.begin(), cooked.dependencies.end());
    cooked.dependencies.erase(std::unique(cooked.dependencies.begin(), cooked.dependencies.end()),
                              cooked.dependencies.end());
    cooked.cacheKey = cacheKey(cooked.dependencies);

    Result<void> written = m_files.writeFile(outputPath, serializeShader(cooked));
    if (!written)
    {
        return Unexpected<Error>{written.error()};
    }
    return ShaderCookResult{false, static_cast<std::uint32_t>(tasks.size()), std::move(cooked.dependencies)};
}
} // namespace Meow2::Assets
