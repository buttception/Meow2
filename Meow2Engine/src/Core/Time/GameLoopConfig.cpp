/// @file
/// @brief GameLoopDesc from the config (GameLoopConfig.h).

#include "Meow2/Core/Time/GameLoopConfig.h"

#include <string_view>

namespace Meow2::Time
{
namespace
{
/// @brief An integer setting, or the default (with a warning) if it's outside `min..max`.
std::int64_t readInRange(const Config::Config& config, std::string_view key, std::int64_t defaultValue,
                         std::int64_t min, std::int64_t max, Log::Logger& log)
{
    const std::int64_t value = config.getInt(key, defaultValue);
    if (value < min || value > max)
    {
        MEOW2_LOG_WARN(log, "Config: {} = {} is outside {}..{}; using {}", key, value, min, max, defaultValue);
        return defaultValue;
    }
    return value;
}
} // namespace

GameLoopDesc gameLoopDescFromConfig(const Config::Config& config, Log::Logger& log)
{
    const std::int64_t stepHz =
        readInRange(config, "loop.fixedStepHz", c_defaultFixedStepHz, c_minFixedStepHz, c_maxFixedStepHz, log);
    const std::int64_t maxSteps =
        readInRange(config, "loop.maxFixedSteps", c_defaultMaxFixedSteps, 1, c_maxMaxFixedSteps, log);
    return {.fixedStepSeconds = 1.0 / static_cast<double>(stepHz),
            .maxFixedStepsPerFrame = static_cast<std::uint32_t>(maxSteps)};
}
} // namespace Meow2::Time
