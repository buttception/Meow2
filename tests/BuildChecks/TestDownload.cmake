# Script mode: checks meow2_download() (cmake/Meow2Download.cmake) with file:// URLs, so no network is needed.
# Usage: cmake -DMEOW2_SOURCE_DIR=<repo> -DMEOW2_WORK_DIR=<empty folder> -DMEOW2_CASE=<case> -P TestDownload.cmake
#
#   FetchesAndCaches       a download arrives and leaves a copy in the cache
#   UsesTheCache           a good cached copy is used, so a URL that doesn't exist doesn't matter
#   IgnoresABadCachedCopy  a cached copy with the wrong content is replaced by a fresh download
#   FailsAfterRetries      a URL that doesn't exist fails after every attempt, with the hint (checked by the test's
#                          PASS_REGULAR_EXPRESSION, since configure stops with an error)
cmake_minimum_required(VERSION 3.25)

file(REMOVE_RECURSE "${MEOW2_WORK_DIR}")
file(MAKE_DIRECTORY "${MEOW2_WORK_DIR}/source")
set(source "${MEOW2_WORK_DIR}/source/archive.bin")
file(WRITE "${source}" "the pinned file\n")
file(SHA256 "${source}" hash)

# file:///C:/path on Windows, file:///path elsewhere.
function(fileUrl path out)
    if(path MATCHES "^/")
        set(${out} "file://${path}" PARENT_SCOPE)
    else()
        set(${out} "file:///${path}" PARENT_SCOPE)
    endif()
endfunction()
fileUrl("${source}" goodUrl)
fileUrl("${MEOW2_WORK_DIR}/source/missing.bin" missingUrl)

set(MEOW2_DOWNLOAD_PAUSES 0 0 0) # the tests don't wait between attempts
set(cache "${MEOW2_WORK_DIR}/cache")
set(cachedName "${cache}/${hash}-archive.bin")
set(target "${MEOW2_WORK_DIR}/out/archive.bin")
if(MEOW2_CASE STREQUAL "FailsAfterRetries")
    set(MEOW2_DOWNLOAD_CACHE "")
else()
    set(MEOW2_DOWNLOAD_CACHE "${cache}")
endif()
include("${MEOW2_SOURCE_DIR}/cmake/Meow2Download.cmake")

function(expectFile path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing: ${path}")
    endif()
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL hash)
        message(FATAL_ERROR "wrong content: ${path}")
    endif()
endfunction()

if(MEOW2_CASE STREQUAL "FetchesAndCaches")
    meow2_download(NAME "Test" URL "${goodUrl}" FILE "${target}" SHA256 ${hash})
    expectFile("${target}")
    expectFile("${cachedName}")
elseif(MEOW2_CASE STREQUAL "UsesTheCache")
    file(MAKE_DIRECTORY "${cache}")
    file(COPY_FILE "${source}" "${cachedName}")
    meow2_download(NAME "Test" URL "${missingUrl}" FILE "${target}" SHA256 ${hash})
    expectFile("${target}")
elseif(MEOW2_CASE STREQUAL "IgnoresABadCachedCopy")
    file(WRITE "${cachedName}" "something else\n")
    meow2_download(NAME "Test" URL "${goodUrl}" FILE "${target}" SHA256 ${hash})
    expectFile("${target}")
    expectFile("${cachedName}")
elseif(MEOW2_CASE STREQUAL "FailsAfterRetries")
    meow2_download(NAME "Test" URL "${missingUrl}" FILE "${target}" SHA256 ${hash} HINT "Pass -DTEST_OFFLINE=ON.")
    message(FATAL_ERROR "the download should have failed")
else()
    message(FATAL_ERROR "unknown case '${MEOW2_CASE}'")
endif()
message(STATUS "${MEOW2_CASE}: ok")
