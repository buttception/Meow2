#pragma once

/// @file
/// @brief CPU instruction-set detection, and the startup check that a CPU can run this build.
///
/// Every app calls checkCpuFeatures() as the first thing in `main`:
/// @code
/// int main(int argc, char** argv)
/// {
///     if (!Meow2::checkCpuFeatures("Meow2Player"))
///     {
///         return 1; // the user has been told which instruction sets their CPU lacks
///     }
///     // ...
/// }
/// @endcode
/// Without it, a build for AVX2 started on an older CPU crashes with "illegal instruction" the first time the
/// compiler used an AVX2 instruction, with no hint why. This code is compiled without the build's SIMD flags (its own
/// library, Meow2CpuCheck), so it runs on those CPUs.
/// @see techstack.md §10, docs/wiki/Math.md, cmake/Meow2Simd.cmake

#include <cstddef>

namespace Meow2
{
/// @brief Which x64 SIMD instruction sets a CPU (or a build) has. All `false` on non-x64 CPUs. Plain data.
struct CpuFeatures
{
    bool sse42 = false; ///< SSE4.2 (every Windows 11 24H2 PC).
    bool avx = false;   ///< AVX, and the OS saves its registers on a thread switch.
    bool avx2 = false;  ///< AVX2 (needs AVX).
    bool fma3 = false;  ///< Fused multiply-add (comes with AVX2 CPUs).
    bool f16c = false;  ///< Half-float conversion (comes with AVX2 CPUs).
};

/// @brief Asks the CPU (CPUID) and the OS (XGETBV) which instruction sets can be used.
///
/// AVX counts only if the OS also saves the 256-bit registers on a thread switch: a CPU that has AVX under an OS that
/// doesn't is the same as no AVX.
/// @return What this machine supports; all `false` off x64.
/// @par Thread safety
/// Thread-safe (no state).
[[nodiscard]] CpuFeatures detectCpuFeatures() noexcept;

/// @brief What this build was compiled for (CMake's `MEOW2_SIMD`).
/// @return AVX2, AVX, FMA3, F16C and SSE4.2 for the AVX2 baseline; SSE4.2 for the SSE4.2 baseline; nothing off x64.
[[nodiscard]] CpuFeatures requiredCpuFeatures() noexcept;

/// @brief Writes the names of the required features that are missing, e.g. `"AVX2, FMA3"`.
/// @param required  What the build needs.
/// @param available What the machine has.
/// @param[out] buffer Receives the names, comma-separated, null-terminated (truncated if too small).
/// @param size      The buffer's size in bytes; 0 writes nothing.
/// @return How many features are missing; 0 means the machine can run the build.
std::size_t describeMissingCpuFeatures(const CpuFeatures& required, const CpuFeatures& available, char* buffer,
                                       std::size_t size) noexcept;

/// @brief Checks that this machine can run this build; if not, tells the user (a message box on Windows, stderr
///        elsewhere) which instruction sets are missing.
/// @param appName The app's name for the message, e.g. `"Meow2Player"`.
/// @return `true` if the app can start; `false` if it must exit.
/// @par Thread safety
/// Call once, from `main`, before anything else.
[[nodiscard]] bool checkCpuFeatures(const char* appName) noexcept;
} // namespace Meow2
