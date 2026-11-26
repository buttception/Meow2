#pragma once

/// @file
/// @brief EditorArguments: the editor's own command-line options (headless mode and the MCP server).

#include <Meow2/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Meow2::Editor
{
/// @brief The editor's own command-line options. Plain data.
///
/// The others on the same command line (`--config`, `--set`, `--log-dir`, Qt's `-platform`, …) belong to their own
/// readers and are skipped here.
struct EditorArguments
{
    bool headless = false;                ///< `--headless`: no window; the viewports draw offscreen.
    bool mcp = false;                     ///< `--mcp`: start the MCP server.
    std::optional<std::uint16_t> mcpPort; ///< `--mcp-port <n>`: its port (1..65535); else `editor.mcp.port`.
    std::optional<std::string> mcpToken;  ///< `--mcp-token <text>`: its bearer token; else a random one.
};

/// @brief The shortest `--mcp-token` accepted, in characters: a short token could be guessed.
inline constexpr std::size_t c_minMcpTokenLength = 16;

/// @brief Reads the editor's options from a command line.
/// @param argc The argument count, as main() gets it.
/// @param argv The arguments; argv[0] (the program) is skipped.
/// @return The options, or an error naming a bad one (a port that isn't a number in range, a missing value, a
///         token shorter than c_minMcpTokenLength).
[[nodiscard]] Result<EditorArguments> parseEditorArguments(int argc, const char* const* argv);
} // namespace Meow2::Editor
