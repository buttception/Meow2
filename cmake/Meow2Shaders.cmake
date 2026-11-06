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
    add_custom_target(${target} ALL
        COMMAND Meow2ShaderCompiler --source "${arg_SOURCE_DIR}" --output "${arg_OUTPUT_DIR}"
                "$<$<CONFIG:Debug>:--debug>"
        COMMAND_EXPAND_LISTS
        COMMENT "Cooking shaders in ${arg_SOURCE_DIR}"
        VERBATIM)
    set_target_properties(${target} PROPERTIES FOLDER "Tools")
endfunction()
