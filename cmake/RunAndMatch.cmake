# Script mode helper for meow2_add_smoke_test().
# Usage: cmake -DMEOW2_EXE=<path> -DMEOW2_EXPECT=<regex> [-DMEOW2_ARGS=<arg1|arg2|...>] -P RunAndMatch.cmake
string(REPLACE "|" ";" meow2Args "${MEOW2_ARGS}")
execute_process(
    COMMAND "${MEOW2_EXE}" ${meow2Args}
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
