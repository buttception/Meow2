/// @file
/// @brief TextureCache (TextureCache.h).

#include "Meow2/Assets/Texture/TextureCache.h"

#include "Meow2/Platform/FileSystem/FileSystem.h"

#include <format>
#include <utility>

namespace Meow2::Assets
{
TextureCache::TextureCache(FileSystem::FileSystem& files, std::string folder, Log::Logger& log)
    : m_files(files)
    , m_folder(std::move(folder))
    , m_log(log)
{
}

std::string TextureCache::pathOf(const Image& image, TextureKind kind, bool compress) const
{
    return std::format("{}/{}.m2texture", m_folder, textureCacheKey(image, kind, compress));
}

Result<CookedTexture> TextureCache::cook(const Image& image, TextureKind kind, bool compress, Jobs::JobSystem* jobs)
{
    const std::string path = pathOf(image, kind, compress);
    if (m_files.exists(path))
    {
        if (Result<std::vector<std::byte>> bytes = m_files.readFile(path); bytes)
        {
            if (Result<CookedTexture> cached = decodeM2Texture(*bytes); cached)
            {
                return cached;
            }
            else
            {
                MEOW2_LOG_WARN(m_log, "Cached texture {} is damaged, cooking it again: {}", path, cached.error());
            }
        }
        else
        {
            MEOW2_LOG_WARN(m_log, "Cached texture {} can't be read, cooking it again: {}", path, bytes.error());
        }
    }
    Result<CookedTexture> cooked = cookTexture(image, kind, compress, jobs);
    if (!cooked)
    {
        return cooked;
    }
    if (Result<void> written = m_files.writeFile(path, encodeM2Texture(*cooked)); !written)
    {
        MEOW2_LOG_WARN(m_log, "Cooked texture not cached at {}: {}", path, written.error());
    }
    return cooked;
}
} // namespace Meow2::Assets
