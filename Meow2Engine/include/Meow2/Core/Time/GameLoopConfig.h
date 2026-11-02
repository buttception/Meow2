#pragma once

/// @file
/// @brief Reading a GameLoopDesc from the app's config file.
///
/// The keys, all optional (`Meow2<App>.toml`):
/// @code{.toml}
/// [loop]
/// fixedStepHz = 60     # fixedUpdate calls per second of game time (1..1000)
/// maxFixedSteps = 8    # most fixedUpdate calls in one frame before time is dropped (1..64)
/// @endcode
/// @see docs/wiki/GameLoop.md, docs/wiki/Config.md

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Time/GameLoop.h>

#include <cstdint>

namespace Meow2::Time
{
/// @brief Default for `loop.fixedStepHz`: 60 fixed steps per second.
inline constexpr std::int64_t c_defaultFixedStepHz = 60;

/// @brief Smallest accepted `loop.fixedStepHz`.
inline constexpr std::int64_t c_minFixedStepHz = 1;

/// @brief Largest accepted `loop.fixedStepHz`.
inline constexpr std::int64_t c_maxFixedStepHz = 1000;

/// @brief Default for `loop.maxFixedSteps`.
inline constexpr std::int64_t c_defaultMaxFixedSteps = 8;

/// @brief Largest accepted `loop.maxFixedSteps` (the smallest is 1).
inline constexpr std::int64_t c_maxMaxFixedSteps = 64;

/// @brief Builds a GameLoopDesc from the `loop.*` keys, with the defaults for missing or invalid values.
///
/// A value outside its range is logged as a warning and replaced by the default.
/// @param config The app's settings.
/// @param log    Receives the warnings.
/// @return The loop settings.
[[nodiscard]] GameLoopDesc gameLoopDescFromConfig(const Config::Config& config, Log::Logger& log);
} // namespace Meow2::Time
