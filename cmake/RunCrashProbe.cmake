# Script mode: runs Meow2CrashProbe in a fresh folder and checks the crash was dumped and logged.
# Usage:
#   cmake -DMEOW2_PROBE=<exe> -DMEOW2_DIR=<folder> -DMEOW2_MODE=<mode> -DMEOW2_EXPECT_CRASH=ON|OFF
#         [-DMEOW2_EXPECT_LOG=<regex>] -P RunCrashProbe.cmake
#
# Expected crash: the probe exits non-zero, exactly one .dmp exists, it starts with the minidump
# signature "MDMP" and isn't trivially small, and the log says where the dump was written.
# No crash: exit code 0 and no .dmp.

cmake_minimum_required(VERSION 3.25) # script mode: enables modern policies

file(REMOVE_RECURSE "${MEOW2_DIR}")
execute_process(
    COMMAND "${MEOW2_PROBE}" "${MEOW2_DIR}" "${MEOW2_MODE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 60)
message(STATUS "Meow2CrashProbe ${MEOW2_MODE}: exit '${result}'\n${errors}")

file(GLOB dumps "${MEOW2_DIR}/*.dmp")
file(GLOB logs "${MEOW2_DIR}/*.log")
list(LENGTH dumps dumpCount)
set(logContent "")
if(logs)
    list(GET logs 0 logFile)
    file(READ "${logFile}" logContent)
endif()

if(NOT MEOW2_EXPECT_CRASH)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "expected a normal exit, got '${result}'")
    endif()
    if(NOT dumpCount EQUAL 0)
        message(FATAL_ERROR "expected no dump, found: ${dumps}")
    endif()
    return()
endif()

if(result STREQUAL "0")
    message(FATAL_ERROR "expected the probe to crash, but it exited normally")
endif()
if(NOT dumpCount EQUAL 1)
    message(FATAL_ERROR "expected exactly one .dmp in ${MEOW2_DIR}, found ${dumpCount}\nLog:\n${logContent}")
endif()

list(GET dumps 0 dump)
file(READ "${dump}" signature LIMIT 4 HEX)
if(NOT signature STREQUAL "4d444d50") # "MDMP"
    message(FATAL_ERROR "${dump} isn't a minidump (starts with ${signature})")
endif()
file(SIZE "${dump}" dumpSize)
if(dumpSize LESS 4096)
    message(FATAL_ERROR "${dump} is suspiciously small (${dumpSize} bytes)")
endif()

# The reason itself may contain parentheses ("abort()"), so match up to "): dump written to".
if(NOT logContent MATCHES "<error> Probe: Crash \\(.*\\): dump written to ")
    message(FATAL_ERROR "the log doesn't report the dump:\n${logContent}")
endif()
if(DEFINED MEOW2_EXPECT_LOG AND NOT logContent MATCHES "${MEOW2_EXPECT_LOG}")
    message(FATAL_ERROR "the log doesn't match '${MEOW2_EXPECT_LOG}':\n${logContent}")
endif()
message(STATUS "OK: ${dump} (${dumpSize} bytes)")
