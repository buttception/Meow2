# DXC, the HLSL compiler (techstack.md §4, §12): a pinned Microsoft release, not a submodule (building it takes hours).
#
# Configure downloads the release into build/_dxc/<version>/ (shared by all presets of a checkout), checks its SHA-256
# and unpacks it. The download is retried and can be cached (cmake/Meow2Download.cmake, MEOW2_DOWNLOAD_CACHE). Every developer and CI job gets the same compiler, so cooked shaders don't depend on which SDK a
# machine happens to have.
#
#   MEOW2_SHADER_TOOLS=OFF   no DXC, no download: no way to compile shaders (cooked ones still load).
#   MEOW2_DXC_DIR=<folder>   use an already unpacked release, e.g. on a machine without internet.
#
# Development only: nothing here is installed, so DXC isn't in the release zip (techstack.md §4).
option(MEOW2_SHADER_TOOLS "Download DXC and build the shader tools" ON)
set(MEOW2_DXC_DIR "" CACHE PATH "An unpacked DXC release to use instead of downloading it")

if(NOT MEOW2_SHADER_TOOLS)
    message(STATUS "DXC: off (MEOW2_SHADER_TOOLS=OFF)")
    return()
endif()
include(Meow2Download)

set(meow2DxcVersion "v1.9.2609")
set(meow2DxcBaseUrl
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${meow2DxcVersion}")
if(WIN32)
    set(meow2DxcArchive "dxc_2026_09_29.zip")
    set(meow2DxcHash "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1")
else()
    set(meow2DxcArchive "linux_dxc_2026_09_28.x86_x64.tar.gz")
    set(meow2DxcHash "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1")
endif()

if(MEOW2_DXC_DIR)
    set(meow2DxcDir "${MEOW2_DXC_DIR}")
else()
    set(meow2DxcDir "${PROJECT_SOURCE_DIR}/build/_dxc/${meow2DxcVersion}")
    if(NOT EXISTS "${meow2DxcDir}/.complete")
        message(STATUS "DXC: downloading ${meow2DxcArchive}")
        set(meow2DxcFile "${PROJECT_SOURCE_DIR}/build/_dxc/${meow2DxcArchive}")
        meow2_download(NAME "DXC" URL "${meow2DxcBaseUrl}/${meow2DxcArchive}" FILE "${meow2DxcFile}"
                       SHA256 ${meow2DxcHash}
                       HINT "Without internet, pass -DMEOW2_DXC_DIR=<unpacked release> or -DMEOW2_SHADER_TOOLS=OFF.")
        file(REMOVE_RECURSE "${meow2DxcDir}")
        file(ARCHIVE_EXTRACT INPUT "${meow2DxcFile}" DESTINATION "${meow2DxcDir}")
        file(REMOVE "${meow2DxcFile}")
        # Written last: an interrupted unpack is redone by the next configure.
        file(WRITE "${meow2DxcDir}/.complete" "${meow2DxcVersion}\n")
    endif()
endif()

if(WIN32)
    set(MEOW2_DXC_EXECUTABLE "${meow2DxcDir}/bin/x64/dxc.exe")
    set(meow2DxcIncludeDir "${meow2DxcDir}/inc")
    set(MEOW2_DXC_LIBRARY_DIR "${meow2DxcDir}/bin/x64")
    set(meow2DxcRuntime "${meow2DxcDir}/bin/x64/dxcompiler.dll" "${meow2DxcDir}/bin/x64/dxil.dll")
else()
    set(MEOW2_DXC_EXECUTABLE "${meow2DxcDir}/bin/dxc")
    set(meow2DxcIncludeDir "${meow2DxcDir}/include/dxc")
    set(MEOW2_DXC_LIBRARY_DIR "${meow2DxcDir}/lib")
    set(meow2DxcRuntime "${meow2DxcDir}/lib/libdxcompiler.so" "${meow2DxcDir}/lib/libdxil.so")
endif()
if(NOT EXISTS "${MEOW2_DXC_EXECUTABLE}")
    message(FATAL_ERROR "DXC: no compiler at ${MEOW2_DXC_EXECUTABLE}")
endif()
message(STATUS "DXC: ${meow2DxcVersion} in ${meow2DxcDir}")

add_library(meow2_dxc_headers INTERFACE)
target_include_directories(meow2_dxc_headers SYSTEM INTERFACE "${meow2DxcIncludeDir}")
add_library(Dxc::Headers ALIAS meow2_dxc_headers)

# meow2_copy_dxc_runtime(<target>)
#   Copies the compiler libraries next to an executable that compiles shaders, where ShaderCompiler looks for them.
function(meow2_copy_dxc_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${meow2DxcRuntime} "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
endfunction()
