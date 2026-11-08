# Downloads of pinned third-party files at configure time (DXC, the Agility SDK): checked by SHA-256, retried when a
# server hiccups, and optionally kept in a cache folder so the next configure, checkout or CI job reuses them.
#
#   MEOW2_DOWNLOAD_CACHE=<folder>   -D on the command line, or an environment variable of that name. Every verified
#                                   download is copied there (named by its hash, so versions can't collide), and looked
#                                   for there first. A cached file is used only if its hash still matches. Unset: no
#                                   cache. CI sets it on the self-hosted Windows runner (docs/wiki/CI.md).
#   MEOW2_DOWNLOAD_PAUSES=<list>    seconds to wait before each retry; default 2;5;10 (four attempts). Tests use 0s.
#
# Works in project and script mode (the tests run it with cmake -P).
include_guard(GLOBAL)

if(NOT DEFINED MEOW2_DOWNLOAD_CACHE)
    set(MEOW2_DOWNLOAD_CACHE "$ENV{MEOW2_DOWNLOAD_CACHE}")
endif()

# meow2_download(NAME <label> URL <url> FILE <path> SHA256 <hash> [HINT <text>])
#   Puts the file at <path>, from the cache or downloaded, and stops configure with <text> if every attempt fails.
function(meow2_download)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;URL;FILE;SHA256;HINT" "")
    foreach(required NAME URL FILE SHA256)
        if(NOT arg_${required})
            message(FATAL_ERROR "meow2_download: ${required} is missing")
        endif()
    endforeach()
    string(TOLOWER "${arg_SHA256}" expected)
    get_filename_component(fileName "${arg_FILE}" NAME)
    get_filename_component(fileDir "${arg_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${fileDir}")

    set(cached "")
    if(MEOW2_DOWNLOAD_CACHE)
        set(cached "${MEOW2_DOWNLOAD_CACHE}/${expected}-${fileName}")
        if(EXISTS "${cached}")
            file(SHA256 "${cached}" actual)
            if(actual STREQUAL expected)
                message(STATUS "${arg_NAME}: from the download cache (${MEOW2_DOWNLOAD_CACHE})")
                file(COPY_FILE "${cached}" "${arg_FILE}")
                return()
            endif()
            message(STATUS "${arg_NAME}: the cached copy doesn't match its hash; downloading it again")
            file(REMOVE "${cached}")
        endif()
    endif()

    message(STATUS "${arg_NAME}: downloading ${fileName}")
    # A failed attempt waits a little longer each time: a server's brief hiccup is usually over within seconds.
    if(DEFINED MEOW2_DOWNLOAD_PAUSES)
        set(pauses ${MEOW2_DOWNLOAD_PAUSES})
    else()
        set(pauses 2 5 10)
    endif()
    set(attempt 1)
    while(TRUE)
        # Not file(DOWNLOAD ... EXPECTED_HASH): when the transfer itself fails, CMake then stops configure on the spot
        # ("cannot compute hash on failed download") instead of reporting it in STATUS, so nothing could be retried.
        # The hash is checked separately, and a wrong file counts as a failed attempt too.
        file(DOWNLOAD "${arg_URL}" "${arg_FILE}" TLS_VERIFY ON STATUS status)
        list(GET status 0 result)
        list(GET status 1 error)
        if(result EQUAL 0)
            file(SHA256 "${arg_FILE}" actual)
            if(actual STREQUAL expected)
                break()
            endif()
            set(error "the SHA-256 is ${actual}, expected ${expected}")
        endif()
        file(REMOVE "${arg_FILE}")
        list(LENGTH pauses retriesLeft)
        if(retriesLeft EQUAL 0)
            message(FATAL_ERROR "${arg_NAME}: download failed after ${attempt} attempts: ${error}\n${arg_HINT}")
        endif()
        list(POP_FRONT pauses pause)
        message(STATUS "${arg_NAME}: attempt ${attempt} failed (${error}); trying again in ${pause} s")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${pause})
        math(EXPR attempt "${attempt} + 1")
    endwhile()

    if(cached)
        file(MAKE_DIRECTORY "${MEOW2_DOWNLOAD_CACHE}")
        # Under a temporary name first: a job stopped halfway never leaves a partial file under the real one (and the
        # hash check would catch it anyway).
        file(COPY_FILE "${arg_FILE}" "${cached}.partial")
        file(RENAME "${cached}.partial" "${cached}")
    endif()
endfunction()
