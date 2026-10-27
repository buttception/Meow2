#pragma once

/// @file
/// @brief Asserts: `MEOW2_ASSERT`, `MEOW2_ASSERT_SLOW` and `MEOW2_VERIFY`, and the replaceable failure handler.
///
/// | Macro                 | Debug   | RelWithDebInfo | Release                         |
/// |-----------------------|---------|----------------|---------------------------------|
/// | `MEOW2_ASSERT`        | checked | checked        | removed (expression not run)    |
/// | `MEOW2_ASSERT_SLOW`   | checked | removed        | removed                         |
/// | `MEOW2_VERIFY`        | checked | checked        | expression runs, result ignored |
///
/// Use `MEOW2_ASSERT` for conditions that can only be false because of a bug (an invariant, a precondition).
/// Use `MEOW2_VERIFY` when the expression must run in every build because it has a side effect, e.g.
/// `MEOW2_VERIFY(file.close())`. Errors that can happen in a correct program (a missing file, bad user input) are
/// not asserts: return a `Result` instead.
///
/// @code
/// MEOW2_ASSERT(index < m_count);
/// MEOW2_ASSERT(index < m_count, "index out of range");     // message: a string literal
/// MEOW2_ASSERT_SLOW(isSorted(items), "items must be sorted");
/// MEOW2_VERIFY(m_device.waitIdle());
/// @endcode
///
/// On failure, the current AssertHandler gets an AssertInfo and decides what happens (AssertAction). The default
/// handler prints the failure to `stderr`, then stops in the debugger if one is attached and aborts otherwise.
/// The logger installs its own handler later, and tests can install one that records failures.
/// @par Thread safety
/// Asserts can fail on any thread. setAssertHandler() is thread-safe, and the handler is called on the failing
/// thread, so it must be thread-safe itself.

#include <Meow2/Core/Platform.h>

/// @def MEOW2_ASSERTS_ENABLED
/// @brief 1 when `MEOW2_ASSERT` and `MEOW2_VERIFY` check their condition: Debug and RelWithDebInfo by default.
///
/// Can be forced by defining it (0 or 1) on the compiler command line, e.g. for a Release build used in testing.

/// @def MEOW2_SLOW_ASSERTS_ENABLED
/// @brief 1 when `MEOW2_ASSERT_SLOW` checks its condition: Debug only by default. Can be forced like
///        MEOW2_ASSERTS_ENABLED.

#ifndef MEOW2_ASSERTS_ENABLED
#if MEOW2_CONFIG_DEBUG || MEOW2_CONFIG_RELWITHDEBINFO
#define MEOW2_ASSERTS_ENABLED 1
#else
#define MEOW2_ASSERTS_ENABLED 0
#endif
#endif

#ifndef MEOW2_SLOW_ASSERTS_ENABLED
#if MEOW2_CONFIG_DEBUG
#define MEOW2_SLOW_ASSERTS_ENABLED 1
#else
#define MEOW2_SLOW_ASSERTS_ENABLED 0
#endif
#endif

namespace Meow2
{
/// @brief `true` when `MEOW2_ASSERT` and `MEOW2_VERIFY` check their condition (see MEOW2_ASSERTS_ENABLED).
inline constexpr bool c_assertsEnabled = MEOW2_ASSERTS_ENABLED == 1;

/// @brief `true` when `MEOW2_ASSERT_SLOW` checks its condition (see MEOW2_SLOW_ASSERTS_ENABLED).
inline constexpr bool c_slowAssertsEnabled = MEOW2_SLOW_ASSERTS_ENABLED == 1;

/// @brief Everything known about a failed assert. Plain data, valid only during the handler call.
struct AssertInfo
{
    const char* expression; ///< The failed condition as written in the code, e.g. `"index < m_count"`.
    const char* message;    ///< The message given to the macro, or an empty string. Never null.
    const char* file;       ///< Source file of the assert (`__FILE__`).
    int line;               ///< Line of the assert (`__LINE__`).
    const char* function;   ///< Name of the function containing the assert (`__func__`).
};

/// @brief What should happen after a failed assert has been reported.
enum class AssertAction
{
    Break,    ///< Stop in the debugger at the assert's line (`MEOW2_DEBUG_BREAK`), then continue if resumed.
    Continue, ///< Ignore the failure and keep running. For tests and "assert once" handlers.
    Abort     ///< Terminate the program with `std::abort()` (the crash handler, once installed, writes a dump).
};

/// @brief Function called for every failed assert. It decides what happens next.
///
/// A plain function pointer, not a `std::function`: it can be called from any state (out of memory, a corrupted
/// heap) without allocating.
/// @param info The failure. Its pointers are valid only during the call.
/// @return What to do next.
using AssertHandler = AssertAction (*)(const AssertInfo& info);

/// @brief The built-in handler: prints the failure to `stderr`, then Break if a debugger is attached, otherwise
///        Abort.
/// @param info The failure.
/// @return AssertAction::Break with a debugger attached, AssertAction::Abort without.
AssertAction defaultAssertHandler(const AssertInfo& info);

/// @brief Replaces the handler that failed asserts are reported to.
///
/// Called by the composition root (e.g. to route failures through the logger) and by tests.
/// @param handler The new handler, or `nullptr` to restore defaultAssertHandler().
/// @return The previous handler (never null), so it can be restored later.
AssertHandler setAssertHandler(AssertHandler handler);

/// @brief Implementation details of the assert macros. Not for direct use.
namespace Detail
{
/// @brief Reports a failed assert to the current handler. Called only by the assert macros.
///
/// An assert that fails while a handler is already running on the same thread aborts immediately, so a failing
/// handler can't recurse forever.
/// @param info The failure.
/// @return `true` if the caller should break into the debugger (AssertAction::Break), `false` to continue.
///         AssertAction::Abort doesn't return.
bool reportAssertFailure(const AssertInfo& info);
} // namespace Detail
} // namespace Meow2

/// @def MEOW2_ASSERT(expression, ...)
/// @brief Checks that `expression` is true in Debug and RelWithDebInfo builds. Removed from Release builds.
///
/// @param expression Condition that must hold. Not evaluated when asserts are disabled, so it must not have side
///                   effects (use MEOW2_VERIFY for those). It is still compiled, so it can't silently rot.
/// @param ...        Optional message, which must be a **string literal**: `MEOW2_ASSERT(p, "no device")`.
///
/// On failure, the current AssertHandler is called. If it returns AssertAction::Break, the debugger stops on this
/// line (not inside the handler).

/// @def MEOW2_ASSERT_SLOW(expression, ...)
/// @brief Like MEOW2_ASSERT, but for expensive checks (e.g. validating a whole container): only in Debug builds.
/// @param expression Condition that must hold. Not evaluated when slow asserts are disabled.
/// @param ...        Optional message (a string literal).

/// @def MEOW2_VERIFY(expression, ...)
/// @brief Evaluates `expression` in **every** build, and checks that it's true where asserts are enabled.
///
/// For expressions whose side effect is needed even in Release: `MEOW2_VERIFY(file.close())`.
/// @param expression Expression to evaluate; its result is checked only when asserts are enabled.
/// @param ...        Optional message (a string literal).

/// @def MEOW2_DETAIL_ASSERT_CHECK(expression, ...)
/// @brief Internal: the enabled form of the assert macros. Use MEOW2_ASSERT, MEOW2_ASSERT_SLOW or MEOW2_VERIFY.
/// @param expression Condition to check.
/// @param ...        Optional message (a string literal).

/// @def MEOW2_DETAIL_ASSERT_IGNORE(expression)
/// @brief Internal: the disabled form of the assert macros (compiles `expression`, never evaluates it).
/// @param expression Condition that is type-checked but not run.

// The message is passed as `"" __VA_ARGS__`: with no message it is "", with a message the two
// literals are concatenated. A non-literal message (a variable) is a compile error on purpose:
// formatting with arguments arrives with logging.
#define MEOW2_DETAIL_ASSERT_CHECK(expression, ...)                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression)) [[unlikely]]                                                                                \
        {                                                                                                              \
            if (::Meow2::Detail::reportAssertFailure(                                                                  \
                    ::Meow2::AssertInfo{#expression, "" __VA_ARGS__, __FILE__, __LINE__, __func__}))                   \
            {                                                                                                          \
                MEOW2_DEBUG_BREAK();                                                                                   \
            }                                                                                                          \
        }                                                                                                              \
    } while (false)

// Disabled: sizeof() keeps the expression compiled (typos and renamed variables are still
// errors, and its variables count as used) without evaluating it.
#define MEOW2_DETAIL_ASSERT_IGNORE(expression)                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        static_cast<void>(sizeof(!(expression)));                                                                      \
    } while (false)

#if MEOW2_ASSERTS_ENABLED
#define MEOW2_ASSERT(expression, ...) MEOW2_DETAIL_ASSERT_CHECK(expression, __VA_ARGS__)
#define MEOW2_VERIFY(expression, ...) MEOW2_DETAIL_ASSERT_CHECK(expression, __VA_ARGS__)
#else
#define MEOW2_ASSERT(expression, ...) MEOW2_DETAIL_ASSERT_IGNORE(expression)
#define MEOW2_VERIFY(expression, ...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        static_cast<void>(expression);                                                                                 \
    } while (false)
#endif

#if MEOW2_SLOW_ASSERTS_ENABLED
#define MEOW2_ASSERT_SLOW(expression, ...) MEOW2_DETAIL_ASSERT_CHECK(expression, __VA_ARGS__)
#else
#define MEOW2_ASSERT_SLOW(expression, ...) MEOW2_DETAIL_ASSERT_IGNORE(expression)
#endif
