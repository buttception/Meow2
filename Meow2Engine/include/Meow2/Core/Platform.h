#pragma once

// What is this code being compiled for? Include this instead of testing
// compiler-specific macros (_WIN32, __clang__, NDEBUG, ...) directly.
//
// Every MEOW2_* macro here is ALWAYS defined, as 0 or 1. Test them with #if,
// never #ifdef:
//     #if MEOW2_PLATFORM_WINDOWS      // correct
//     #ifdef MEOW2_PLATFORM_WINDOWS   // WRONG: always true, the macro is defined as 0 or 1
// A misspelled name in #if is caught by -Wundef (GCC/Clang).
//
// Thread safety: compile-time constants only.

// ---- Platform ----------------------------------------------------------------
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
// Stops in the debugger (or crashes with a breakpoint/trap signal when none is attached).
#if MEOW2_COMPILER_MSVC
#define MEOW2_DEBUG_BREAK() __debugbreak()
#elif MEOW2_COMPILER_CLANG
#define MEOW2_DEBUG_BREAK() __builtin_debugtrap()
#else
#include <csignal>
#define MEOW2_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

// Ask the compiler to inline even when its heuristics say no. Use sparingly:
// only for tiny hot functions where a profiler showed the call overhead.
#if MEOW2_COMPILER_MSVC
#define MEOW2_FORCE_INLINE __forceinline
#else
#define MEOW2_FORCE_INLINE inline __attribute__((always_inline))
#endif

namespace Meow2
{
// The same information as values, for logs and `if constexpr`.
inline constexpr const char* c_platformName = MEOW2_PLATFORM_WINDOWS ? "Windows"
                                              : MEOW2_PLATFORM_LINUX ? "Linux"
                                                                     : "Android";
inline constexpr const char* c_compilerName = MEOW2_COMPILER_MSVC ? "MSVC" : MEOW2_COMPILER_CLANG ? "Clang" : "GCC";
inline constexpr const char* c_configName = MEOW2_CONFIG_DEBUG            ? "Debug"
                                            : MEOW2_CONFIG_RELWITHDEBINFO ? "RelWithDebInfo"
                                                                          : "Release";
inline constexpr bool c_isDebugBuild = MEOW2_CONFIG_DEBUG == 1;
} // namespace Meow2
