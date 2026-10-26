# Script mode: checks that tracked text files follow .editorconfig's basics.
# Usage: cmake -DMEOW2_SOURCE_DIR=<repo> -DMEOW2_GIT=<git> -P CheckTextFiles.cmake
#
# Rules (for text files tracked by git, outside external/):
#   - ends with a newline           (insert_final_newline)
#   - no trailing spaces/tabs       (trim_trailing_whitespace)
#   - no tab indentation            (indent_style = space; .gitmodules excepted)
#   - no CR characters              (end_of_line = lf; CRLF file types excepted)

cmake_minimum_required(VERSION 3.25) # script mode: enables modern policies (e.g. if(IN_LIST))

execute_process(
    COMMAND "${MEOW2_GIT}" ls-files
    WORKING_DIRECTORY "${MEOW2_SOURCE_DIR}"
    OUTPUT_VARIABLE trackedFiles
    RESULT_VARIABLE gitResult)
if(NOT gitResult EQUAL 0)
    message(FATAL_ERROR "git ls-files failed in ${MEOW2_SOURCE_DIR}")
endif()
string(REPLACE "\n" ";" trackedFiles "${trackedFiles}")

set(textExtensions .h .hpp .cpp .inl .in .cmake .txt .md .json .yml .yaml)
set(textNames .gitignore .gitattributes .gitmodules .editorconfig .clang-format LICENSE)
set(crlfExtensions .bat .cmd .ps1 .sln .vcxproj .props)

# Returns the 1-based line number of character offset `offset` in `content`.
function(lineOf content offset outVar)
    string(SUBSTRING "${content}" 0 ${offset} before)
    string(REGEX MATCHALL "\n" newlines "${before}")
    list(LENGTH newlines count)
    math(EXPR line "${count} + 1")
    set(${outVar} ${line} PARENT_SCOPE)
endfunction()

set(problems "")
set(checkedCount 0)
foreach(relPath IN LISTS trackedFiles)
    if(relPath STREQUAL "" OR relPath MATCHES "^external/")
        continue()
    endif()
    get_filename_component(name "${relPath}" NAME)
    get_filename_component(ext "${relPath}" LAST_EXT)
    if(NOT ext IN_LIST textExtensions AND NOT name IN_LIST textNames AND NOT ext IN_LIST crlfExtensions)
        continue()
    endif()
    set(path "${MEOW2_SOURCE_DIR}/${relPath}")
    if(IS_DIRECTORY "${path}" OR NOT EXISTS "${path}")
        continue() # submodule entries, files deleted in the working tree
    endif()
    math(EXPR checkedCount "${checkedCount} + 1")
    file(READ "${path}" content)

    if(content STREQUAL "")
        continue()
    endif()
    if(NOT content MATCHES "\n$")
        list(APPEND problems "${relPath}: missing final newline")
    endif()

    string(FIND "${content}" " \n" spacePos)
    string(FIND "${content}" "\t\n" tabPos)
    if(spacePos GREATER -1 OR tabPos GREATER -1)
        if(spacePos EQUAL -1 OR (tabPos GREATER -1 AND tabPos LESS spacePos))
            set(spacePos ${tabPos})
        endif()
        lineOf("${content}" ${spacePos} line)
        list(APPEND problems "${relPath}:${line}: trailing whitespace")
    endif()

    if(NOT name STREQUAL ".gitmodules")
        string(FIND "${content}" "\n\t" indentPos)
        if(indentPos GREATER -1)
            math(EXPR indentPos "${indentPos} + 1")
            lineOf("${content}" ${indentPos} line)
            list(APPEND problems "${relPath}:${line}: tab indentation (use spaces)")
        endif()
    endif()

    if(NOT ext IN_LIST crlfExtensions)
        # file(READ) silently drops CR characters, so look at the raw bytes instead:
        # any byte 0x0D at an even (byte-aligned) position of the hex dump.
        file(READ "${path}" hexContent HEX)
        if(hexContent MATCHES "^(..)*0d")
            list(APPEND problems "${relPath}: CR/CRLF line endings (use LF)")
        endif()
    endif()
endforeach()

if(problems)
    list(LENGTH problems problemCount)
    list(JOIN problems "\n  " problemText)
    message(FATAL_ERROR "${problemCount} text-file problem(s):\n  ${problemText}")
endif()
message(STATUS "Checked ${checkedCount} text files: OK")
