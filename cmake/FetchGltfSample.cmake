# Fetches Khronos glTF sample models on demand (techstack.md §13):
#
#   cmake -P cmake/FetchGltfSample.cmake <Model> [<Model> ...]
#
# The first run makes a blobless, sparse clone of glTF-Sample-Assets at the pinned commit into
# assets/gltf-sample-assets/ (git-ignored). Each model adds only its own folder, Models/<Model>/, so nothing else is
# downloaded. Running it again for a model that's there does nothing. Needs git on PATH and a network.

cmake_minimum_required(VERSION 3.25)

# The sample repository and the commit every machine uses, so golden images stay comparable.
set(meow2SampleRepository "https://github.com/KhronosGroup/glTF-Sample-Assets.git")
set(meow2SampleCommit "8f492390ddc7a2ae221a1369ef560589cf8c70fb")

get_filename_component(meow2RepoDir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(meow2SampleDir "${meow2RepoDir}/assets/gltf-sample-assets")

# Arguments after the script name: cmake -P <script> <arg> → CMAKE_ARGV3 onwards.
set(models "")
math(EXPR lastArgument "${CMAKE_ARGC} - 1")
foreach(i RANGE 3 ${lastArgument})
    if(DEFINED CMAKE_ARGV${i})
        list(APPEND models "${CMAKE_ARGV${i}}")
    endif()
endforeach()
if(NOT models)
    message(FATAL_ERROR "Name at least one model: cmake -P cmake/FetchGltfSample.cmake DamagedHelmet")
endif()
foreach(model IN LISTS models)
    if(NOT model MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR "'${model}' isn't a model folder name (letters, digits, - and _ only)")
    endif()
endforeach()

find_package(Git REQUIRED QUIET)

function(meow2_git)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN}
                    WORKING_DIRECTORY "${meow2SampleDir}"
                    RESULT_VARIABLE result
                    ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed:\n${error}")
    endif()
endfunction()

if(NOT EXISTS "${meow2SampleDir}/.git")
    message(STATUS "Cloning the glTF sample index (no models yet) into ${meow2SampleDir}")
    file(MAKE_DIRECTORY "${meow2SampleDir}")
    meow2_git(init --quiet)
    meow2_git(remote add origin "${meow2SampleRepository}")
    meow2_git(config extensions.partialClone origin)
    meow2_git(config remote.origin.promisor true)
    meow2_git(config remote.origin.partialclonefilter blob:none)
    meow2_git(sparse-checkout set --cone Models/__none__)
    meow2_git(fetch --quiet --depth 1 --filter=blob:none origin "${meow2SampleCommit}")
    meow2_git(checkout --quiet --detach FETCH_HEAD)
endif()

foreach(model IN LISTS models)
    if(EXISTS "${meow2SampleDir}/Models/${model}")
        message(STATUS "${model}: already here")
        continue()
    endif()
    message(STATUS "${model}: downloading")
    meow2_git(sparse-checkout add "Models/${model}")
    if(NOT EXISTS "${meow2SampleDir}/Models/${model}")
        message(FATAL_ERROR "${model} isn't in glTF-Sample-Assets at ${meow2SampleCommit}. "
                            "See https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models")
    endif()
endforeach()
message(STATUS "glTF samples are in ${meow2SampleDir}/Models")
