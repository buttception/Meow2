# The DirectX 12 Agility SDK (techstack.md §4, §11): Microsoft's D3D12 runtime as a DLL the app ships, instead of the
# one built into Windows. It brings Shader Model 6.6 (ResourceDescriptorHeap, the bindless contract) and enhanced
# barriers to Windows 10, and makes every PC run the same runtime.
#
# Configure downloads the pinned NuGet package into build/_agility/<version>/, checks its SHA-256 and unpacks it, the
# same way as DXC (cmake/Meow2Dxc.cmake). It isn't a submodule: Microsoft only publishes it as a package.
#
#   MEOW2_AGILITY_DIR=<folder>   use an already unpacked package, e.g. on a machine without internet.
#
# meow2_use_agility(<target>) makes an executable load it: the exports Windows looks for, and the DLLs copied into a
# D3D12/ folder next to the exe. The release zip gets D3D12Core.dll and the package's licence (the package's
# "distributable files.txt" lists D3D12Core.dll, d3d12SDKLayers.dll and d3dconfig.exe as redistributable).
if(NOT MEOW2_RHI_D3D12)
    return()
endif()

set(MEOW2_AGILITY_DIR "" CACHE PATH "An unpacked Microsoft.Direct3D.D3D12 package to use instead of downloading it")
set(meow2AgilityVersion "1.620.1")
set(MEOW2_AGILITY_SDK_VERSION 620) # D3D12_SDK_VERSION in that package's d3d12.h, and in DirectX-Headers v1.620.1
set(meow2AgilityHash "a8a494c3e5f687d9be0a465ec8248da70781ab689d888db8806b600422722be6")
set(meow2AgilityUrl
    "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/${meow2AgilityVersion}/microsoft.direct3d.d3d12.${meow2AgilityVersion}.nupkg")

if(MEOW2_AGILITY_DIR)
    set(meow2AgilityDir "${MEOW2_AGILITY_DIR}")
else()
    set(meow2AgilityDir "${PROJECT_SOURCE_DIR}/build/_agility/${meow2AgilityVersion}")
    if(NOT EXISTS "${meow2AgilityDir}/.complete")
        message(STATUS "Agility SDK: downloading ${meow2AgilityVersion}")
        set(meow2AgilityFile "${PROJECT_SOURCE_DIR}/build/_agility/agility-${meow2AgilityVersion}.zip")
        file(DOWNLOAD "${meow2AgilityUrl}" "${meow2AgilityFile}"
             EXPECTED_HASH SHA256=${meow2AgilityHash} TLS_VERIFY ON STATUS meow2AgilityStatus)
        list(GET meow2AgilityStatus 0 meow2AgilityResult)
        if(NOT meow2AgilityResult EQUAL 0)
            list(GET meow2AgilityStatus 1 meow2AgilityError)
            message(FATAL_ERROR "Agility SDK: download failed: ${meow2AgilityError}\n"
                                "Without internet, pass -DMEOW2_AGILITY_DIR=<unpacked package> or -DMEOW2_RHI_D3D12=OFF.")
        endif()
        file(REMOVE_RECURSE "${meow2AgilityDir}")
        file(ARCHIVE_EXTRACT INPUT "${meow2AgilityFile}" DESTINATION "${meow2AgilityDir}")
        file(REMOVE "${meow2AgilityFile}")
        # Written last: an interrupted unpack is redone by the next configure.
        file(WRITE "${meow2AgilityDir}/.complete" "${meow2AgilityVersion}\n")
    endif()
endif()

set(meow2AgilityBin "${meow2AgilityDir}/build/native/bin/x64")
if(NOT EXISTS "${meow2AgilityBin}/D3D12Core.dll")
    message(FATAL_ERROR "Agility SDK: no D3D12Core.dll in ${meow2AgilityBin}")
endif()
message(STATUS "Agility SDK: ${meow2AgilityVersion} in ${meow2AgilityDir}")

# meow2_use_agility(<target>)
#   Makes an executable load the Agility runtime from D3D12/ next to it: compiles the two exports Windows reads
#   (Meow2Engine/src/Graphics/Rhi/D3D12/AgilityExports.cpp) into it, and copies the runtime and the debug layer there
#   after each build. Exports only count in the exe itself, not in a static library, as with meow2_track_global_new().
#   Call it before meow2_source_groups(<target>).
function(meow2_use_agility target)
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/Meow2Engine/src/Graphics/Rhi/D3D12/AgilityExports.cpp")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/Meow2Engine/src/Graphics/Rhi/D3D12/AgilityExports.cpp"
        TARGET_DIRECTORY ${target}
        PROPERTIES COMPILE_DEFINITIONS "MEOW2_AGILITY_SDK_VERSION=${MEOW2_AGILITY_SDK_VERSION}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${meow2AgilityBin}/D3D12Core.dll" "${meow2AgilityBin}/d3d12SDKLayers.dll"
                "$<TARGET_FILE_DIR:${target}>/D3D12"
        VERBATIM)
endfunction()

# The release zip: the runtime only (the debug layer is for development), with its licence next to it.
install(FILES "${meow2AgilityBin}/D3D12Core.dll" DESTINATION D3D12)
install(FILES "${meow2AgilityDir}/LICENSE.txt" DESTINATION D3D12)
