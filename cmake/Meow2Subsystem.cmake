# meow2_gui_subsystem_in_release(<target>)
#
# Windows: in Release (and MinSizeRel) the app is linked for the GUI subsystem, so starting it opens no console
# window next to its own (techstack.md §5, §7). Debug and RelWithDebInfo stay console apps: their log goes to the
# console too. Elsewhere this does nothing (Linux and Android have no such distinction).
#
# A GUI-subsystem exe normally starts at WinMain; /ENTRY:mainCRTStartup keeps the standard int main(argc, argv), so
# main.cpp is the same in every configuration. Output redirected by the caller (a pipe, a file, CTest) still works:
# the C runtime picks up the standard handles the parent passes in, it just never creates a console of its own.
#
# Also registers Build.Subsystem.<name> (labels integration;windows), which reads the subsystem out of the built exe.
function(meow2_gui_subsystem_in_release target)
    if(NOT WIN32)
        return()
    endif()
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE "$<CONFIG:Release,MinSizeRel>")
    if(MSVC)
        target_link_options(${target} PRIVATE "$<$<CONFIG:Release,MinSizeRel>:/ENTRY:mainCRTStartup>")
    endif()

    string(REGEX REPLACE "^Meow2" "" testName "${target}")
    add_test(NAME Build.Subsystem.${testName}
        COMMAND ${CMAKE_COMMAND}
            "-DMEOW2_EXE=$<TARGET_FILE:${target}>"
            "-DMEOW2_EXPECT=$<IF:$<CONFIG:Release,MinSizeRel>,gui,console>"
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckSubsystem.cmake")
    set_tests_properties(Build.Subsystem.${testName} PROPERTIES LABELS "integration;windows")
endfunction()
