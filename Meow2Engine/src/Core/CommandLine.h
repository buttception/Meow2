#pragma once

/// @file
/// @brief Private helper: finds `--name value` / `--name=value` options in `main`'s arguments.

#include <string_view>
#include <vector>

namespace Meow2::Detail
{
/// @brief Every value given for one option, in command-line order.
///
/// Both spellings are accepted: `--name value` and `--name=value`. A missing or empty value (`--name` as the last
/// argument, `--name ""`, `--name=`) is skipped. `argv[0]` (the program) is never looked at.
/// @param argc   Argument count, as given to `main`.
/// @param argv   Arguments, as given to `main`.
/// @param option The option including its dashes, e.g. `"--config"`.
/// @return Views into `argv`; they live as long as `argv` does.
/// @par Thread safety
/// Thread-safe (reads only).
[[nodiscard]] std::vector<std::string_view> optionValues(int argc, const char* const* argv, std::string_view option);
} // namespace Meow2::Detail
