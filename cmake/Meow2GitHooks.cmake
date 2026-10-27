# Points git at the repository's hooks in .githooks/ (core.hooksPath), so that after a pull,
# rebase or branch switch the Visual Studio solution is regenerated when CMake files changed
# (.githooks/regenerate-vs-solution.sh). Visual Studio then offers to reload.
#
# - Local repository config only (.git/config), shared by all worktrees of this clone.
# - Never overrides a hooks path the user set to something else.
# - Skipped in CI, so build machines' checkouts aren't modified.
# - Turn off with -DMEOW2_GIT_HOOKS=OFF (and `git config --unset core.hooksPath` to undo).

option(MEOW2_GIT_HOOKS "Set git core.hooksPath to .githooks (regenerate the VS solution after pull/checkout)" ON)

if(MEOW2_GIT_HOOKS AND NOT DEFINED ENV{CI} AND EXISTS "${PROJECT_SOURCE_DIR}/.githooks")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" config --get core.hooksPath
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE meow2CurrentHooksPath
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE meow2GitConfigResult
            ERROR_QUIET)
        if(meow2CurrentHooksPath STREQUAL "")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" config core.hooksPath .githooks
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                RESULT_VARIABLE meow2GitConfigResult)
            if(meow2GitConfigResult EQUAL 0)
                message(STATUS "git hooks: core.hooksPath set to .githooks (regenerates the VS solution after pull; "
                    "-DMEOW2_GIT_HOOKS=OFF to skip)")
            endif()
        elseif(NOT meow2CurrentHooksPath STREQUAL ".githooks")
            message(STATUS "git hooks: core.hooksPath is '${meow2CurrentHooksPath}', left unchanged "
                "(Meow2's hooks are in .githooks/)")
        endif()
    endif()
endif()
