# Meow2Engine unit tests — one test executable per module.
meow2_add_test_suite(
    TARGET  Meow2EngineCoreTests
    MODULE  Core
    SOURCES
        Core/AssertTests.cpp
        Core/Log/LogTests.cpp
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
