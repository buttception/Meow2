# The shader build step (techstack.md §12).
#
#   meow2_add_shaders(<target> SOURCE_DIR <folder> OUTPUT_DIR <folder>)
#     Cooks every shader asset below SOURCE_DIR into OUTPUT_DIR on each build. It always runs: Meow2ShaderCompiler's
#     cache keys know what changed (includes too), which CMake's dependency scanning can't for HLSL, and a run with
#     nothing to do takes milliseconds per shader.
function(meow2_add_shaders target)
    cmake_parse_arguments(arg "" "SOURCE_DIR;OUTPUT_DIR" "" ${ARGN})
    if(NOT arg_SOURCE_DIR OR NOT arg_OUTPUT_DIR)
        message(FATAL_ERROR "meow2_add_shaders: SOURCE_DIR and OUTPUT_DIR are required")
    endif()
    # Under ThreadSanitizer the cook step runs the instrumented compiler during the build, outside ctest, so it gets
    # the same options and suppressions as the tests (CMakePresets.json, linux-clang-tsan).
    set(sanitizerEnvironment "")
    if(MEOW2_SANITIZER STREQUAL "thread")
        set(suppressions "${PROJECT_SOURCE_DIR}/cmake/TsanSuppressions.txt")
        set(sanitizerEnvironment ${CMAKE_COMMAND} -E env
            "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:suppressions=${suppressions}")
    endif()
    add_custom_target(${target} ALL
        COMMAND ${sanitizerEnvironment} Meow2ShaderCompiler --source "${arg_SOURCE_DIR}" --output "${arg_OUTPUT_DIR}"
                "$<$<CONFIG:Debug>:--debug>"
        COMMAND_EXPAND_LISTS
        COMMENT "Cooking shaders in ${arg_SOURCE_DIR}"
        VERBATIM)
    set_target_properties(${target} PROPERTIES FOLDER "Tools")
endfunction()
