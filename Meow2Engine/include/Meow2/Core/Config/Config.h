#pragma once

/// @file
/// @brief Config: settings from a TOML file, overridden on the command line, written back when the app changes them.
///
/// Typical composition root:
/// @code
/// using namespace Meow2;
///
/// int main(int argc, char** argv)
/// {
///     // ... logging ...
///     // Meow2Renderer.toml next to the exe (or --config <path>), plus --set key=value overrides.
///     Config::Config config = Config::openAppConfig("Renderer", argc, argv, engineLog);
///
///     const std::int64_t width = config.getInt("window.width", 1280); // file value, or 1280
///     // ... run ...
///     config.setInt("window.width", currentWidth);                   // remembered for the next run
///     if (const Result<void> saved = config.save(); !saved)          // writes only if something changed
///     {
///         MEOW2_LOG_WARN(engineLog, "Settings not saved: {}", saved.error());
///     }
/// }
/// @endcode
///
/// The file is plain TOML, e.g. `Meow2Renderer.toml`:
/// @code{.toml}
/// [window]
/// width = 1600
/// height = 900
/// mode = "windowed"
/// @endcode
/// @see techstack.md §3, docs/wiki/Config.md

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Meow2::Config
{
/// @brief Settings of one app: a TOML file plus command-line overrides, read by dotted key (`"window.width"`).
///
/// Where a value comes from, highest priority first:
/// 1. a command-line override, `--set window.width=800` (applyArguments()); used for this run only, never saved;
/// 2. the file (load()), including values changed with the `set*()` functions;
/// 3. the default the caller passes to the `get*()` function.
///
/// Keys are dotted paths of bare TOML keys (letters, digits, `_`, `-`): `"window.width"` is the key `width` in the
/// table `[window]`. Values are booleans, 64-bit integers, floating-point numbers and strings. Arrays, dates and
/// inline-table syntax in the file are kept when saving, but can't be read through this class yet.
///
/// save() rewrites the whole file from the parsed data, so **comments and formatting are lost** when it writes. It
/// only writes when a `set*()` call actually changed a value, so a hand-written file stays as it is until the app
/// changes something in it.
/// @par Thread safety
/// Not thread-safe. Read the settings on one thread (usually in the composition root) and hand the values out.
/// @par Lifetime
/// The Logger passed to load() must outlive the Config.
class Config
{
public:
    /// @brief Reads a config file.
    ///
    /// A missing file isn't an error: that's the first run. The Config is then empty and save() creates the file.
    /// @param path File to read, and where save() writes.
    /// @param log  Receives warnings about values of the wrong type. Must outlive the Config.
    /// @return The settings, or ErrorCode::ParseError (with file, line and column in the message) if the file isn't
    ///         valid TOML, or ErrorCode::IoError if it exists but can't be read.
    [[nodiscard]] static Result<Config> load(const std::filesystem::path& path, Log::Logger& log);

    /// @brief A Config with no settings that saves to `path`. Use it when load() failed, to run with defaults.
    /// @param path Where save() writes.
    /// @param log  Receives warnings about values of the wrong type. Must outlive the Config.
    /// @return The empty settings.
    [[nodiscard]] static Config empty(const std::filesystem::path& path, Log::Logger& log);

    /// @brief Frees the parsed settings.
    ~Config();

    /// @brief Takes over the settings of another Config.
    /// @param other Config to move from. It must not be used afterwards, except to be destroyed or assigned to.
    Config(Config&& other) noexcept;

    /// @brief Replaces these settings with another Config's (the old ones are dropped, not saved).
    /// @param other Config to move from. It must not be used afterwards, except to be destroyed or assigned to.
    /// @return `*this`.
    Config& operator=(Config&& other) noexcept;

    Config(const Config&) = delete;            ///< Not copyable: one Config per file.
    Config& operator=(const Config&) = delete; ///< Not copyable: one Config per file.

    /// @brief Applies every `--set key=value` (or `--set=key=value`) command-line argument as an override.
    ///
    /// The value is read as a TOML value when it is one (`800`, `true`, `1.5`, `"text"`) and as a string otherwise,
    /// so `--set window.mode=fullscreen` works without quotes. Valid overrides are applied even if others fail.
    /// @param argc Argument count, as given to `main`.
    /// @param argv Arguments, as given to `main`.
    /// @return Success, or ErrorCode::InvalidArgument listing the arguments that aren't `key=value` with a valid key.
    [[nodiscard]] Result<void> applyArguments(int argc, const char* const* argv);

    /// @brief Reads a boolean.
    /// @param key          Dotted key, e.g. `"window.resizable"`.
    /// @param defaultValue Returned when the key is missing, or has another type (which logs a warning).
    /// @return The override, else the file value, else `defaultValue`.
    /// @pre `key` is a valid dotted key (asserted).
    [[nodiscard]] bool getBool(std::string_view key, bool defaultValue) const;

    /// @brief Reads an integer.
    /// @param key          Dotted key, e.g. `"window.width"`.
    /// @param defaultValue Returned when the key is missing, or has another type (which logs a warning).
    /// @return The override, else the file value, else `defaultValue`.
    /// @pre `key` is a valid dotted key (asserted).
    [[nodiscard]] std::int64_t getInt(std::string_view key, std::int64_t defaultValue) const;

    /// @brief Reads a floating-point number. An integer value is accepted too (`scale = 2` reads as 2.0).
    /// @param key          Dotted key, e.g. `"renderer.scale"`.
    /// @param defaultValue Returned when the key is missing, or has another type (which logs a warning).
    /// @return The override, else the file value, else `defaultValue`.
    /// @pre `key` is a valid dotted key (asserted).
    [[nodiscard]] double getFloat(std::string_view key, double defaultValue) const;

    /// @brief Reads a string.
    /// @param key          Dotted key, e.g. `"window.mode"`.
    /// @param defaultValue Returned when the key is missing, or has another type (which logs a warning).
    /// @return The override, else the file value, else `defaultValue`.
    /// @pre `key` is a valid dotted key (asserted).
    [[nodiscard]] std::string getString(std::string_view key, std::string_view defaultValue) const;

    /// @brief Whether the key has a value, from the file or an override.
    /// @param key Dotted key.
    /// @return `true` if get*() would find a value (of any type) instead of using its default.
    /// @pre `key` is a valid dotted key (asserted).
    [[nodiscard]] bool contains(std::string_view key) const;

    /// @brief Stores a boolean in the file layer and removes an override of the same key.
    /// @param key   Dotted key. Missing tables are created; a non-table value in the way is replaced (with a warning).
    /// @param value New value.
    /// @pre `key` is a valid dotted key (asserted).
    void setBool(std::string_view key, bool value);

    /// @brief Stores an integer in the file layer and removes an override of the same key.
    /// @param key   Dotted key (see setBool()).
    /// @param value New value.
    /// @pre `key` is a valid dotted key (asserted).
    void setInt(std::string_view key, std::int64_t value);

    /// @brief Stores a floating-point number in the file layer and removes an override of the same key.
    /// @param key   Dotted key (see setBool()).
    /// @param value New value.
    /// @pre `key` is a valid dotted key (asserted).
    void setFloat(std::string_view key, double value);

    /// @brief Stores a string in the file layer and removes an override of the same key.
    /// @param key   Dotted key (see setBool()).
    /// @param value New value.
    /// @pre `key` is a valid dotted key (asserted).
    void setString(std::string_view key, std::string_view value);

    /// @brief Whether a `set*()` call changed the file layer since load() or the last save().
    /// @return `true` if save() would write.
    [[nodiscard]] bool hasUnsavedChanges() const;

    /// @brief Writes the file layer to path() if it has unsaved changes; does nothing otherwise.
    ///
    /// Missing folders are created. The file is written to `<path>.tmp` first and then renamed over the old one, so
    /// a crash or full disk never leaves a half-written config behind. Overrides are never written.
    /// @return Success, or ErrorCode::IoError (with the path) if the file couldn't be written, or
    ///         ErrorCode::Unsupported if saving is disabled (see openAppConfig()).
    [[nodiscard]] Result<void> save();

    /// @brief The file this Config was loaded from and saves to.
    /// @return The path given to load() or empty().
    [[nodiscard]] const std::filesystem::path& path() const;

private:
    // Disables saving for the fallback Config of a file that couldn't be loaded.
    friend Config openAppConfig(std::string_view appName, int argc, const char* const* argv, Log::Logger& log);

    struct Impl;
    explicit Config(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

/// @brief The default config file of an app: `Meow2<appName>.toml` next to the running executable.
/// @param appName Short app name, e.g. `"Renderer"`.
/// @return `<folder of the executable>/Meow2<appName>.toml`, or just the file name (relative to the working
///         directory) if the executable's location can't be determined.
[[nodiscard]] std::filesystem::path defaultConfigPath(std::string_view appName);

/// @brief Finds a `--config <path>` or `--config=<path>` command-line argument.
///
/// When the apps are started from the Visual Studio solution, CMake passes `--config <repo>/config/Meow2<App>.toml`,
/// so runs from the IDE keep their settings in the repository's (git-ignored) `config` folder.
/// @param argc Argument count, as given to `main`.
/// @param argv Arguments, as given to `main`. `argv[0]` (the program) is skipped.
/// @return The file given on the command line (the last one if repeated), or `std::nullopt` if the option isn't
///         there (or has no value).
[[nodiscard]] std::optional<std::filesystem::path> configPathFromArguments(int argc, const char* const* argv);

/// @brief Everything an app does to get its settings, in one call: find the file, load it, apply `--set` overrides,
///        and log what happened.
///
/// - The file is `--config <path>` if given (configPathFromArguments()), else defaultConfigPath(appName).
/// - If the file can't be loaded (bad TOML, unreadable), the error is logged and the app runs with defaults. That
///   Config never saves: save() returns ErrorCode::Unsupported instead of overwriting the user's file, so a typo
///   doesn't cost them their settings.
/// - Invalid `--set` arguments are logged as a warning; the valid ones still apply.
/// @param appName Short app name, e.g. `"Renderer"`.
/// @param argc    Argument count, as given to `main`.
/// @param argv    Arguments, as given to `main`.
/// @param log     Where problems are logged; also kept by the Config. Must outlive it.
/// @return The app's settings. Always usable, possibly empty.
[[nodiscard]] Config openAppConfig(std::string_view appName, int argc, const char* const* argv, Log::Logger& log);
} // namespace Meow2::Config
