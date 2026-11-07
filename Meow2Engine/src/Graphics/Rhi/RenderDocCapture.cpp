/// @file
/// @brief RenderDocCapture: the only file that includes RenderDoc's header.

#include "Meow2/Graphics/Rhi/RenderDocCapture.h"

#include "Meow2/Core/Platform.h"

#include <renderdoc_app.h>

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
#if MEOW2_PLATFORM_WINDOWS
constexpr std::string_view c_libraryName = "renderdoc.dll";
constexpr std::string_view c_defaultLibrary = "C:/Program Files/RenderDoc/renderdoc.dll";
#else
constexpr std::string_view c_libraryName = "librenderdoc.so";
constexpr std::string_view c_defaultLibrary = "librenderdoc.so"; // the loader's search path
#endif

/// @brief The API table, in its real type.
/// @param api The stored pointer.
/// @return The table.
const RENDERDOC_API_1_6_0& table(const void* api)
{
    return *static_cast<const RENDERDOC_API_1_6_0*>(api);
}
} // namespace

RenderDocCapture::RenderDocCapture(Log::Logger& log, const RenderDocDesc& desc)
    : m_log(log)
    , m_openWindow(desc.openWindow)
{
    Result<SharedLibrary> library = SharedLibrary::findLoaded(c_libraryName);
    if (!library && desc.load)
    {
        library = SharedLibrary::open(desc.library.empty() ? std::filesystem::path{c_defaultLibrary} : desc.library);
        if (!library)
        {
            MEOW2_LOG_WARN(log, "Can't load RenderDoc: {}. Is it installed? rhi.renderDocPath sets where.",
                           library.error());
            return;
        }
    }
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
    library->keepLoaded(); // it hooks the graphics API, so it can't go away before the process does
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

void RenderDocCapture::update()
{
    const std::uint32_t count = captureCount();
    if (count == m_opened)
    {
        return;
    }
    m_opened = count;
    if (m_openWindow)
    {
        open(count - 1);
    }
}

void RenderDocCapture::open(std::uint32_t index)
{
    const RENDERDOC_API_1_6_0& api = table(m_api);
    std::uint32_t length = 0;
    if (api.GetCapture(index, nullptr, &length, nullptr) == 0 || length == 0)
    {
        return;
    }
    std::string path(length, '\0');
    api.GetCapture(index, path.data(), &length, nullptr);
    path.resize(std::char_traits<char>::length(path.c_str()));
    MEOW2_LOG_INFO(m_log, "Captured {}", path);
    if (api.IsTargetControlConnected() != 0)
    {
        api.ShowReplayUI(); // already open: this frame is in its list
        return;
    }
    if (api.LaunchReplayUI(1, std::format("\"{}\"", path).c_str()) == 0)
    {
        MEOW2_LOG_WARN(m_log, "Can't open the RenderDoc window; open the capture by hand");
    }
}

std::uint32_t RenderDocCapture::captureCount() const
{
    return m_api != nullptr ? table(m_api).GetNumCaptures() : 0;
}
} // namespace Meow2::Rhi
