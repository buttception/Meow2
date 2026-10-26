# Unit-test support: GoogleTest (git submodule) + CTest discovery.
# Included from the top-level CMakeLists.txt when MEOW2_BUILD_TESTS is ON.

set(meow2GoogleTestDir "${PROJECT_SOURCE_DIR}/external/googletest")
if(NOT EXISTS "${meow2GoogleTestDir}/CMakeLists.txt")
    message(FATAL_ERROR
        "GoogleTest is missing (external/googletest is empty).\n"
        "Fetch the submodules with:\n"
        "  git submodule update --init --recursive\n"
        "or configure with -DMEOW2_BUILD_TESTS=OFF.")
endif()

# GoogleTest build switches. Plain (non-cache) variables are enough: googletest's
# option() calls honour them (policy CMP0077), and they don't leak into the cache.
set(gtest_force_shared_crt ON) # MSVC: use /MD like the rest of the project, not /MT
set(INSTALL_GTEST OFF)         # we never install googletest
set(BUILD_GMOCK OFF)           # no mocking needed yet (fakes are chosen at build time)

# EXCLUDE_FROM_ALL: only build gtest when a test target needs it.
# SYSTEM: its headers are treated as system headers, so our strict warnings
#         never fire on third-party code.
add_subdirectory("${meow2GoogleTestDir}" "${PROJECT_BINARY_DIR}/external/googletest" EXCLUDE_FROM_ALL SYSTEM)

include(GoogleTest)

# meow2_add_test_suite(
#     TARGET    <executable name>          e.g. Meow2EngineCoreTests
#     MODULE    <module name>              e.g. Core  -> tests named Core.<Suite>.<Test>
#     SOURCES   <files...>
#     [LABELS   <labels...>]               default: unit
#     [LIBRARIES <extra link targets...>])
#
# Builds one GoogleTest executable linked with gtest_main and Meow2::Engine, and
# registers every TEST() in it as its own CTest test.
function(meow2_add_test_suite)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "TARGET;MODULE" "SOURCES;LABELS;LIBRARIES")
    if(NOT arg_TARGET OR NOT arg_MODULE OR NOT arg_SOURCES)
        message(FATAL_ERROR "meow2_add_test_suite: TARGET, MODULE and SOURCES are required")
    endif()
    if(NOT arg_LABELS)
        set(arg_LABELS unit)
    endif()

    add_executable(${arg_TARGET} ${arg_SOURCES})
    target_link_libraries(${arg_TARGET} PRIVATE GTest::gtest_main Meow2::Engine ${arg_LIBRARIES})

    # TEST(Version, MatchesProjectVersion) in MODULE Core becomes the CTest test
    # "Core.Version.MatchesProjectVersion" (AGENTS.md naming: <Module>.<Unit>.<Behaviour>).
    # PRE_TEST: the test list is read when ctest runs, not right after linking,
    # so a build never runs test binaries (matters for cross-compiling, e.g. Android).
    gtest_discover_tests(${arg_TARGET}
        TEST_PREFIX "${arg_MODULE}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${arg_LABELS}")
endfunction()
