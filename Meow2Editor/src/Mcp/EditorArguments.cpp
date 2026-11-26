/// @file
/// @brief parseEditorArguments() (EditorArguments.h).

#include "Mcp/EditorArguments.h"

#include <charconv>
#include <format>
#include <string_view>

namespace Meow2::Editor
{
Result<EditorArguments> parseEditorArguments(int argc, const char* const* argv)
{
    EditorArguments arguments;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        // The value after an option that takes one, or an error if the command line ends there.
        const auto value = [&](std::string_view option) -> Result<std::string_view>
        {
            if (i + 1 >= argc)
            {
                return fail(ErrorCode::InvalidArgument, std::format("{} needs a value", option));
            }
            return std::string_view{argv[++i]};
        };
        if (argument == "--headless")
        {
            arguments.headless = true;
        }
        else if (argument == "--mcp")
        {
            arguments.mcp = true;
        }
        else if (argument == "--mcp-port")
        {
            const Result<std::string_view> text = value(argument);
            if (!text)
            {
                return Unexpected<Error>{text.error()};
            }
            unsigned port = 0;
            const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), port);
            if (error != std::errc{} || end != text->data() + text->size() || port == 0 || port > 65535)
            {
                return fail(ErrorCode::InvalidArgument,
                            std::format("--mcp-port expects a port from 1 to 65535, not '{}'", *text));
            }
            arguments.mcpPort = static_cast<std::uint16_t>(port);
        }
        else if (argument == "--mcp-token")
        {
            const Result<std::string_view> text = value(argument);
            if (!text)
            {
                return Unexpected<Error>{text.error()};
            }
            if (text->size() < c_minMcpTokenLength)
            {
                return fail(ErrorCode::InvalidArgument,
                            std::format("--mcp-token must be at least {} characters", c_minMcpTokenLength));
            }
            arguments.mcpToken = std::string{*text};
        }
    }
    return arguments;
}
} // namespace Meow2::Editor
