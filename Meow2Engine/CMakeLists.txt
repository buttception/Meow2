# Meow2Engine — static library with all runtime code (techstack.md §3).
add_library(Meow2Engine STATIC)
add_library(Meow2::Engine ALIAS Meow2Engine)

# Headers generated at configure time live in the build tree, never in git.
set(meow2GeneratedDir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(src/Core/Version.h.in "${meow2GeneratedDir}/Meow2/Core/Version.h" @ONLY)

target_sources(Meow2Engine
    PRIVATE
        src/Core/Assert.cpp
        src/Core/CommandLine.cpp
        src/Core/CommandLine.h
        src/Core/Config/Config.cpp
        src/Core/Events/EventBus.cpp
        src/Core/Jobs/Enkits/JobSystemEnkits.cpp
        src/Core/Log/AssertLogging.cpp
        src/Core/Log/Logger.cpp
        src/Core/Log/LoggerImpl.h
        src/Core/Log/LogSystem.cpp
        src/Core/Memory/BlockArena.cpp
        src/Core/Memory/GlobalTracking.cpp
        src/Core/Memory/LeakCheck.cpp
        src/Core/Memory/MallocResource.cpp
        src/Core/Memory/MemTag.cpp
        src/Core/Memory/TrackingAllocator.cpp
        src/Core/Profiling/Tracy/ProfileTracy.cpp
        src/Core/Time/FrameClock.cpp
        src/Core/Version.cpp
        src/Platform/Sdl/PlatformImplSdl.h
        src/Platform/Sdl/PlatformSdl.cpp
        src/Platform/Window/Sdl/WindowImplSdl.h
        src/Platform/Window/Sdl/WindowSdl.cpp
        src/Platform/Window/WindowConfig.cpp
    PUBLIC
        # A header FILE_SET makes the base dirs PUBLIC include directories and
        # lists the headers in IDEs. Public headers go in include/Meow2/<Subsystem>/[<Module>/].
        FILE_SET HEADERS
        BASE_DIRS
            include
            "${meow2GeneratedDir}"
        FILES
            include/Meow2/Core/Assert.h
            include/Meow2/Core/Config/Config.h
            include/Meow2/Core/Crash/CrashHandler.h
            include/Meow2/Core/Debugger.h
            include/Meow2/Core/Events/EventBus.h
            include/Meow2/Core/Jobs/JobSystem.h
            include/Meow2/Core/Log/AssertLogging.h
            include/Meow2/Core/Log/Logger.h
            include/Meow2/Core/Log/LogSystem.h
            include/Meow2/Core/Memory/BlockArena.h
            include/Meow2/Core/Memory/DebugFill.h
            include/Meow2/Core/Memory/GlobalTracking.h
            include/Meow2/Core/Memory/LeakCheck.h
            include/Meow2/Core/Memory/MallocResource.h
            include/Meow2/Core/Memory/MemTag.h
            include/Meow2/Core/Memory/TrackingAllocator.h
            include/Meow2/Core/Platform.h
            include/Meow2/Core/Process.h
            include/Meow2/Core/Profiling/Profile.h
            include/Meow2/Core/Result.h
            include/Meow2/Core/Time/FrameClock.h
            include/Meow2/Platform/Platform.h
            include/Meow2/Platform/Window/Window.h
            include/Meow2/Platform/Window/WindowConfig.h
            "${meow2GeneratedDir}/Meow2/Core/Version.h")

# OS-specific implementations (AGENTS.md: only in Win32/Linux/Android backend folders).
# Android uses the Linux sources: same kernel interfaces (/proc).
if(WIN32)
    target_sources(Meow2Engine PRIVATE
        src/Core/Crash/Win32/CrashHandlerWin32.cpp
        src/Core/Win32/DebuggerWin32.cpp
        src/Core/Win32/ProcessWin32.cpp)
    target_link_libraries(Meow2Engine PRIVATE dbghelp) # MiniDumpWriteDump
else()
    target_sources(Meow2Engine PRIVATE
        src/Core/Crash/Linux/CrashHandlerLinux.cpp
        src/Core/Linux/DebuggerLinux.cpp
        src/Core/Linux/ProcessLinux.cpp)
endif()

target_include_directories(Meow2Engine PRIVATE src)

# Build metadata for getVersionString() ("0.1.0+g1a2b3c4", techstack.md §21). The git commit is read on every build,
# not only at configure time, so the version never names an old commit; GitVersion.cmake rewrites the private header
# only when it changes, so a build of the same commit recompiles nothing.
set(meow2PrivateGeneratedDir "${CMAKE_CURRENT_BINARY_DIR}/generated/private")
add_custom_target(meow2_git_version
    COMMAND ${CMAKE_COMMAND}
        "-DMEOW2_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DMEOW2_VERSION=${PROJECT_VERSION}"
        "-DMEOW2_OUTPUT=${meow2PrivateGeneratedDir}/Core/BuildMetadata.h"
        -P "${PROJECT_SOURCE_DIR}/cmake/GitVersion.cmake"
    BYPRODUCTS "${meow2PrivateGeneratedDir}/Core/BuildMetadata.h"
    COMMENT "Reading the git commit for the version string"
    VERBATIM)
set_target_properties(meow2_git_version PROPERTIES FOLDER "Tools")
add_dependencies(Meow2Engine meow2_git_version)
target_include_directories(Meow2Engine PRIVATE "${meow2PrivateGeneratedDir}")

# Third-party libraries are PRIVATE: none of their headers appear in Meow2's public headers.
# Exception: tl::expected is PUBLIC, because Result<T> (Meow2/Core/Result.h) is public API.
target_link_libraries(Meow2Engine
    PUBLIC
        tl::expected
    PRIVATE
        SDL3::SDL3-static
        enkiTS
        Tracy::TracyClient
        spdlog::spdlog
        tomlplusplus::tomlplusplus)

# Build configuration for Meow2/Core/Platform.h. PUBLIC: everything linking the
# engine sees the same values. $<CONFIG:...> is evaluated per configuration, so
# this also works with multi-config generators (Visual Studio).
target_compile_definitions(Meow2Engine PUBLIC
    MEOW2_CONFIG_DEBUG=$<CONFIG:Debug>
    MEOW2_CONFIG_RELWITHDEBINFO=$<CONFIG:RelWithDebInfo>
    MEOW2_CONFIG_RELEASE=$<CONFIG:Release,MinSizeRel>
    MEOW2_PROFILING=$<IF:${meow2ProfilingEnabled},1,0>) # Tracy zones (Profile.h); set in cmake/Meow2External.cmake
target_compile_features(Meow2Engine PUBLIC cxx_std_20)
meow2_set_warnings(Meow2Engine)
set_target_properties(Meow2Engine PROPERTIES FOLDER "Engine")
meow2_source_groups(Meow2Engine) # after all target_sources()

# API reference over the public headers (incl. the generated ones): `docs` target and
# the Docs.PublicApi.IsDocumented test (cmake/Meow2Docs.cmake).
meow2_add_api_docs(
    INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/include" "${meow2GeneratedDir}"
    STRIP "${CMAKE_CURRENT_SOURCE_DIR}/include" "${meow2GeneratedDir}")

# meow2_track_global_new(<target>)
#   Compiles the replacement global operator new/delete (src/Core/Memory/GlobalNewDelete.cpp) into an
#   executable, so every C++ heap allocation goes through Meow2::Memory::globalTracker() in Debug and
#   RelWithDebInfo (the file compiles to nothing in Release). It's added to each executable instead of
#   the static library because the linker drops library objects nothing refers to by name, and nothing
#   refers to operator new that way. Call it before meow2_source_groups(<target>).
function(meow2_track_global_new target)
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/Meow2Engine/src/Core/Memory/GlobalNewDelete.cpp")
endfunction()

if(MEOW2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
