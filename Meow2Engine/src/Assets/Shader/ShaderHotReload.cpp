/// @file
/// @brief ShaderHotReload: file-time polling, and the reload job.

#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderCooker.h>
#include <Meow2/Assets/Shader/ShaderHotReload.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Profiling/Profile.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <utility>

namespace Meow2::Assets
{
ShaderHotReload::ShaderHotReload(const ShaderHotReloadDeps& deps)
    : m_deps(deps)
{
}

ShaderHotReload::~ShaderHotReload() = default;

std::filesystem::file_time_type ShaderHotReload::fileTime(const std::string& path) const
{
    const Result<std::filesystem::path> native = m_deps.assets.files().nativePath(path);
    if (!native)
    {
        return {};
    }
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(*native, error);
    return error ? std::filesystem::file_time_type{} : time; // a deleted file counts as a change too
}

void ShaderHotReload::setFiles(WatchedShader& shader, const std::vector<std::string>& includes) const
{
    shader.files = {shader.record.sourcePath, shader.record.metaPath};
    shader.files.insert(shader.files.end(), includes.begin(), includes.end());
    shader.times.clear();
    for (const std::string& file : shader.files)
    {
        shader.times.push_back(fileTime(file));
    }
}

Result<void> ShaderHotReload::watch(AssetId id)
{
    const std::optional<AssetRecord> record = m_deps.assets.find(id);
    if (!record)
    {
        return fail(ErrorCode::NotFound, id.uuid().toString());
    }
    const Result<ShaderCookResult> cooked = m_deps.cooker.cook(*record, {});
    if (!cooked)
    {
        return Unexpected<Error>{cooked.error()};
    }
    WatchedShader shader{*record, {}, {}};
    setFiles(shader, cooked->includes);
    m_shaders.insert_or_assign(id, std::move(shader));
    return {};
}

Result<void> ShaderHotReload::watchAll()
{
    for (const AssetRecord& record : m_deps.assets.records())
    {
        Result<void> watched = watch(record.id);
        if (!watched)
        {
            return watched;
        }
    }
    return {};
}

std::size_t ShaderHotReload::poll()
{
    MEOW2_PROFILE_ZONE();
    if (!m_job.done())
    {
        return 0;
    }
    {
        // The includes the last reload found: a shader may have gained or lost some.
        const std::scoped_lock lock{m_mutex};
        for (const auto& [id, includes] : m_newIncludes)
        {
            const auto watched = m_shaders.find(id);
            if (watched != m_shaders.end())
            {
                setFiles(watched->second, includes);
            }
        }
        m_newIncludes.clear();
    }

    std::vector<AssetRecord> changed;
    for (auto& [id, shader] : m_shaders)
    {
        bool isChanged = false;
        for (std::size_t i = 0; i < shader.files.size(); ++i)
        {
            const std::filesystem::file_time_type time = fileTime(shader.files[i]);
            if (time != shader.times[i])
            {
                shader.times[i] = time;
                isChanged = true;
            }
        }
        if (isChanged)
        {
            changed.push_back(shader.record);
        }
    }
    if (!changed.empty())
    {
        m_job = m_deps.jobs.run([this, changed] { reload(changed); });
    }
    return changed.size();
}

void ShaderHotReload::reload(const std::vector<AssetRecord>& changed)
{
    MEOW2_PROFILE_ZONE();
    for (const AssetRecord& record : changed)
    {
        Result<ShaderCookResult> cooked = m_deps.cooker.cook(record, {});
        if (!cooked)
        {
            MEOW2_LOG_ERROR(m_deps.log, "Hot reload: {}", cooked.error().message);
            m_deps.events.enqueue(ShaderReloadFailed{record.id, cooked.error().message});
            continue;
        }
        {
            const std::scoped_lock lock{m_mutex};
            m_newIncludes.insert_or_assign(record.id, cooked->includes);
        }
        const Result<void> reloaded = m_deps.assets.reload<ShaderAsset>(record.id);
        if (!reloaded)
        {
            MEOW2_LOG_ERROR(m_deps.log, "Hot reload: {}", reloaded.error().message);
            m_deps.events.enqueue(ShaderReloadFailed{record.id, reloaded.error().message});
            continue;
        }
        m_deps.events.enqueue(ShaderReloaded{record.id});
    }
}

void ShaderHotReload::wait()
{
    m_deps.jobs.wait(m_job);
}
} // namespace Meow2::Assets
