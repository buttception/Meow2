/// @file
/// @brief Shader `.meta` parsing and variant keys.

#include <Meow2/Assets/Shader/ShaderMeta.h>
#include <Meow2/Core/Assert.h>

#include <toml++/toml.h>

#include <algorithm>
#include <format>
#include <optional>

namespace Meow2::Assets
{
namespace
{
/// @brief A stage's name in a `.meta` file.
std::optional<Shader::ShaderStage> parseStage(std::string_view name)
{
    using Shader::ShaderStage;
    if (name == "vertex")
    {
        return ShaderStage::Vertex;
    }
    if (name == "pixel")
    {
        return ShaderStage::Pixel;
    }
    if (name == "compute")
    {
        return ShaderStage::Compute;
    }
    if (name == "amplification")
    {
        return ShaderStage::Amplification;
    }
    if (name == "mesh")
    {
        return ShaderStage::Mesh;
    }
    return std::nullopt;
}

/// @brief A macro value in a `.meta` file as text: integers, booleans (0/1) and strings are allowed.
std::optional<std::string> valueText(const toml::node& node)
{
    if (const std::optional<std::int64_t> number = node.value_exact<std::int64_t>())
    {
        return std::to_string(*number);
    }
    if (const std::optional<bool> flag = node.value_exact<bool>())
    {
        return *flag ? "1" : "0";
    }
    if (const std::optional<std::string> text = node.value_exact<std::string>())
    {
        return *text;
    }
    return std::nullopt;
}

/// @brief Builds the "path: what's wrong" error.
Unexpected<Error> metaError(std::string_view path, std::string_view problem)
{
    return fail(ErrorCode::ParseError, std::format("{}: {}", path, problem));
}
} // namespace

Result<ShaderMeta> parseShaderMeta(std::string_view text, std::string_view path)
{
    toml::parse_result parsed = toml::parse(text);
    if (!parsed)
    {
        return metaError(path, parsed.error().description());
    }
    const toml::table& table = parsed.table();

    ShaderMeta meta;
    const toml::array* entries = table["entry"].as_array();
    if (entries == nullptr || entries->empty())
    {
        return metaError(path, "no [[entry]]");
    }
    for (const toml::node& node : *entries)
    {
        const toml::table* entry = node.as_table();
        const std::optional<std::string> name = entry != nullptr ? (*entry)["name"].value<std::string>() : std::nullopt;
        const std::optional<std::string_view> stageName =
            entry != nullptr ? (*entry)["stage"].value<std::string_view>() : std::nullopt;
        if (!name || !stageName)
        {
            return metaError(path, "an [[entry]] needs a name and a stage");
        }
        const std::optional<Shader::ShaderStage> stage = parseStage(*stageName);
        if (!stage)
        {
            return metaError(path, std::format("unknown stage \"{}\"", *stageName));
        }
        meta.entries.push_back({*name, *stage});
    }

    if (const toml::table* defines = table["defines"].as_table())
    {
        for (const auto& [key, node] : *defines)
        {
            const std::optional<std::string> value = valueText(node);
            if (!value)
            {
                return metaError(path, std::format("define {} must be a number, boolean or string", key.str()));
            }
            meta.defines.push_back({std::string{key.str()}, *value});
        }
    }

    if (const toml::table* options = table["options"].as_table())
    {
        for (const auto& [key, node] : *options)
        {
            const toml::array* values = node.as_array();
            if (values == nullptr || values->empty())
            {
                return metaError(path, std::format("option {} needs a list of values", key.str()));
            }
            ShaderOption option{std::string{key.str()}, {}};
            for (const toml::node& valueNode : *values)
            {
                const std::optional<std::string> value = valueText(valueNode);
                if (!value)
                {
                    return metaError(path,
                                     std::format("option {}: values must be numbers, booleans or strings", key.str()));
                }
                if (std::find(option.values.begin(), option.values.end(), *value) != option.values.end())
                {
                    return metaError(path, std::format("option {} repeats the value {}", key.str(), *value));
                }
                option.values.push_back(*value);
            }
            const auto sameName = [&option](const Shader::ShaderDefine& d)
            {
                return d.name == option.name;
            };
            if (std::any_of(meta.defines.begin(), meta.defines.end(), sameName))
            {
                return metaError(path, std::format("{} is both a define and an option", option.name));
            }
            meta.options.push_back(std::move(option));
        }
    }

    // The order in the file mustn't change keys or cook output.
    std::sort(meta.defines.begin(), meta.defines.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(meta.options.begin(), meta.options.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    if (variantCount(meta) > c_shaderVariantLimit)
    {
        return metaError(path,
                         std::format("{} variants is over the limit of {}", variantCount(meta), c_shaderVariantLimit));
    }
    return meta;
}

std::uint64_t variantCount(const ShaderMeta& meta)
{
    std::uint64_t count = 1;
    for (const ShaderOption& option : meta.options)
    {
        count *= option.values.size();
        // Saturate: only "over the limit" matters, and a hostile file mustn't overflow.
        count = std::min(count, c_shaderVariantLimit + 1);
    }
    return count;
}

Result<ShaderVariantKey> variantKey(const ShaderMeta& meta, std::span<const ShaderOptionChoice> choices)
{
    for (const ShaderOptionChoice& choice : choices)
    {
        const auto known = [&choice](const ShaderOption& o)
        {
            return o.name == choice.name;
        };
        if (std::none_of(meta.options.begin(), meta.options.end(), known))
        {
            return fail(ErrorCode::InvalidArgument, std::format("no shader option {}", choice.name));
        }
    }
    // Mixed radix: the first option counts in steps of 1, the next in steps of the first's size, and so on.
    std::uint32_t index = 0;
    std::uint32_t step = 1;
    for (const ShaderOption& option : meta.options)
    {
        const auto chosen = std::find_if(choices.begin(), choices.end(),
                                         [&option](const ShaderOptionChoice& c) { return c.name == option.name; });
        if (chosen != choices.end())
        {
            const auto value = std::find(option.values.begin(), option.values.end(), chosen->value);
            if (value == option.values.end())
            {
                return fail(ErrorCode::InvalidArgument,
                            std::format("shader option {} has no value {}", option.name, chosen->value));
            }
            index += step * static_cast<std::uint32_t>(value - option.values.begin());
        }
        step *= static_cast<std::uint32_t>(option.values.size());
    }
    return ShaderVariantKey{index};
}

std::vector<Shader::ShaderDefine> variantDefines(const ShaderMeta& meta, ShaderVariantKey key)
{
    MEOW2_ASSERT(key.index < variantCount(meta), "the key belongs to another shader");
    std::vector<Shader::ShaderDefine> defines = meta.defines;
    std::uint32_t remaining = key.index;
    for (const ShaderOption& option : meta.options)
    {
        const auto size = static_cast<std::uint32_t>(option.values.size());
        defines.push_back({option.name, option.values[remaining % size]});
        remaining /= size;
    }
    return defines;
}
} // namespace Meow2::Assets
