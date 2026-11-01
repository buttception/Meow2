# meow2_add_smoke_test(<name> <target> <regex> [ARGS <arguments...>])
#
# Registers a CTest test that runs <target> (with ARGS, if given) and passes only if BOTH:
#   - the process exits with code 0, and
#   - its stdout matches <regex>.
#
# CTest's own PASS_REGULAR_EXPRESSION property ignores the exit code, so a
# program that printed the right text and then crashed would still "pass".
# Running it through RunAndMatch.cmake checks both.
function(meow2_add_smoke_test name target regex)
    cmake_parse_arguments(PARSE_ARGV 3 arg "" "" "ARGS")
    # The list travels as one -D value; "|" separates the arguments because ";" would be split by add_test.
    list(JOIN arg_ARGS "|" meow2SmokeArgs)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_EXE=$<TARGET_FILE:${target}>"
            "-DMEOW2_EXPECT=${regex}"
            "-DMEOW2_ARGS=${meow2SmokeArgs}"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunAndMatch.cmake")
    set_tests_properties(${name} PROPERTIES LABELS "smoke")
endfunction()
