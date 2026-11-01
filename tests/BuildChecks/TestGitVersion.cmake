# Checks cmake/GitVersion.cmake (Build.GitVersion.* tests).
# Usage: cmake -DMEOW2_SOURCE_DIR=<repo> -DMEOW2_WORK_DIR=<scratch dir> -DMEOW2_GIT=<git> -DMEOW2_CASE=<case>
#        -P TestGitVersion.cmake
#   CommitHash: in the repository, the metadata is "g" + `git rev-parse --short=7 HEAD` (+ ".dirty" if the tree has
#               local changes), and the full string is "1.2.3+<metadata>".
#   NoGit:      in a folder that isn't a git repository, the metadata is empty and the full string is "1.2.3".

set(script "${MEOW2_SOURCE_DIR}/cmake/GitVersion.cmake")
file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
file(MAKE_DIRECTORY "${MEOW2_WORK_DIR}")
set(header "${MEOW2_WORK_DIR}/BuildMetadata.h")

if(MEOW2_CASE STREQUAL "CommitHash")
    set(sourceDir "${MEOW2_SOURCE_DIR}")
elseif(MEOW2_CASE STREQUAL "NoGit")
    # A plain folder outside the repository (the work dir is in the build tree, which is inside the repository, so
    # a sub-folder of it would still be seen as part of the repo by git; the script must reject that too).
    set(sourceDir "${MEOW2_WORK_DIR}/plain")
    file(MAKE_DIRECTORY "${sourceDir}")
else()
    message(FATAL_ERROR "unknown MEOW2_CASE '${MEOW2_CASE}'")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DMEOW2_SOURCE_DIR=${sourceDir}" -DMEOW2_VERSION=1.2.3 "-DMEOW2_OUTPUT=${header}"
            -P "${script}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "GitVersion.cmake failed (${result})")
endif()
file(READ "${header}" content)
message("${content}")

if(MEOW2_CASE STREQUAL "CommitHash")
    execute_process(COMMAND "${MEOW2_GIT}" rev-parse --short=7 HEAD WORKING_DIRECTORY "${MEOW2_SOURCE_DIR}"
                    OUTPUT_VARIABLE hash OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT content MATCHES "c_buildMetadata = \"g${hash}(\\.dirty)?\"")
        message(FATAL_ERROR "expected metadata g${hash}[.dirty]")
    endif()
    if(NOT content MATCHES "c_fullVersionString = \"1\\.2\\.3\\+g${hash}(\\.dirty)?\"")
        message(FATAL_ERROR "expected the full version 1.2.3+g${hash}[.dirty]")
    endif()
else()
    if(NOT content MATCHES "c_buildMetadata = \"\"" OR NOT content MATCHES "c_fullVersionString = \"1\\.2\\.3\"")
        message(FATAL_ERROR "expected no metadata and the plain version outside a git repository")
    endif()
endif()

# Running again with the same result must not touch the file (no needless rebuilds).
file(TIMESTAMP "${header}" before "%Y%m%d%H%M%S%f")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DMEOW2_SOURCE_DIR=${sourceDir}" -DMEOW2_VERSION=1.2.3 "-DMEOW2_OUTPUT=${header}"
            -P "${script}")
file(TIMESTAMP "${header}" after "%Y%m%d%H%M%S%f")
if(NOT before STREQUAL after)
    message(FATAL_ERROR "the header was rewritten although nothing changed")
endif()
