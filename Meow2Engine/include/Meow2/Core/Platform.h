#pragma once

/// @file
/// @brief What the code is being compiled for: platform, compiler, CPU architecture and build configuration.
///
/// Include this instead of testing compiler-specific macros (`_WIN32`, `__clang__`, `NDEBUG`, …) directly.
///
/// Every `MEOW2_*` macro here is **always defined, as 0 or 1**. Test them with `#if`, never `#ifdef`:
/// @code
/// #if MEOW2_PLATFORM_WINDOWS      // correct
/// #ifdef MEOW2_PLATFORM_WINDOWS   // WRONG: always true, the macro is defined as 0 or 1
/// @endcode
/// A misspelled name in `#if` is caught by `-Wundef` (GCC/Clang).
///
/// The build-configuration macros `MEOW2_CONFIG_DEBUG`, `MEOW2_CONFIG_RELWITHDEBINFO` and `MEOW2_CONFIG_RELEASE`
/// are not defined here: CMake defines them (0 or 1) on the `Meow2::Engine` target, so anything linking the engine
/// gets them. Exactly one of them is 1; this header stops the build with `#error` otherwise.
///
/// @note The generated API documentation shows the values for Windows, MSVC, x64, Debug.
/// @par Thread safety
/// Compile-time constants only.

// ---- Platform ----------------------------------------------------------------

/// @def MEOW2_PLATFORM_WINDOWS
/// @brief 1 when compiling for Windows (32- or 64-bit), otherwise 0.

/// @def MEOW2_PLATFORM_ANDROID
/// @brief 1 when compiling for Android, otherwise 0.

/// @def MEOW2_PLATFORM_LINUX
/// @brief 1 when compiling for desktop Linux, otherwise 0. Android is excluded, although the compiler also defines
///        `__linux__` there.

#if defined(_WIN32)
#define MEOW2_PLATFORM_WINDOWS 1
#else
#define MEOW2_PLATFORM_WINDOWS 0
#endif

#if defined(__ANDROID__)
#define MEOW2_PLATFORM_ANDROID 1
#else
#define MEOW2_PLATFORM_ANDROID 0
#endif

// Android is also Linux to the compiler (__linux__ is defined), so exclude it.
#if defined(__linux__) && !defined(__ANDROID__)
#define MEOW2_PLATFORM_LINUX 1
#else
#define MEOW2_PLATFORM_LINUX 0
#endif

#if MEOW2_PLATFORM_WINDOWS + MEOW2_PLATFORM_LINUX + MEOW2_PLATFORM_ANDROID != 1
#error "Meow2: unsupported platform (expected Windows, Linux or Android)"
#endif

// ---- Compiler ----------------------------------------------------------------

/// @def MEOW2_COMPILER_CLANG
/// @brief 1 when compiling with Clang, including clang-cl on Windows, otherwise 0.

/// @def MEOW2_COMPILER_MSVC
/// @brief 1 when compiling with Microsoft's `cl.exe`, otherwise 0. clang-cl counts as Clang.

/// @def MEOW2_COMPILER_GCC
/// @brief 1 when compiling with GCC, otherwise 0.

// Check Clang first: clang-cl on Windows also defines _MSC_VER, and Clang
// defines __GNUC__ everywhere.
#if defined(__clang__)
#define MEOW2_COMPILER_CLANG 1
#define MEOW2_COMPILER_MSVC 0
#define MEOW2_COMPILER_GCC 0
#elif defined(_MSC_VER)
#define MEOW2_COMPILER_CLANG 0
#define MEOW2_COMPILER_MSVC 1
#define MEOW2_COMPILER_GCC 0
#elif defined(__GNUC__)
#define MEOW2_COMPILER_CLANG 0
#define MEOW2_COMPILER_MSVC 0
#define MEOW2_COMPILER_GCC 1
#else
#error "Meow2: unsupported compiler (expected MSVC, Clang or GCC)"
#endif

// ---- CPU architecture --------------------------------------------------------

/// @def MEOW2_ARCH_X64
/// @brief 1 when compiling for 64-bit x86 (x64 / AMD64), otherwise 0.

/// @def MEOW2_ARCH_ARM64
/// @brief 1 when compiling for 64-bit ARM (arm64 / AArch64), otherwise 0.

#if defined(_M_X64) || defined(__x86_64__)
#define MEOW2_ARCH_X64 1
#define MEOW2_ARCH_ARM64 0
#elif defined(_M_ARM64) || defined(__aarch64__)
#define MEOW2_ARCH_X64 0
#define MEOW2_ARCH_ARM64 1
#else
#error "Meow2: unsupported CPU architecture (expected x64 or arm64)"
#endif

// ---- Build configuration -----------------------------------------------------
// Defined by CMake on the Meow2::Engine target (PUBLIC), so anything linking
// the engine gets them. See Meow2Engine/CMakeLists.txt.
#if !defined(MEOW2_CONFIG_DEBUG) || !defined(MEOW2_CONFIG_RELWITHDEBINFO) || !defined(MEOW2_CONFIG_RELEASE)
// Link against Meow2::Engine instead of only adding its include path.
#error "Meow2: MEOW2_CONFIG_* are not defined by the build system"
#endif

#if MEOW2_CONFIG_DEBUG + MEOW2_CONFIG_RELWITHDEBINFO + MEOW2_CONFIG_RELEASE != 1
// CMAKE_BUILD_TYPE must be Debug, RelWithDebInfo, Release (or MinSizeRel, treated as Release).
#error "Meow2: exactly one MEOW2_CONFIG_* must be 1"
#endif

// ---- Helpers -----------------------------------------------------------------

/// @def MEOW2_DEBUG_BREAK()
/// @brief Stops in the debugger at this line.
///
/// Without a debugger attached, the program crashes with a breakpoint exception (Windows) or a `SIGTRAP` signal
/// (Linux/Android). Used by asserts and fatal errors.

#if MEOW2_COMPILER_MSVC
#define MEOW2_DEBUG_BREAK() __debugbreak()
#elif MEOW2_COMPILER_CLANG
#define MEOW2_DEBUG_BREAK() __builtin_debugtrap()
#else
#include <csignal>
#define MEOW2_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

/// @def MEOW2_FORCE_INLINE
/// @brief Asks the compiler to inline a function even when its heuristics say no.
///
/// Use sparingly: only for tiny hot functions where a profiler showed the call overhead matters.

#if MEOW2_COMPILER_MSVC
#define MEOW2_FORCE_INLINE __forceinline
#else
#define MEOW2_FORCE_INLINE inline __attribute__((always_inline))
#endif

/// @brief Root namespace of the Meow2 engine.
namespace Meow2
{
/// @brief Name of the target platform (`"Windows"`, `"Linux"` or `"Android"`), for logs and `if constexpr`.
inline constexpr const char* c_platformName = MEOW2_PLATFORM_WINDOWS ? "Windows"
                                              : MEOW2_PLATFORM_LINUX ? "Linux"
                                                                     : "Android";

/// @brief Name of the compiler (`"MSVC"`, `"Clang"` or `"GCC"`), for logs and `if constexpr`.
inline constexpr const char* c_compilerName = MEOW2_COMPILER_MSVC ? "MSVC" : MEOW2_COMPILER_CLANG ? "Clang" : "GCC";

/// @brief Name of the build configuration (`"Debug"`, `"RelWithDebInfo"` or `"Release"`).
inline constexpr const char* c_configName = MEOW2_CONFIG_DEBUG            ? "Debug"
                                            : MEOW2_CONFIG_RELWITHDEBINFO ? "RelWithDebInfo"
                                                                          : "Release";

/// @brief `true` in Debug builds (no optimisation, all asserts), `false` in RelWithDebInfo and Release.
inline constexpr bool c_isDebugBuild = MEOW2_CONFIG_DEBUG == 1;
} // namespace Meow2
