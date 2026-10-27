# clang-format integration:
#   cmake --build --preset <p> --target format        rewrite files in place
#   cmake --build --preset <p> --target format-check  fail if anything is unformatted
#   ctest test "Format.Check"                         same check, as part of the test run
#
# Formats only Meow2's own code (Meow2*/, tests/), never external/ or build/.
#
# The clang-format major version is pinned: different majors format some edge cases
# differently, so Format.Check could pass on one machine and fail on another. With any
# other version the format targets are skipped locally; in CI (env CI=true, set by
# GitHub Actions) a missing or wrong clang-format fails the configure step, so the
# check can never be skipped silently.

set(meow2ClangFormatRequiredMajor 19)

if(DEFINED ENV{CI})
    set(meow2ClangFormatFailMode FATAL_ERROR)
else()
    set(meow2ClangFormatFailMode STATUS)
endif()

find_program(MEOW2_CLANG_FORMAT
    NAMES clang-format-${meow2ClangFormatRequiredMajor} clang-format
    HINTS
        # Visual Studio 2022's copies (the Developer Prompt sets these variables)
        "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin"
        "$ENV{DevEnvDir}VC/vcpackages"
    DOC "clang-format executable")

if(NOT MEOW2_CLANG_FORMAT)
    message(${meow2ClangFormatFailMode}
        "clang-format ${meow2ClangFormatRequiredMajor} not found: 'format' targets and Format.Check test disabled")
    return()
endif()

execute_process(COMMAND "${MEOW2_CLANG_FORMAT}" --version OUTPUT_VARIABLE meow2ClangFormatVersion)
string(REGEX MATCH "version ([0-9]+)" _ "${meow2ClangFormatVersion}")
set(meow2ClangFormatMajor "${CMAKE_MATCH_1}")
if(NOT meow2ClangFormatMajor EQUAL meow2ClangFormatRequiredMajor)
    message(${meow2ClangFormatFailMode}
        "clang-format ${meow2ClangFormatMajor} found (${MEOW2_CLANG_FORMAT}), but Meow2 pins version "
        "${meow2ClangFormatRequiredMajor}: format targets disabled")
    return()
endif()
message(STATUS "clang-format ${meow2ClangFormatMajor}: ${MEOW2_CLANG_FORMAT}")

set(meow2FormatGlobs)
foreach(dir Meow2Engine Meow2Renderer Meow2Editor Meow2Player tests)
    foreach(ext h hpp cpp inl)
        list(APPEND meow2FormatGlobs "${PROJECT_SOURCE_DIR}/${dir}/*.${ext}")
    endforeach()
endforeach()
# CONFIGURE_DEPENDS: the build re-globs and re-configures when files are added/removed.
file(GLOB_RECURSE meow2FormatFiles CONFIGURE_DEPENDS ${meow2FormatGlobs})

add_custom_target(format
    COMMAND "${MEOW2_CLANG_FORMAT}" -i ${meow2FormatFiles}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Formatting Meow2 sources"
    VERBATIM)

add_custom_target(format-check
    COMMAND "${MEOW2_CLANG_FORMAT}" --dry-run --Werror ${meow2FormatFiles}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Checking Meow2 source formatting"
    VERBATIM)

add_test(NAME Format.Check
    COMMAND "${MEOW2_CLANG_FORMAT}" --dry-run --Werror ${meow2FormatFiles}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
set_tests_properties(Format.Check PROPERTIES LABELS "lint")
