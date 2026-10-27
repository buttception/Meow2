#pragma once

/// @file
/// @brief Private to the Log module: Logger::Impl and the LogLevel ↔ spdlog conversion, shared by Logger.cpp and
///        LogSystem.cpp. spdlog is only ever included from src/Core/Log/.

#include "Meow2/Core/Log/Logger.h"

#include <spdlog/logger.h>

#include <memory>
#include <string>

namespace Meow2::Log
{
/// @brief The implementation behind a Logger: a shared spdlog logger plus the channel name.
struct Logger::Impl
{
    std::shared_ptr<spdlog::logger> logger; ///< Writes to the sinks of the LogSystem that created it.
    std::string name;                       ///< Channel name, also stored in `logger`.
};

/// @brief Converts an engine level to spdlog's (Fatal maps to spdlog's `critical`).
/// @param level Engine level.
/// @return The matching spdlog level.
spdlog::level::level_enum toSpdlogLevel(LogLevel level);
} // namespace Meow2::Log
