# Checks cmake/ReleaseNotes.cmake (Build.ReleaseNotes.* tests) on a sample CHANGELOG.
# Usage: cmake -DMEOW2_SOURCE_DIR=<repo> -DMEOW2_WORK_DIR=<scratch dir> -DMEOW2_CASE=ExtractsSection|MissingVersion
#        -P TestReleaseNotes.cmake

file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
file(MAKE_DIRECTORY "${MEOW2_WORK_DIR}")
set(changelog "${MEOW2_WORK_DIR}/CHANGELOG.md")
set(notes "${MEOW2_WORK_DIR}/notes.md")
file(WRITE "${changelog}" "# Changelog

Intro text.

## [Unreleased]

### Added
- Something new (#99).

## [0.2.0] — 2026-11-02

### Added
- First feature; with a semicolon (#10).

- After a blank line (#11).

### Fixed
- A fix (#12).

## [0.1.0] — 2026-10-27

### Added
- Old stuff (#1).
")

if(MEOW2_CASE STREQUAL "ExtractsSection")
    set(version 0.2.0)
elseif(MEOW2_CASE STREQUAL "MissingVersion")
    set(version 9.9.9)
else()
    message(FATAL_ERROR "unknown MEOW2_CASE '${MEOW2_CASE}'")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -DMEOW2_VERSION=${version} "-DMEOW2_CHANGELOG=${changelog}" "-DMEOW2_OUTPUT=${notes}"
            -P "${MEOW2_SOURCE_DIR}/cmake/ReleaseNotes.cmake"
    RESULT_VARIABLE result
    OUTPUT_QUIET ERROR_QUIET)

if(MEOW2_CASE STREQUAL "MissingVersion")
    if(result EQUAL 0)
        message(FATAL_ERROR "a version without a CHANGELOG section must fail")
    endif()
    return()
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "ReleaseNotes.cmake failed (${result})")
endif()
file(READ "${notes}" text)
message("${text}")
set(expected "### Added
- First feature; with a semicolon (#10).

- After a blank line (#11).

### Fixed
- A fix (#12).
")
if(NOT text STREQUAL expected)
    message(FATAL_ERROR "unexpected notes; expected:\n${expected}")
endif()
