# Third-party libraries from external/ (git submodules, techstack.md §4) used by the engine.
# GoogleTest is set up separately in Meow2Tests.cmake (tests only).
#
# Every library is added with EXCLUDE_FROM_ALL (only built when something links it) and SYSTEM
# (its headers are system headers, so our strict warnings never fire on its code). Their targets
# never get meow2_set_warnings(). They are linked PRIVATE: no third-party header appears in
# Meow2's public headers (AGENTS.md §3). The exceptions are tl::expected (Result<T> is part of
# the public API until C++23 std::expected) and, later, DirectXMath.

# meow2_require_submodule(<name> <dir>)
#   Stops with a helpful message when a submodule folder is empty (cloned without
#   --recurse-submodules).
function(meow2_require_submodule name dir)
    if(NOT EXISTS "${dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "${name} is missing (${dir} is empty).\n"
            "Fetch the submodules with:\n"
            "  git submodule update --init --recursive")
    endif()
endfunction()

# ---- tl::expected (Result<T>, techstack.md §3) ---------------------------------------------------
# Header-only. Its own CMakeLists.txt includes CTest and enables its tests (downloading Catch2),
# which would add CDash targets to our build, so we define a small INTERFACE target over its
# include folder instead of add_subdirectory(). Include it only through <Meow2/Core/Result.h>.
set(meow2ExpectedDir "${PROJECT_SOURCE_DIR}/external/expected")
meow2_require_submodule(tl::expected "${meow2ExpectedDir}")

add_library(meow2_tl_expected INTERFACE)
target_include_directories(meow2_tl_expected SYSTEM INTERFACE "${meow2ExpectedDir}/include")
add_library(tl::expected ALIAS meow2_tl_expected)

# ---- spdlog (logging, techstack.md §7) ----------------------------------------------------------
set(meow2SpdlogDir "${PROJECT_SOURCE_DIR}/external/spdlog")
meow2_require_submodule(spdlog "${meow2SpdlogDir}")

set(SPDLOG_BUILD_SHARED OFF)            # static library, linked into Meow2Engine
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_INSTALL OFF)
set(SPDLOG_DISABLE_DEFAULT_LOGGER ON)   # no global default logger: loggers are injected (§18)
set(SPDLOG_PREVENT_CHILD_FD ON)         # child processes (tools we launch, death tests) don't inherit log files
if(WIN32)
    # File names as wchar_t on Windows, so log folders with non-ASCII characters work
    # (the narrow Windows file APIs use the ANSI code page, not UTF-8).
    set(SPDLOG_WCHAR_FILENAMES ON)
endif()

add_subdirectory("${meow2SpdlogDir}" "${PROJECT_BINARY_DIR}/external/spdlog" EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(spdlog PROPERTIES FOLDER "External/spdlog") # IDE solution folder

# ---- toml++ (config files, techstack.md §3) -----------------------------------------------------
# Header-only. As with tl::expected, its own CMakeLists.txt is skipped (it would add install and test
# targets); a small INTERFACE target over its include folder is enough. Only src/Core/Config/Config.cpp
# includes it. TOML_EXCEPTIONS=0: parse errors come back as a value we turn into Result<T>, not as a throw.
set(meow2TomlDir "${PROJECT_SOURCE_DIR}/external/tomlplusplus")
meow2_require_submodule(toml++ "${meow2TomlDir}")

add_library(meow2_tomlplusplus INTERFACE)
target_include_directories(meow2_tomlplusplus SYSTEM INTERFACE "${meow2TomlDir}/include")
target_compile_definitions(meow2_tomlplusplus INTERFACE TOML_EXCEPTIONS=0)
add_library(tomlplusplus::tomlplusplus ALIAS meow2_tomlplusplus)
