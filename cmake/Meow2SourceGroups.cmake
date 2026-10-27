# How Meow2 is organised inside Visual Studio (and other IDEs that read CMake's project model):
#
#   Filters (source groups): a file's filter mirrors its folder below include/Meow2/, src/ or
#   tests/, so Solution Explorer looks like the disk. Headers and sources of one module share
#   one filter (no separate "Header Files"/"Source Files"). At most three levels:
#   Subsystem\Module\Backend (techstack.md §2), e.g.
#       include/Meow2/Graphics/Rhi/Device.h          -> Graphics\Rhi
#       src/Graphics/Rhi/D3D12/DeviceD3D12.cpp       -> Graphics\Rhi\D3D12
#
#   Solution folders: targets are grouped as Engine, Apps, Tests, External and Tools.

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Subsystem\Module\Backend; anything deeper is shown in the third level.
set(MEOW2_SOURCE_GROUP_MAX_DEPTH 3)

# meow2_source_groups(<target>)
#   Assigns every source and header of <target> to the filter matching its folder.
#   Call it at the END of the CMakeLists.txt that creates <target>, after every
#   target_sources(): source groups belong to a directory and only cover files known
#   at the time of the call.
function(meow2_source_groups target)
    get_target_property(sources ${target} SOURCES)
    get_target_property(headers ${target} HEADER_SET)
    get_target_property(sourceDir ${target} SOURCE_DIR)
    get_target_property(binaryDir ${target} BINARY_DIR)

    # Roots, most specific first. A file's filter is its folder relative to the first root
    # that contains it; files directly in a root go to the top level of the project.
    set(roots
        "${sourceDir}/include/Meow2"
        "${binaryDir}/generated/include/Meow2"
        "${sourceDir}/src"
        "${sourceDir}/tests"
        "${sourceDir}")

    foreach(file IN LISTS sources headers)
        if(NOT file OR file MATCHES "-NOTFOUND$" OR file MATCHES "\\$<")
            continue() # no header set, or a generator expression (resolved only at build time)
        endif()
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${sourceDir}" NORMALIZE OUTPUT_VARIABLE absoluteFile)

        set(group "")
        foreach(root IN LISTS roots)
            cmake_path(IS_PREFIX root "${absoluteFile}" NORMALIZE isUnderRoot)
            if(isUnderRoot)
                file(RELATIVE_PATH relativeFile "${root}" "${absoluteFile}")
                cmake_path(GET relativeFile PARENT_PATH relativeDir)
                if(relativeDir)
                    string(REPLACE "/" ";" parts "${relativeDir}")
                    list(LENGTH parts depth)
                    if(depth GREATER MEOW2_SOURCE_GROUP_MAX_DEPTH)
                        list(SUBLIST parts 0 ${MEOW2_SOURCE_GROUP_MAX_DEPTH} parts)
                    endif()
                    list(JOIN parts "\\" group)
                endif()
                break()
            endif()
        endforeach()

        source_group("${group}" FILES "${absoluteFile}")
    endforeach()
endfunction()
