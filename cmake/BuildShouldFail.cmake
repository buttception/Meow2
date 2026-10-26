# Script mode helper for tests that expect a build to FAIL with a specific message.
# Usage: cmake -DMEOW2_BUILD_DIR=<dir> -DMEOW2_TARGET=<t> -DMEOW2_EXPECT=<regex> -P BuildShouldFail.cmake
#
# Passes only if the build exits non-zero AND its output matches the regex, so
# neither "it built fine" nor "it failed for some unrelated reason" counts as a pass.
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${MEOW2_BUILD_DIR}" --target "${MEOW2_TARGET}"
    RESULT_VARIABLE exitCode
    OUTPUT_VARIABLE buildOutput
    ERROR_VARIABLE buildOutput)

message("${buildOutput}")

if(exitCode EQUAL 0)
    message(FATAL_ERROR "Building ${MEOW2_TARGET} succeeded, but it was expected to fail")
endif()
if(NOT buildOutput MATCHES "${MEOW2_EXPECT}")
    message(FATAL_ERROR "Build failed, but not with the expected message: ${MEOW2_EXPECT}")
endif()
