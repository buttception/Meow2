/// @file
/// @brief RenderDocCapture: the only file that includes RenderDoc's header.

#include "Meow2/Graphics/Rhi/RenderDocCapture.h"

#include "Meow2/Core/Platform.h"

#include <renderdoc_app.h>

#include <string_view>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
#if MEOW2_PLATFORM_WINDOWS
constexpr std::string_view c_libraryName = "renderdoc.dll";
#else
constexpr std::string_view c_libraryName = "librenderdoc.so";
#endif

/// @brief The API table, in its real type.
/// @param api The stored pointer.
/// @return The table.
const RENDERDOC_API_1_6_0& table(const void* api)
{
    return *static_cast<const RENDERDOC_API_1_6_0*>(api);
}
} // namespace

RenderDocCapture::RenderDocCapture(Log::Logger& log)
{
    Result<SharedLibrary> library = SharedLibrary::findLoaded(c_libraryName);
    if (!library)
    {
        MEOW2_LOG_DEBUG(log, "RenderDoc isn't attached");
        return;
    }
    const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(library->symbol("RENDERDOC_GetAPI"));
    void* api = nullptr;
    if (getApi == nullptr || getApi(eRENDERDOC_API_Version_1_6_0, &api) != 1 || api == nullptr)
    {
        MEOW2_LOG_WARN(log, "RenderDoc is attached, but its version is too old for in-app capture");
        return;
    }
    m_library.emplace(std::move(*library));
    m_api = api;
    MEOW2_LOG_INFO(log, "RenderDoc attached: in-app capture is available");
}

RenderDocCapture::~RenderDocCapture() = default;

void RenderDocCapture::capture()
{
    if (m_api != nullptr)
    {
        table(m_api).TriggerCapture();
    }
}

std::uint32_t RenderDocCapture::captureCount() const
{
    return m_api != nullptr ? table(m_api).GetNumCaptures() : 0;
}
} // namespace Meow2::Rhi
