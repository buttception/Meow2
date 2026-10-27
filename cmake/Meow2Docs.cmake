# Doxygen API reference (techstack.md §16.1):
#   cmake --build --preset <p> --target docs     HTML in <build dir>/docs/html/index.html
#   ctest test "Docs.PublicApi.IsDocumented"     fails on any undocumented public declaration
#
# Doxygen is optional on developer machines: without it the target and the test are
# skipped, with a message. In CI (env CI=true, set by GitHub Actions) a missing Doxygen
# fails the configure step instead, so the documentation check can never be skipped
# silently (same approach as Meow2Format.cmake).

# 1.9.8 is what Ubuntu 24.04 (the Linux CI image) ships.
set(meow2DoxygenMinVersion 1.9.8)

if(DEFINED ENV{CI})
    set(meow2DoxygenFailMode FATAL_ERROR)
else()
    set(meow2DoxygenFailMode STATUS)
endif()

find_package(Doxygen ${meow2DoxygenMinVersion} QUIET)
if(DOXYGEN_FOUND)
    message(STATUS "Doxygen ${DOXYGEN_VERSION}: ${DOXYGEN_EXECUTABLE}")
else()
    message(${meow2DoxygenFailMode}
        "Doxygen >= ${meow2DoxygenMinVersion} not found: 'docs' target and Docs.PublicApi.IsDocumented test disabled")
endif()

# meow2_add_api_docs(INPUTS <dir/file>... STRIP <dir>...)
#   Adds the `docs` target over the given public header directories/files and registers
#   the Docs.PublicApi.IsDocumented test. STRIP lists the include roots, so pages show
#   paths as they are #included (Meow2/Core/Platform.h). Does nothing without Doxygen.
function(meow2_add_api_docs)
    if(NOT DOXYGEN_FOUND)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "INPUTS;STRIP")

    set(DOXYGEN_PROJECT_NAME "Meow2")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
    set(DOXYGEN_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/docs")
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/docs/api/MainPage.md")
    set(DOXYGEN_FILE_PATTERNS "*.h")
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_FULL_PATH_NAMES YES)
    set(DOXYGEN_STRIP_FROM_PATH ${arg_STRIP})
    set(DOXYGEN_STRIP_FROM_INC_PATH ${arg_STRIP})
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_HAVE_DOT NO)
    set(DOXYGEN_QUIET YES)

    # Only documented entities appear, and every public one must be documented.
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_IF_DOC_ERROR YES)
    set(DOXYGEN_WARN_IF_INCOMPLETE_DOC YES)
    set(DOXYGEN_WARN_NO_PARAMDOC YES)
    set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)

    # Platform.h selects its macros with #if. Document one consistent configuration
    # (Windows, MSVC, x64, Debug), so every macro is defined exactly once.
    set(DOXYGEN_ENABLE_PREPROCESSING YES)
    set(DOXYGEN_PREDEFINED
        "_WIN32=1"
        "_MSC_VER=1940"
        "_M_X64=1"
        "MEOW2_CONFIG_DEBUG=1"
        "MEOW2_CONFIG_RELWITHDEBINFO=0"
        "MEOW2_CONFIG_RELEASE=0")

    doxygen_add_docs(docs
        ${arg_INPUTS}
        "${PROJECT_SOURCE_DIR}/docs/api/MainPage.md"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating the Meow2 API reference (Doxygen)")

    # Same Doxyfile as the target: warnings (undocumented API, bad @param, …) fail the test.
    add_test(NAME Docs.PublicApi.IsDocumented
        COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.docs"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(Docs.PublicApi.IsDocumented PROPERTIES LABELS "lint")
endfunction()
