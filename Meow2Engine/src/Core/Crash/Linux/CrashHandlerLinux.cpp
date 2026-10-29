/// @file
/// @brief Linux/Android CrashHandler: not implemented yet, so it only records the path and says so in the log.
///
/// Writing dumps on Linux/Android needs an out-of-process writer such as crashpad or breakpad (a signal handler
/// can't safely do the work itself). That comes with the Platforms milestone (M12).

#include "Meow2/Core/Crash/CrashHandler.h"

namespace Meow2::Crash
{
struct CrashHandler::Impl
{
    std::filesystem::path dumpPath; ///< Kept so dumpPath() behaves the same on every platform.
};

CrashHandler::CrashHandler(const CrashHandlerDesc& desc, Log::Logger& log)
    : m_impl(std::make_unique<Impl>(Impl{desc.dumpPath}))
{
    log.debug("Crash dumps aren't supported on this platform yet; crashes won't write {}", desc.dumpPath.string());
}

CrashHandler::~CrashHandler() = default;

bool CrashHandler::isActive() const
{
    return false;
}

const std::filesystem::path& CrashHandler::dumpPath() const
{
    return m_impl->dumpPath;
}
} // namespace Meow2::Crash
