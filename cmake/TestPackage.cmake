# Checks a release zip the way a player gets it: unpack it into an empty folder outside the build tree and run every
# app there. They must start, open their window (offscreen, so no desktop is needed), run 3 frames and exit with 0.
#
# Usage: cmake -DMEOW2_ZIP=<zip> -DMEOW2_WORK_DIR=<empty scratch folder> -P TestPackage.cmake
#
# Run it from a plain prompt (no developer prompt, no Qt on PATH): then a DLL missing from the zip can't be found
# elsewhere by accident.

if(NOT EXISTS "${MEOW2_ZIP}")
    message(FATAL_ERROR "zip not found: ${MEOW2_ZIP}")
endif()
file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
file(MAKE_DIRECTORY "${MEOW2_WORK_DIR}")
file(ARCHIVE_EXTRACT INPUT "${MEOW2_ZIP}" DESTINATION "${MEOW2_WORK_DIR}")

get_filename_component(packageName "${MEOW2_ZIP}" NAME_WLE) # WLE: the version has dots in it
set(root "${MEOW2_WORK_DIR}/${packageName}")
if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "the zip has no top-level folder '${packageName}'")
endif()

foreach(file IN ITEMS README.md LICENSE CHANGELOG.md)
    if(NOT EXISTS "${root}/${file}")
        message(FATAL_ERROR "${file} missing from the zip")
    endif()
endforeach()

set(apps Meow2Player Meow2Renderer)
if(EXISTS "${root}/Meow2Editor.exe" OR EXISTS "${root}/Meow2Editor")
    list(APPEND apps Meow2Editor)
endif()

foreach(app IN LISTS apps)
    if(WIN32)
        set(exe "${root}/${app}.exe")
        if(NOT EXISTS "${root}/${app}.pdb")
            message(FATAL_ERROR "${app}.pdb missing: crash dumps from this build couldn't be read")
        endif()
    else()
        set(exe "${root}/${app}")
    endif()
    # Its own log folder and config next to it, as a player would have; offscreen so no desktop is needed.
    execute_process(
        COMMAND "${exe}" --log-dir "${root}/logs" --config "${root}/${app}.toml"
                --set platform.videoDriver=offscreen --set app.quitAfterFrames=3
        WORKING_DIRECTORY "${root}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        TIMEOUT 60)
    message("${app}: ${output}")
    if(NOT result EQUAL 0 OR NOT output MATCHES "Ran 3 frames")
        message(FATAL_ERROR "${app} didn't run from the unpacked zip (exit '${result}')")
    endif()
endforeach()
message("Package OK: ${packageName} (${apps})")
