# Runs one test of a test executable from an empty folder, so no DXC can be found next to it: proves that loading
# cooked shaders needs no compiler, as in Meow2Player (techstack.md §12).
#
#   cmake -DMEOW2_TEST_EXE=<exe> -DMEOW2_FILTER=<gtest filter> -DMEOW2_WORK_DIR=<folder> -P RunWithoutDxc.cmake
file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
file(MAKE_DIRECTORY "${MEOW2_WORK_DIR}")
file(COPY "${MEOW2_TEST_EXE}" DESTINATION "${MEOW2_WORK_DIR}")
get_filename_component(meow2ExeName "${MEOW2_TEST_EXE}" NAME)
execute_process(COMMAND "${MEOW2_WORK_DIR}/${meow2ExeName}" "--gtest_filter=${MEOW2_FILTER}" RESULT_VARIABLE meow2Result)
file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
if(NOT meow2Result EQUAL 0)
    message(FATAL_ERROR "${MEOW2_FILTER} failed without DXC: ${meow2Result}")
endif()
