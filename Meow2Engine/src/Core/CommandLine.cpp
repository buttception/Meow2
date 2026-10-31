/// @file
/// @brief Command-line option lookup (CommandLine.h).

#include "Core/CommandLine.h"

namespace Meow2::Detail
{
std::vector<std::string_view> optionValues(int argc, const char* const* argv, std::string_view option)
{
    std::vector<std::string_view> values;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == option)
        {
            // `--name value`: the value is the next argument (consumed, so it isn't read as an option itself).
            if (i + 1 < argc)
            {
                ++i;
                if (argv[i][0] != '\0')
                {
                    values.emplace_back(argv[i]);
                }
            }
        }
        else if (argument.size() > option.size() + 1 && argument.starts_with(option) && argument[option.size()] == '=')
        {
            values.push_back(argument.substr(option.size() + 1));
        }
    }
    return values;
}
} // namespace Meow2::Detail
