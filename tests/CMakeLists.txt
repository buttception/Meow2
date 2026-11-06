# Repository-level tests that check the build itself (not engine code).

if(MEOW2_WARNINGS_AS_ERRORS)
    # EXCLUDE_FROM_ALL: never built by a normal build — only by the test below.
    add_executable(Meow2WarningProbe EXCLUDE_FROM_ALL BuildChecks/WarningProbe.cpp)
    meow2_set_warnings(Meow2WarningProbe)
    set_target_properties(Meow2WarningProbe PROPERTIES FOLDER "Tests")
    meow2_source_groups(Meow2WarningProbe)

    add_test(NAME Build.Warnings.AreErrors
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DMEOW2_TARGET=Meow2WarningProbe"
            "-DMEOW2_EXPECT=unused-variable|C4189"
            -P "${PROJECT_SOURCE_DIR}/cmake/BuildShouldFail.cmake")
    set_tests_properties(Build.Warnings.AreErrors PROPERTIES LABELS "integration")
endif()

# The pinned DXC runs (cmake/Meow2Dxc.cmake): the download, its hash and the unpacking all worked.
if(MEOW2_SHADER_TOOLS)
    add_test(NAME Build.Dxc.PinnedCompilerRuns COMMAND "${MEOW2_DXC_EXECUTABLE}" --version)
    set_tests_properties(Build.Dxc.PinnedCompilerRuns PROPERTIES
        LABELS "integration"
        PASS_REGULAR_EXPRESSION "1\.9"
        ENVIRONMENT "LD_LIBRARY_PATH=${MEOW2_DXC_LIBRARY_DIR}")
endif()

# The build step end to end: cook, nothing to do, one shader edited, a broken shader.
if(MEOW2_SHADER_TOOLS)
    add_test(NAME Build.Shaders.CookStep
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_TOOL=$<TARGET_FILE:Meow2ShaderCompiler>"
            "-DMEOW2_SHADERS=${PROJECT_SOURCE_DIR}/shaders"
            "-DMEOW2_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/shader-tool"
            -P "${PROJECT_SOURCE_DIR}/cmake/TestShaderTool.cmake")
    set_tests_properties(Build.Shaders.CookStep PROPERTIES LABELS "integration")
endif()

# .editorconfig basics for every tracked text file (needs git to list them).
find_package(Git QUIET)
if(GIT_FOUND)
    add_test(NAME Lint.EditorConfig.TextFilesFollowRules
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DMEOW2_GIT=${GIT_EXECUTABLE}"
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckTextFiles.cmake")
    set_tests_properties(Lint.EditorConfig.TextFilesFollowRules PROPERTIES LABELS "lint")

    # The git commit in the version string (cmake/GitVersion.cmake): the real hash here, nothing outside a repo.
    foreach(meow2Case IN ITEMS CommitHash NoGit)
        add_test(NAME Build.GitVersion.${meow2Case}
            COMMAND ${CMAKE_COMMAND}
                "-DMEOW2_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DMEOW2_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/git-version/${meow2Case}"
                "-DMEOW2_GIT=${GIT_EXECUTABLE}"
                -DMEOW2_CASE=${meow2Case}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/BuildChecks/TestGitVersion.cmake")
        set_tests_properties(Build.GitVersion.${meow2Case} PROPERTIES LABELS "integration")
    endforeach()
else()
    message(STATUS "git not found: Lint.EditorConfig.TextFilesFollowRules disabled")
endif()

# Release notes from CHANGELOG.md (cmake/ReleaseNotes.cmake, used by release.yml).
foreach(meow2Case IN ITEMS ExtractsSection MissingVersion)
    add_test(NAME Build.ReleaseNotes.${meow2Case}
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DMEOW2_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-notes/${meow2Case}"
            -DMEOW2_CASE=${meow2Case}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/BuildChecks/TestReleaseNotes.cmake")
    set_tests_properties(Build.ReleaseNotes.${meow2Case} PROPERTIES LABELS "integration")
endforeach()
