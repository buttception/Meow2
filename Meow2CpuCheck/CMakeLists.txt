# Meow2CpuCheck: tells the user, before anything else runs, that their CPU lacks an instruction set this build
# needs (cmake/Meow2Simd.cmake). It is its own library so it can be compiled WITHOUT the build's SIMD flags: this
# code has to run on exactly the CPUs the rest of the program can't run on. Added before those flags are set.
add_library(Meow2CpuCheck STATIC
    src/Core/CpuFeatures.cpp)
target_sources(Meow2CpuCheck
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/Meow2/Core/CpuFeatures.h)

if(WIN32)
    target_sources(Meow2CpuCheck PRIVATE src/Core/Win32/CpuCheckWin32.cpp)
else()
    target_sources(Meow2CpuCheck PRIVATE src/Core/Linux/CpuCheckLinux.cpp)
endif()

# What this build was compiled for, so the check knows what to require.
if(meow2SimdIsX64 AND MEOW2_SIMD STREQUAL "AVX2")
    target_compile_definitions(Meow2CpuCheck PRIVATE MEOW2_REQUIRE_AVX2=1 MEOW2_REQUIRE_SSE42=1)
elseif(meow2SimdIsX64)
    target_compile_definitions(Meow2CpuCheck PRIVATE MEOW2_REQUIRE_AVX2=0 MEOW2_REQUIRE_SSE42=1)
else()
    target_compile_definitions(Meow2CpuCheck PRIVATE MEOW2_REQUIRE_AVX2=0 MEOW2_REQUIRE_SSE42=0)
endif()

meow2_set_warnings(Meow2CpuCheck)
set_target_properties(Meow2CpuCheck PROPERTIES FOLDER "Engine")
meow2_source_groups(Meow2CpuCheck)
