/// @file
/// @brief CPU feature detection (CpuFeatures.h). Compiled without the build's SIMD flags.
///
/// No C++ standard-library templates here (no std::string, no std::vector): their functions are compiled into this
/// file *and* into the AVX2-compiled rest of the program, and the linker keeps one copy, which may be the AVX2 one.
/// Plain C functions (snprintf, strlen) live in the C runtime, compiled once.

#include "Meow2/Core/CpuFeatures.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#define MEOW2_CPU_CHECK_X64 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#else
#define MEOW2_CPU_CHECK_X64 0
#endif

namespace Meow2
{
/// @brief Shows the "this CPU can't run the app" message; per OS (src/Core/<OS>/CpuCheck<OS>.cpp).
void showCpuCheckFailure(const char* message) noexcept;

namespace
{
#if MEOW2_CPU_CHECK_X64
struct CpuidRegisters
{
    std::uint32_t eax = 0;
    std::uint32_t ebx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
};

CpuidRegisters cpuid(std::uint32_t leaf, std::uint32_t subleaf) noexcept
{
    CpuidRegisters registers;
#if defined(_MSC_VER)
    int values[4] = {};
    __cpuidex(values, static_cast<int>(leaf), static_cast<int>(subleaf));
    registers.eax = static_cast<std::uint32_t>(values[0]);
    registers.ebx = static_cast<std::uint32_t>(values[1]);
    registers.ecx = static_cast<std::uint32_t>(values[2]);
    registers.edx = static_cast<std::uint32_t>(values[3]);
#else
    __cpuid_count(leaf, subleaf, registers.eax, registers.ebx, registers.ecx, registers.edx);
#endif
    return registers;
}

/// @brief The OS's XCR0 register: which register sets it saves on a thread switch.
std::uint64_t readXcr0() noexcept
{
#if defined(_MSC_VER)
    return _xgetbv(0);
#else
    // Inline assembly instead of _xgetbv(): GCC only offers that intrinsic with -mxsave, which this file must not use.
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    return (static_cast<std::uint64_t>(high) << 32) | low;
#endif
}

constexpr bool hasBit(std::uint32_t value, int bit) noexcept
{
    return ((value >> bit) & 1U) != 0;
}
#endif

void append(char* buffer, std::size_t size, std::size_t& used, const char* name) noexcept
{
    if (size == 0)
    {
        return;
    }
    const int written = std::snprintf(buffer + used, size - used, "%s%s", used == 0 ? "" : ", ", name);
    if (written > 0)
    {
        used = used + static_cast<std::size_t>(written) < size ? used + static_cast<std::size_t>(written) : size - 1;
    }
}
} // namespace

CpuFeatures detectCpuFeatures() noexcept
{
    CpuFeatures features;
#if MEOW2_CPU_CHECK_X64
    const std::uint32_t maxLeaf = cpuid(0, 0).eax;
    if (maxLeaf < 1)
    {
        return features;
    }
    const CpuidRegisters leaf1 = cpuid(1, 0);
    features.sse42 = hasBit(leaf1.ecx, 20);
    features.fma3 = hasBit(leaf1.ecx, 12);
    features.f16c = hasBit(leaf1.ecx, 29);

    // AVX needs the CPU (bit 28) *and* the OS: OSXSAVE (bit 27) says XGETBV works, and XCR0 bits 1 and 2 say the OS
    // saves the SSE and 256-bit AVX registers. Without that, AVX code would corrupt other threads' registers.
    const bool cpuHasAvx = hasBit(leaf1.ecx, 28);
    const bool osSavesAvx = hasBit(leaf1.ecx, 27) && (readXcr0() & 0x6U) == 0x6U;
    features.avx = cpuHasAvx && osSavesAvx;
    features.fma3 = features.fma3 && features.avx;
    features.f16c = features.f16c && features.avx;
    if (maxLeaf >= 7)
    {
        features.avx2 = features.avx && hasBit(cpuid(7, 0).ebx, 5);
    }
#endif
    return features;
}

CpuFeatures requiredCpuFeatures() noexcept
{
    CpuFeatures required;
    required.sse42 = MEOW2_REQUIRE_SSE42 != 0;
    required.avx = MEOW2_REQUIRE_AVX2 != 0;
    required.avx2 = MEOW2_REQUIRE_AVX2 != 0;
    required.fma3 = MEOW2_REQUIRE_AVX2 != 0;
    required.f16c = MEOW2_REQUIRE_AVX2 != 0;
    return required;
}

std::size_t describeMissingCpuFeatures(const CpuFeatures& required, const CpuFeatures& available, char* buffer,
                                       std::size_t size) noexcept
{
    if (size > 0)
    {
        buffer[0] = '\0';
    }
    std::size_t used = 0;
    std::size_t missing = 0;
    const auto check = [&](bool needed, bool present, const char* name)
    {
        if (needed && !present)
        {
            ++missing;
            append(buffer, size, used, name);
        }
    };
    check(required.sse42, available.sse42, "SSE4.2");
    check(required.avx, available.avx, "AVX");
    check(required.avx2, available.avx2, "AVX2");
    check(required.fma3, available.fma3, "FMA3");
    check(required.f16c, available.f16c, "F16C");
    return missing;
}

bool checkCpuFeatures(const char* appName) noexcept
{
    char missing[128];
    if (describeMissingCpuFeatures(requiredCpuFeatures(), detectCpuFeatures(), missing, sizeof missing) == 0)
    {
        return true;
    }
    char message[512];
    const char* const c_hint = MEOW2_REQUIRE_AVX2 != 0
                                   ? "This build needs a processor with AVX2 (Intel since 2013, AMD since 2015). "
                                     "A build for older processors can be made with MEOW2_SIMD=SSE4.2."
                                   : "This build needs a processor with SSE4.2.";
    std::snprintf(message, sizeof message, "%s can't run on this computer: its processor doesn't support %s.\n\n%s",
                  appName != nullptr ? appName : "This program", missing, c_hint);
    showCpuCheckFailure(message);
    return false;
}
} // namespace Meow2
