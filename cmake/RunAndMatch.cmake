# Script mode helper for meow2_add_smoke_test().
# Usage: cmake -DMEOW2_EXE=<path> -DMEOW2_EXPECT=<regex> -P RunAndMatch.cmake
execute_process(
    COMMAND "${MEOW2_EXE}"
    RESULT_VARIABLE exitCode
    OUTPUT_VARIABLE stdOut
    ERROR_VARIABLE stdErr)

message("${stdOut}${stdErr}")

if(NOT exitCode EQUAL 0)
    message(FATAL_ERROR "${MEOW2_EXE} exited with '${exitCode}' (expected 0)")
endif()
if(NOT stdOut MATCHES "${MEOW2_EXPECT}")
    message(FATAL_ERROR "Output did not match regex: ${MEOW2_EXPECT}")
endif()
