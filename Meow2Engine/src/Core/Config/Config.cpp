/// @file
/// @brief Config implementation over toml++ (the only file that includes it).

#include "Meow2/Core/Config/Config.h"

#include "Core/CommandLine.h"
#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Core/Process.h"

#include <toml++/toml.h>

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace Meow2::Config
{
namespace
{
using Memory::MemTag;
using Memory::MemTagScope;

/// @brief Whether `key` is a dotted path of bare TOML keys: `window.width`, never `window..width` or `"a b"`.
bool isValidKey(std::string_view key)
{
    if (key.empty() || key.front() == '.' || key.back() == '.' || key.find("..") != std::string_view::npos)
    {
        return false;
    }
    for (const char c : key)
    {
        const bool bare = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '-' || c == '.';
        if (!bare)
        {
            return false;
        }
    }
    return true;
}

/// @brief Splits a valid dotted key into its parts.
std::vector<std::string_view> splitKey(std::string_view key)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t dot = key.find('.', start);
        parts.push_back(key.substr(start, dot - start));
        if (dot == std::string_view::npos)
        {
            return parts;
        }
        start = dot + 1;
    }
}

/// @brief The node at a dotted key, or null if the key (or a table on its way) doesn't exist.
const toml::node* find(const toml::table& root, std::string_view key)
{
    const toml::table* table = &root;
    const std::vector<std::string_view> parts = splitKey(key);
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        const toml::node* child = table->get(parts[i]);
        table = child != nullptr ? child->as_table() : nullptr;
        if (table == nullptr)
        {
            return nullptr;
        }
    }
    return table->get(parts.back());
}

/// @brief The table that holds the last part of a dotted key, creating missing tables on the way.
/// @param replaced Set to `true` if a non-table value was in the way and had to be replaced by a table.
toml::table& parentTable(toml::table& root, const std::vector<std::string_view>& parts, bool& replaced)
{
    toml::table* table = &root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        toml::node* child = table->get(parts[i]);
        if (child == nullptr || !child->is_table())
        {
            replaced = replaced || child != nullptr;
            child = &table->insert_or_assign(parts[i], toml::table{}).first->second;
        }
        table = child->as_table();
    }
    return *table;
}

/// @brief Removes a dotted key if it exists. Empty parent tables are left in place (harmless, and rare).
void erase(toml::table& root, std::string_view key)
{
    toml::table* table = &root;
    const std::vector<std::string_view> parts = splitKey(key);
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        toml::node* child = table->get(parts[i]);
        table = child != nullptr ? child->as_table() : nullptr;
        if (table == nullptr)
        {
            return;
        }
    }
    table->erase(parts.back());
}

/// @brief Reads one `--set` value: a TOML value if it parses as one, otherwise the text as a string.
void insertArgumentValue(toml::table& table, std::string_view name, std::string_view text)
{
    // Parsing `v = <text>` reuses toml++ for numbers, booleans and quoted strings. Anything else (`fullscreen`,
    // `My Game`, `1280x720`) isn't a TOML value and is taken literally.
    toml::parse_result parsed = toml::parse(std::format("v = {}", text));
    if (parsed)
    {
        if (toml::node* value = parsed.table().get("v"))
        {
            // node is abstract; visit() hands over the concrete type (value<T>, array or table) to move from.
            value->visit([&table, name](auto&& concrete) { table.insert_or_assign(name, std::move(concrete)); });
            return;
        }
    }
    table.insert_or_assign(name, std::string{text});
}
} // namespace

/// @brief The parsed file, the overrides, and where to save.
struct Config::Impl
{
    std::filesystem::path path; ///< Where the file was loaded from and is saved to.
    Log::Logger* log = nullptr; ///< Warnings about wrong value types. Never null.
    toml::table file;           ///< The file layer: what save() writes.
    toml::table overrides;      ///< `--set` values: read first, never saved.
    bool dirty = false;         ///< A set*() call changed `file` since the last load or save.
    bool saveDisabled = false;  ///< The file exists but couldn't be loaded: never overwrite it.

    /// @brief The value of a key: the override if there is one, else the file value, else null.
    [[nodiscard]] const toml::node* lookup(std::string_view key) const
    {
        MEOW2_ASSERT(isValidKey(key), "config keys are dotted bare keys, e.g. window.width");
        const toml::node* node = find(overrides, key);
        return node != nullptr ? node : find(file, key);
    }

    /// @brief Logs that a key exists with a type the caller can't use.
    void warnWrongType(std::string_view key, std::string_view expected) const
    {
        MEOW2_LOG_WARN(*log, "Config {}: '{}' should be {}; using the default", path.string(), key, expected);
    }

    /// @brief Stores `value` at `key` in the file layer, unless it's already there, and drops the override.
    template <class T>
    void set(std::string_view key, T value)
    {
        MEOW2_ASSERT(isValidKey(key), "config keys are dotted bare keys, e.g. window.width");
        const MemTagScope tag{MemTag::Config};
        erase(overrides, key);

        if (const toml::node* existing = find(file, key))
        {
            if (const auto* same = existing->as<T>(); same != nullptr && same->get() == value)
            {
                return; // unchanged: no write, so a hand-edited file keeps its comments
            }
        }
        const std::vector<std::string_view> parts = splitKey(key);
        bool replaced = false;
        toml::table& table = parentTable(file, parts, replaced);
        if (replaced)
        {
            MEOW2_LOG_WARN(*log, "Config {}: a value on the way to '{}' was replaced by a table", path.string(), key);
        }
        table.insert_or_assign(parts.back(), std::move(value));
        dirty = true;
    }
};

Config::Config(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Config::~Config() = default;
Config::Config(Config&& other) noexcept = default;
Config& Config::operator=(Config&& other) noexcept = default;

Result<Config> Config::load(const std::filesystem::path& path, Log::Logger& log)
{
    const MemTagScope tag{MemTag::Config};
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return empty(path, log); // first run: nothing to read yet
    }

    // Read the file ourselves (rather than toml::parse_file) so wide Windows paths work and an unreadable file is
    // an IoError, not a parse error.
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return fail(ErrorCode::IoError, std::format("can't open {}", path.string()));
    }
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad())
    {
        return fail(ErrorCode::IoError, std::format("can't read {}", path.string()));
    }

    toml::parse_result parsed = toml::parse(text, path.string());
    if (!parsed)
    {
        const toml::parse_error& parseError = parsed.error();
        return fail(ErrorCode::ParseError, std::format("{}({}:{}): {}", path.string(), parseError.source().begin.line,
                                                       parseError.source().begin.column, parseError.description()));
    }

    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->log = &log;
    impl->file = std::move(parsed).table();
    return Config{std::move(impl)};
}

Config Config::empty(const std::filesystem::path& path, Log::Logger& log)
{
    const MemTagScope tag{MemTag::Config};
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->log = &log;
    return Config{std::move(impl)};
}

Result<void> Config::applyArguments(int argc, const char* const* argv)
{
    const MemTagScope tag{MemTag::Config};
    std::string invalid;
    for (const std::string_view argument : Detail::optionValues(argc, argv, "--set"))
    {
        const std::size_t equals = argument.find('=');
        const std::string_view key = argument.substr(0, equals);
        if (equals == std::string_view::npos || !isValidKey(key))
        {
            invalid += invalid.empty() ? "" : ", ";
            invalid += argument;
            continue;
        }
        const std::vector<std::string_view> parts = splitKey(key);
        bool replaced = false;
        insertArgumentValue(parentTable(m_impl->overrides, parts, replaced), parts.back(), argument.substr(equals + 1));
    }
    if (!invalid.empty())
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("--set expects key=value, e.g. window.width=1280: {}", invalid));
    }
    return {};
}

bool Config::getBool(std::string_view key, bool defaultValue) const
{
    const toml::node* node = m_impl->lookup(key);
    if (node == nullptr)
    {
        return defaultValue;
    }
    if (const auto* value = node->as_boolean())
    {
        return value->get();
    }
    m_impl->warnWrongType(key, "true or false");
    return defaultValue;
}

std::int64_t Config::getInt(std::string_view key, std::int64_t defaultValue) const
{
    const toml::node* node = m_impl->lookup(key);
    if (node == nullptr)
    {
        return defaultValue;
    }
    if (const auto* value = node->as_integer())
    {
        return value->get();
    }
    m_impl->warnWrongType(key, "an integer");
    return defaultValue;
}

double Config::getFloat(std::string_view key, double defaultValue) const
{
    const toml::node* node = m_impl->lookup(key);
    if (node == nullptr)
    {
        return defaultValue;
    }
    if (const auto* value = node->as_floating_point())
    {
        return value->get();
    }
    if (const auto* value = node->as_integer())
    {
        return static_cast<double>(value->get()); // people write `scale = 2`, not `scale = 2.0`
    }
    m_impl->warnWrongType(key, "a number");
    return defaultValue;
}

std::string Config::getString(std::string_view key, std::string_view defaultValue) const
{
    const toml::node* node = m_impl->lookup(key);
    if (node != nullptr)
    {
        if (const auto* value = node->as_string())
        {
            return value->get();
        }
        m_impl->warnWrongType(key, "a string");
    }
    return std::string{defaultValue};
}

bool Config::contains(std::string_view key) const
{
    return m_impl->lookup(key) != nullptr;
}

void Config::setBool(std::string_view key, bool value)
{
    m_impl->set(key, value);
}

void Config::setInt(std::string_view key, std::int64_t value)
{
    m_impl->set(key, value);
}

void Config::setFloat(std::string_view key, double value)
{
    m_impl->set(key, value);
}

void Config::setString(std::string_view key, std::string_view value)
{
    const MemTagScope tag{MemTag::Config};
    m_impl->set(key, std::string{value});
}

bool Config::hasUnsavedChanges() const
{
    return m_impl->dirty;
}

Result<void> Config::save()
{
    if (!m_impl->dirty)
    {
        return {};
    }
    const MemTagScope tag{MemTag::Config};
    const std::filesystem::path& path = m_impl->path;
    if (m_impl->saveDisabled)
    {
        return fail(ErrorCode::Unsupported,
                    std::format("{} couldn't be loaded at startup, so it isn't overwritten", path.string()));
    }
    std::error_code error;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            return fail(ErrorCode::IoError,
                        std::format("can't create {}: {}", path.parent_path().string(), error.message()));
        }
    }

    // Write a temporary file and rename it over the old one: a crash or a full disk mid-write then leaves the old
    // config intact instead of a truncated one.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << m_impl->file << '\n';
        stream.close();
        if (!stream)
        {
            std::filesystem::remove(temporary, error);
            return fail(ErrorCode::IoError, std::format("can't write {}", temporary.string()));
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fail(ErrorCode::IoError, std::format("can't replace {}: {}", path.string(), error.message()));
    }
    m_impl->dirty = false;
    return {};
}

const std::filesystem::path& Config::path() const
{
    return m_impl->path;
}

std::filesystem::path defaultConfigPath(std::string_view appName)
{
    const std::string fileName = std::format("Meow2{}.toml", appName);
    const std::filesystem::path executable = executablePath();
    return executable.empty() ? std::filesystem::path{fileName} : executable.parent_path() / fileName;
}

std::optional<std::filesystem::path> configPathFromArguments(int argc, const char* const* argv)
{
    const std::vector<std::string_view> values = Detail::optionValues(argc, argv, "--config");
    if (values.empty())
    {
        return std::nullopt;
    }
    return std::filesystem::path{values.back()};
}

Config openAppConfig(std::string_view appName, int argc, const char* const* argv, Log::Logger& log)
{
    const std::filesystem::path path = configPathFromArguments(argc, argv).value_or(defaultConfigPath(appName));
    Result<Config> loaded = Config::load(path, log);
    if (!loaded)
    {
        MEOW2_LOG_ERROR(log, "Config not loaded, running with defaults (and not saving over it): {}", loaded.error());
        loaded = Config::empty(path, log);
        loaded->m_impl->saveDisabled = true;
    }
    if (const Result<void> applied = loaded->applyArguments(argc, argv); !applied)
    {
        MEOW2_LOG_WARN(log, "Ignored command-line settings: {}", applied.error());
    }
    MEOW2_LOG_INFO(log, "Config file: {}", path.string());
    return std::move(*loaded);
}
} // namespace Meow2::Config
