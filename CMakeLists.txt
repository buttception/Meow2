# Meow2 — top-level build script.
# See techstack.md §2 for the project layout and docs/wiki/Building.md for usage.
cmake_minimum_required(VERSION 3.25)

# Building inside the source tree would scatter generated files between the
# sources (and they would show up in git). Fail early, before project().
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. Use a separate build directory, e.g.\n"
        "  cmake -S . -B build/debug\n"
        "Then delete the CMakeCache.txt and CMakeFiles/ that were just created here.")
endif()

project(Meow2
    VERSION 0.1.0
    DESCRIPTION "Modular C++20 game engine"
    LANGUAGES CXX)

# ---- Build type -------------------------------------------------------------
# Single-config generators (Ninja, Makefiles) build with no optimisation *and*
# no debug info when CMAKE_BUILD_TYPE is empty, and Meow2/Core/Platform.h would
# reject it. Default to Debug. (Presets always set it; multi-config IDEs pick per build.)
get_property(meow2IsMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT meow2IsMultiConfig AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, RelWithDebInfo or Release" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Debug")
endif()

# ---- Language ---------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # error instead of silently using an older standard
set(CMAKE_CXX_EXTENSIONS OFF)        # -std=c++20, not -std=gnu++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # compile_commands.json for clangd/clang-tidy

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Meow2SourceGroups)
include(Meow2Warnings)

# ---- Options ----------------------------------------------------------------
option(MEOW2_BUILD_EDITOR   "Build Meow2Editor"   ON)
option(MEOW2_BUILD_PLAYER   "Build Meow2Player"   ON)
option(MEOW2_BUILD_RENDERER "Build Meow2Renderer" ON)
option(MEOW2_BUILD_TESTS    "Build unit tests (needs the googletest submodule)" ON)

# ---- Output layout ----------------------------------------------------------
# All executables (and later DLLs such as Qt's) land in one bin/ folder so they
# can find each other at runtime without PATH tweaks.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# ---- Testing ----------------------------------------------------------------
# enable_testing() (not include(CTest)) — we only need add_test/ctest, not the
# CDash dashboard targets that include(CTest) would add.
enable_testing()
include(Meow2External)
include(Meow2SmokeTest)
if(MEOW2_BUILD_TESTS)
    include(Meow2Tests)
endif()
include(Meow2Format)
include(Meow2Docs)
include(Meow2GitHooks)

# ---- Projects ---------------------------------------------------------------
add_subdirectory(Meow2Engine)

if(MEOW2_BUILD_EDITOR)
    add_subdirectory(Meow2Editor)
endif()
if(MEOW2_BUILD_PLAYER)
    add_subdirectory(Meow2Player)
endif()
if(MEOW2_BUILD_RENDERER)
    add_subdirectory(Meow2Renderer)
endif()

add_subdirectory(tests)
