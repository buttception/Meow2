#pragma once

/// @file
/// @brief `Result<T>`: a value **or** an Error, for operations that can fail in a correct program.
///
/// Use Result when failure is an expected outcome the caller must handle: a missing file, a bad config value, an
/// unsupported format, a full disk. Bugs ("this can't happen") are asserts, not Results (see Assert.h).
///
/// @code
/// Meow2::Result<Config> loadConfig(const std::filesystem::path& path)
/// {
///     if (!std::filesystem::exists(path))
///     {
///         return Meow2::fail(Meow2::ErrorCode::NotFound, path.string());
///     }
///     Config config = parse(path);
///     return config;                               // implicitly a success
/// }
///
/// if (const Meow2::Result<Config> config = loadConfig("config.toml"))
/// {
///     use(*config);                                // the value
/// }
/// else
/// {
///     MEOW2_LOG_WARN(m_log, "No config: {}", config.error());   // e.g. "NotFound: config.toml"
/// }
/// @endcode
///
/// `Result<T>` is `tl::expected<T, Error>`, a C++20 implementation of C++23's `std::expected`. **Use only the part of
/// the API that `std::expected` also has:** `has_value()`, `operator bool`, `*`, `->`, `value()`, `error()`,
/// `value_or()`, `and_then()`, `transform()`, `or_else()`, `transform_error()`. Don't use tl's extra names (`map`,
/// `map_error`). Moving to `std::expected` then only means changing the alias in this file. The same goes for
/// Unexpected (what fail() returns): only **return** it, don't read it. Its accessor is `value()` in tl and
/// `error()` in `std`.
///
/// Dereferencing a Result that holds an error (`*result`, `result->`), or calling `error()` on a success, fails a
/// Meow2 assert in Debug and RelWithDebInfo. `value()` throws `bad_expected_access` instead; prefer checking first.
///
/// Always include this header, never `<tl/expected.hpp>` directly: it routes tl::expected's internal checks to
/// MEOW2_ASSERT.
/// @par Thread safety
/// Results are values: like any object, one may be read from several threads, but not modified concurrently.
/// @see techstack.md §3

#include <Meow2/Core/Assert.h>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#if defined(TL_EXPECTED_HPP)
#error "Include <Meow2/Core/Result.h> instead of <tl/expected.hpp>, so TL_ASSERT is the same everywhere."
#endif

/// @def TL_ASSERT(x)
/// @brief Routes tl::expected's own checks (e.g. dereferencing an error Result) to MEOW2_ASSERT.
/// @param x Condition tl::expected requires.
#define TL_ASSERT(x) MEOW2_ASSERT(x, "tl::expected precondition (e.g. *result on an error)")

#include <tl/expected.hpp>

namespace Meow2
{
/// @brief What kind of failure an Error describes. Callers branch on the code; the message is for humans.
enum class ErrorCode : std::uint8_t
{
    Unknown,         ///< Anything that doesn't fit another code. Prefer a specific one.
    InvalidArgument, ///< A parameter or input value is malformed or out of range.
    NotFound,        ///< A file, folder, key or resource doesn't exist.
    AlreadyExists,   ///< Something that must be new already exists.
    AccessDenied,    ///< The OS refused access (permissions, file in use).
    IoError,         ///< Reading or writing failed (disk full, device error).
    ParseError,      ///< Text or binary data couldn't be parsed (bad TOML, corrupt glTF).
    Unsupported,     ///< Valid, but this platform, backend or version can't do it.
    OutOfMemory,     ///< An allocation failed.
    Timeout,         ///< Waited too long for something (GPU fence, file lock).
    Cancelled        ///< The operation was cancelled on purpose.
};

/// @brief An error: a code to branch on and a message for people. Plain data.
///
/// Format it with `std::format("{}", error)` (or a `MEOW2_LOG_*` macro): `NotFound: config.toml`, or just `NotFound`
/// when the message is empty.
struct Error
{
    ErrorCode code = ErrorCode::Unknown; ///< What kind of failure it is.
    std::string message;                 ///< Details for logs and users, e.g. the path that wasn't found.

    /// @brief Errors are equal when code and message are equal.
    friend bool operator==(const Error&, const Error&) = default;
};

/// @brief A value of type `T`, or an error of type `E` (Error by default).
/// @tparam T Type of the value on success. `void` for operations that return nothing on success.
/// @tparam E Type of the error. Use the default unless a module needs richer error data.
template <class T, class E = Error>
using Result = tl::expected<T, E>;

/// @brief Wraps an error so it converts into any `Result<T, E>`: `return Unexpected<Error>{Error{...}};`.
///        Usually fail() is shorter.
/// @tparam E Type of the error.
template <class E>
using Unexpected = tl::unexpected<E>;

/// @brief Builds a failed Result: `return fail(ErrorCode::NotFound, path.string());`.
/// @param code    What kind of failure.
/// @param message Details for logs and users (may be empty).
/// @return An Unexpected<Error> that converts to any `Result<T>`.
[[nodiscard]] inline Unexpected<Error> fail(ErrorCode code, std::string message = {})
{
    return Unexpected<Error>{Error{code, std::move(message)}};
}

/// @brief The name of an error code, e.g. `"NotFound"`.
/// @param code The code.
/// @return A static, null-terminated name. Never empty.
[[nodiscard]] constexpr std::string_view toString(ErrorCode code)
{
    switch (code)
    {
        case ErrorCode::Unknown:
            return "Unknown";
        case ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case ErrorCode::NotFound:
            return "NotFound";
        case ErrorCode::AlreadyExists:
            return "AlreadyExists";
        case ErrorCode::AccessDenied:
            return "AccessDenied";
        case ErrorCode::IoError:
            return "IoError";
        case ErrorCode::ParseError:
            return "ParseError";
        case ErrorCode::Unsupported:
            return "Unsupported";
        case ErrorCode::OutOfMemory:
            return "OutOfMemory";
        case ErrorCode::Timeout:
            return "Timeout";
        case ErrorCode::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}
} // namespace Meow2

// Doxygen can't place specializations of std templates (it has no model of namespace std), so this one
// is hidden from the API reference; its behaviour is documented on Meow2::Error.
/// @cond
/// @brief Formats a Meow2::Error as `Code: message` (or `Code` without a message), for `std::format` and logging.
template <>
struct std::formatter<Meow2::Error> : std::formatter<std::string_view>
{
    /// @brief Writes the error to the output.
    /// @param error   The error to format.
    /// @param context Format context provided by `std::format`.
    /// @return Iterator past the written characters.
    auto format(const Meow2::Error& error, std::format_context& context) const
    {
        if (error.message.empty())
        {
            return std::formatter<std::string_view>::format(Meow2::toString(error.code), context);
        }
        return std::format_to(context.out(), "{}: {}", Meow2::toString(error.code), error.message);
    }
};
/// @endcond
