# Runs Meow2ShaderCompiler the way the build does: cooks a copy of shaders/, cooks again (nothing to do), edits an
# include (only its dependents recook), and breaks a shader (exit code 1, the message names file and line).
#
#   cmake -DMEOW2_TOOL=<exe> -DMEOW2_SHADERS=<folder> -DMEOW2_WORK_DIR=<folder> -P TestShaderTool.cmake
file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
file(COPY "${MEOW2_SHADERS}/" DESTINATION "${MEOW2_WORK_DIR}/shaders")
set(meow2Run "${MEOW2_TOOL}" --source "${MEOW2_WORK_DIR}/shaders" --output "${MEOW2_WORK_DIR}/cooked")

execute_process(COMMAND ${meow2Run} RESULT_VARIABLE result OUTPUT_VARIABLE first ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT first MATCHES "([0-9]+) shaders, ([0-9]+) recooked")
    message(FATAL_ERROR "the first cook failed (${result}): ${first} ${errors}")
endif()
set(meow2Shaders "${CMAKE_MATCH_1}")
if(NOT CMAKE_MATCH_2 EQUAL meow2Shaders OR meow2Shaders EQUAL 0)
    message(FATAL_ERROR "the first cook should cook every shader: ${first}")
endif()

execute_process(COMMAND ${meow2Run} RESULT_VARIABLE result OUTPUT_VARIABLE second ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT second MATCHES ", 0 recooked, 0 compiles")
    message(FATAL_ERROR "a second cook should do nothing (${result}): ${second} ${errors}")
endif()

# Appending to a shader recooks that one only.
file(GLOB meow2Hlsl "${MEOW2_WORK_DIR}/shaders/*.hlsl")
list(GET meow2Hlsl 0 meow2Edited)
file(APPEND "${meow2Edited}" "// edited\n")
execute_process(COMMAND ${meow2Run} RESULT_VARIABLE result OUTPUT_VARIABLE third ERROR_VARIABLE errors)
if(NOT result EQUAL 0 OR NOT third MATCHES ", 1 recooked")
    message(FATAL_ERROR "only the edited shader should recook (${result}): ${third} ${errors}")
endif()

# A broken shader stops the build with a usable message.
file(APPEND "${meow2Edited}" "this is not hlsl\n")
get_filename_component(meow2EditedName "${meow2Edited}" NAME)
execute_process(COMMAND ${meow2Run} RESULT_VARIABLE result OUTPUT_VARIABLE fourth ERROR_VARIABLE errors)
if(result EQUAL 0 OR NOT errors MATCHES "shaders/${meow2EditedName}:[0-9]+:[0-9]+: error")
    message(FATAL_ERROR "a broken shader should fail with file and line (${result}): ${fourth} ${errors}")
endif()
file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
