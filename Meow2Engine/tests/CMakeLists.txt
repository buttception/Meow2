# Meow2Engine unit tests — one test executable per module.
meow2_add_test_suite(
    TARGET  Meow2EngineCoreTests
    MODULE  Core
    SOURCES
        Core/AssertTests.cpp
        Core/Config/ConfigTests.cpp
        Core/Log/LogTests.cpp
        Core/Memory/BlockArenaTests.cpp
        Core/Memory/CountingResource.h
        Core/Memory/MallocResourceTests.cpp
        Core/Memory/TrackingAllocatorTests.cpp
        Core/PlatformTests.cpp
        Core/ProcessTests.cpp
        Core/ResultTests.cpp
        Core/VersionTests.cpp)

# The expected version comes straight from CMake, independently of the generated
# header, so the test proves configure_file wired the numbers through correctly.
target_compile_definitions(Meow2EngineCoreTests PRIVATE
    MEOW2_TEST_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MEOW2_TEST_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MEOW2_TEST_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# What the build system says we are building, to check Platform.h against.
target_compile_definitions(Meow2EngineCoreTests PRIVATE
    MEOW2_TEST_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    MEOW2_TEST_BUILD_TYPE="$<CONFIG>")

# Crash handler (Windows only for now): a probe program crashes on purpose in a fresh folder, and
# cmake/RunCrashProbe.cmake checks that a valid .dmp and a log line appeared (techstack.md §8).
if(WIN32)
    add_executable(Meow2CrashProbe Core/Crash/CrashProbe.cpp)
    target_link_libraries(Meow2CrashProbe PRIVATE Meow2::Engine)
    meow2_set_warnings(Meow2CrashProbe)
    set_target_properties(Meow2CrashProbe PROPERTIES FOLDER "Tests")
    meow2_source_groups(Meow2CrashProbe)

    # meow2_add_crash_test(<name> <mode> <expect crash: ON|OFF|genex> [extra -D arguments...])
    # (No backslashes in the -D patterns: CTest escapes them again. Use . to match a parenthesis.)
    function(meow2_add_crash_test name mode expectCrash)
        add_test(NAME Core.CrashHandler.${name}
            COMMAND ${CMAKE_COMMAND}
                "-DMEOW2_PROBE=$<TARGET_FILE:Meow2CrashProbe>"
                "-DMEOW2_DIR=${CMAKE_CURRENT_BINARY_DIR}/crash-tests/$<CONFIG>/${name}"
                "-DMEOW2_MODE=${mode}"
                "-DMEOW2_EXPECT_CRASH=${expectCrash}"
                ${ARGN}
                -P "${PROJECT_SOURCE_DIR}/cmake/RunCrashProbe.cmake")
        set_tests_properties(Core.CrashHandler.${name} PROPERTIES LABELS "integration;windows" TIMEOUT 120)
    endfunction()

    meow2_add_crash_test(NoCrashNoDump none OFF)
    meow2_add_crash_test(AccessViolationWritesDump access-violation ON
        "-DMEOW2_EXPECT_LOG=unhandled exception 0xC0000005")
    meow2_add_crash_test(StackOverflowWritesDump stack-overflow ON
        "-DMEOW2_EXPECT_LOG=unhandled exception 0xC00000FD")
    meow2_add_crash_test(AbortWritesDump abort ON "-DMEOW2_EXPECT_LOG=Crash .abort.*: dump written")
    meow2_add_crash_test(TerminateWritesDump terminate ON "-DMEOW2_EXPECT_LOG=Crash .std::terminate")
    meow2_add_crash_test(CppExceptionWritesDump cpp-exception ON)
    meow2_add_crash_test(InvalidParameterWritesDump invalid-parameter ON
        "-DMEOW2_EXPECT_LOG=CRT invalid parameter")
    meow2_add_crash_test(FatalLogWritesDump fatal ON "-DMEOW2_EXPECT_LOG=<fatal> Probe: probe fatal error 7")
    # Asserts are compiled out of Release: there the probe exits normally.
    meow2_add_crash_test(FailedAssertWritesDump assert "$<IF:$<CONFIG:Release,MinSizeRel>,OFF,ON>"
        "-DMEOW2_EXPECT_LOG=Assertion failed: value == 2")
endif()

# Global operator new/delete replacement: its own executable, built with meow2_track_global_new(), so every `new`
# in it goes through the tracker without changing how the other test suites allocate.
meow2_add_test_suite(
    TARGET  Meow2EngineGlobalNewTests
    MODULE  Core
    SOURCES
        Core/Memory/CountingResource.h
        Core/Memory/GlobalNewTests.cpp)
meow2_track_global_new(Meow2EngineGlobalNewTests)
