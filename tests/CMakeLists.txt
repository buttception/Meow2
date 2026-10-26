# Repository-level tests that check the build itself (not engine code).

if(MEOW2_WARNINGS_AS_ERRORS)
    # EXCLUDE_FROM_ALL: never built by a normal build — only by the test below.
    add_executable(Meow2WarningProbe EXCLUDE_FROM_ALL BuildChecks/WarningProbe.cpp)
    meow2_set_warnings(Meow2WarningProbe)

    add_test(NAME Build.Warnings.AreErrors
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DMEOW2_TARGET=Meow2WarningProbe"
            "-DMEOW2_EXPECT=unused-variable|C4189"
            -P "${PROJECT_SOURCE_DIR}/cmake/BuildShouldFail.cmake")
    set_tests_properties(Build.Warnings.AreErrors PROPERTIES LABELS "integration")
endif()
