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
    VERSION 0.7.0
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

# PDBs in every configuration (techstack.md §5): Release zips ship them, so crash dumps from players can be read.
# /Zi writes the debug info; in Release /DEBUG makes the linker write the PDB, and /OPT:REF,ICF keep the removal of
# unused and duplicate code that /DEBUG would otherwise switch off, so Release stays as small and fast as before.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
if(MSVC)
    add_link_options("$<$<CONFIG:Release,MinSizeRel>:/DEBUG>" "$<$<CONFIG:Release,MinSizeRel>:/OPT:REF>"
                     "$<$<CONFIG:Release,MinSizeRel>:/OPT:ICF>")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Meow2SourceGroups)
include(Meow2Warnings)

# ---- Options ----------------------------------------------------------------
option(MEOW2_BUILD_EDITOR   "Build Meow2Editor"   ON)
option(MEOW2_BUILD_PLAYER   "Build Meow2Player"   ON)
option(MEOW2_BUILD_RENDERER "Build Meow2Renderer" ON)
option(MEOW2_BUILD_TESTS    "Build unit tests (needs the googletest submodule)" ON)
option(MEOW2_RHI_VULKAN    "Vulkan backend of the RHI" ON)
# D3D12 is Windows-only (techstack.md §1), so the option only exists there.
include(CMakeDependentOption)
cmake_dependent_option(MEOW2_RHI_D3D12 "D3D12 backend of the RHI (Windows only)" ON "WIN32" OFF)
option(MEOW2_GPU_TESTS     "Tests that need a GPU and a desktop (labels gpu, visual)" OFF)
option(MEOW2_MSVC_MP       "MSVC: compile a project's files in parallel (/MP), for Visual Studio solutions" OFF)

# /MP lets one cl.exe compile many files at once. Only MSBuild (Visual Studio generators) hands cl.exe many files;
# Ninja starts one cl.exe per file and runs them in parallel itself (-j, CMAKE_BUILD_PARALLEL_LEVEL), so there /MP
# would do nothing.
if(MEOW2_MSVC_MP AND MSVC)
    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/MP>")
    else()
        message(STATUS "MEOW2_MSVC_MP: no effect with ${CMAKE_GENERATOR} (it compiles files in parallel itself)")
    endif()
endif()

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
include(Meow2Sanitizers) # before the external libraries: they must be instrumented too
include(Meow2Simd)       # after the sanitizers, before the external libraries: one instruction set everywhere
include(Meow2External)
include(Meow2Dxc)
include(Meow2SmokeTest)
include(Meow2Subsystem)
include(Meow2Packaging) # meow2_install_app(); meow2_package() is called at the end
include(Meow2AppResources) # meow2_app_icon(), meow2_app_fonts()
include(Meow2Agility)   # after the packaging module: it adds the runtime to the release zip
if(MEOW2_BUILD_TESTS)
    include(Meow2Tests)
endif()
include(Meow2Shaders)
include(Meow2Format)
include(Meow2Docs)
include(Meow2GitHooks)

# ---- Projects ---------------------------------------------------------------
# Next to the exes. Multi-config generators (Visual Studio) put those in bin/<Config>/, so the path has to follow.
get_property(meow2MultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(meow2MultiConfig)
    set(MEOW2_COOKED_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/cooked")
else()
    set(MEOW2_COOKED_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cooked")
endif()
add_subdirectory(Meow2Engine)

if(MEOW2_SHADER_TOOLS)
    add_subdirectory(Meow2ShaderCompiler)
    meow2_add_shaders(Meow2Shaders
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/shaders"
        OUTPUT_DIR "${MEOW2_COOKED_DIR}")
    # Next to the exes, where they're found at runtime, and in the release zip.
    install(DIRECTORY "${MEOW2_COOKED_DIR}" DESTINATION ${MEOW2_INSTALL_DIR} OPTIONAL)
endif()

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

# Release zip: runtime DLLs, documents and CPack (`--target package`), after every app has installed itself.
meow2_package()
