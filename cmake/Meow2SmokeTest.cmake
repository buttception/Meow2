# meow2_add_smoke_test(<name> <target> <regex>)
#
# Registers a CTest test that runs <target> and passes only if BOTH:
#   - the process exits with code 0, and
#   - its stdout matches <regex>.
#
# CTest's own PASS_REGULAR_EXPRESSION property ignores the exit code, so a
# program that printed the right text and then crashed would still "pass".
# Running it through RunAndMatch.cmake checks both.
function(meow2_add_smoke_test name target regex)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_EXE=$<TARGET_FILE:${target}>"
            "-DMEOW2_EXPECT=${regex}"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunAndMatch.cmake")
    set_tests_properties(${name} PROPERTIES LABELS "smoke")
endfunction()
