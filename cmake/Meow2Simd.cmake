# SIMD instruction set for the whole build (techstack.md §10). x64 only; ARM uses NEON, DirectXMath's default there.
#
#   MEOW2_SIMD=AVX2    (default) AVX2 + FMA3 + F16C: DirectXMath's fastest paths. Any x64 CPU from about 2013 on
#                       (Intel Haswell, AMD Excavator/Zen); not some low-end Pentium/Celeron/Atom models.
#   MEOW2_SIMD=SSE4.2  For older CPUs: every PC that runs Windows 11 24H2 has it.
#
# The flags go on **every** target, third-party ones included: an inline function (DirectXMath's, or any
# template) compiled with different instruction sets in different files is still one function to the linker,
# which keeps one copy. If it kept the AVX2 copy, the SSE build of a caller would crash on an old CPU (an ODR
# violation). Included after the sanitizers and before the external libraries.
#
# The one exception is Meow2CpuCheck: it tells the user their CPU is too old, so it must run on that CPU. Its
# subdirectory is added *before* add_compile_options below, so the flags don't reach it.
set(MEOW2_SIMD "AVX2" CACHE STRING "x64 SIMD baseline: AVX2 (with FMA3 and F16C) or SSE4.2")
set_property(CACHE MEOW2_SIMD PROPERTY STRINGS AVX2 SSE4.2)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$")
    if(NOT MEOW2_SIMD MATCHES "^(AVX2|SSE4\\.2)$")
        message(FATAL_ERROR "MEOW2_SIMD must be AVX2 or SSE4.2, not '${MEOW2_SIMD}'")
    endif()
    set(meow2SimdIsX64 ON)
else()
    set(meow2SimdIsX64 OFF)
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/Meow2CpuCheck" "${PROJECT_BINARY_DIR}/Meow2CpuCheck")

if(NOT meow2SimdIsX64)
    message(STATUS "SIMD: ${CMAKE_SYSTEM_PROCESSOR}: compiler default (NEON on ARM)")
    return()
endif()

if(MEOW2_SIMD STREQUAL "AVX2")
    if(MSVC)
        # /arch:AVX2 also lets MSVC use FMA3 and defines __AVX2__, from which DirectXMath turns on its AVX2, FMA3 and
        # F16C paths.
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-mavx2 -mfma -mf16c)
    endif()
else()
    if(NOT MSVC)
        add_compile_options(-msse4.2)
    endif()
    # MSVC on x64 always has the SSE4 intrinsics; DirectXMath only needs to be told to use them.
    add_compile_definitions(_XM_SSE4_INTRINSICS_)
endif()
message(STATUS "SIMD: x64 baseline ${MEOW2_SIMD}")
