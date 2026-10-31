# Meow2Engine — static library with all runtime code (techstack.md §3).
add_library(Meow2Engine STATIC)
add_library(Meow2::Engine ALIAS Meow2Engine)

# Headers generated at configure time live in the build tree, never in git.
set(meow2GeneratedDir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(src/Core/Version.h.in "${meow2GeneratedDir}/Meow2/Core/Version.h" @ONLY)

target_sources(Meow2Engine
    PRIVATE
        src/Core/Assert.cpp
        src/Core/Log/AssertLogging.cpp
        src/Core/Log/Logger.cpp
        src/Core/Log/LoggerImpl.h
        src/Core/Log/LogSystem.cpp
        src/Core/Memory/BlockArena.cpp
        src/Core/Memory/MallocResource.cpp
        src/Core/Memory/MemTag.cpp
        src/Core/Memory/TrackingAllocator.cpp
        src/Core/Version.cpp
    PUBLIC
        # A header FILE_SET makes the base dirs PUBLIC include directories and
        # lists the headers in IDEs. Public headers go in include/Meow2/<Subsystem>/[<Module>/].
        FILE_SET HEADERS
        BASE_DIRS
            include
            "${meow2GeneratedDir}"
        FILES
            include/Meow2/Core/Assert.h
            include/Meow2/Core/Crash/CrashHandler.h
            include/Meow2/Core/Debugger.h
            include/Meow2/Core/Log/AssertLogging.h
            include/Meow2/Core/Log/Logger.h
            include/Meow2/Core/Log/LogSystem.h
            include/Meow2/Core/Memory/BlockArena.h
            include/Meow2/Core/Memory/DebugFill.h
            include/Meow2/Core/Memory/MallocResource.h
            include/Meow2/Core/Memory/MemTag.h
            include/Meow2/Core/Memory/TrackingAllocator.h
            include/Meow2/Core/Platform.h
            include/Meow2/Core/Process.h
            include/Meow2/Core/Result.h
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

# Third-party libraries are PRIVATE: none of their headers appear in Meow2's public headers.
# Exception: tl::expected is PUBLIC, because Result<T> (Meow2/Core/Result.h) is public API.
target_link_libraries(Meow2Engine
    PUBLIC
        tl::expected
    PRIVATE
        spdlog::spdlog)

# Build configuration for Meow2/Core/Platform.h. PUBLIC: everything linking the
# engine sees the same values. $<CONFIG:...> is evaluated per configuration, so
# this also works with multi-config generators (Visual Studio).
target_compile_definitions(Meow2Engine PUBLIC
    MEOW2_CONFIG_DEBUG=$<CONFIG:Debug>
    MEOW2_CONFIG_RELWITHDEBINFO=$<CONFIG:RelWithDebInfo>
    MEOW2_CONFIG_RELEASE=$<CONFIG:Release,MinSizeRel>)
target_compile_features(Meow2Engine PUBLIC cxx_std_20)
meow2_set_warnings(Meow2Engine)
set_target_properties(Meow2Engine PROPERTIES FOLDER "Engine")
meow2_source_groups(Meow2Engine) # after all target_sources()

# API reference over the public headers (incl. the generated ones): `docs` target and
# the Docs.PublicApi.IsDocumented test (cmake/Meow2Docs.cmake).
meow2_add_api_docs(
    INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/include" "${meow2GeneratedDir}"
    STRIP "${CMAKE_CURRENT_SOURCE_DIR}/include" "${meow2GeneratedDir}")

if(MEOW2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
