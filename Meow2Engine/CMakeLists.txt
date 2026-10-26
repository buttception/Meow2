# Meow2Engine — static library with all runtime code (techstack.md §3).
add_library(Meow2Engine STATIC)
add_library(Meow2::Engine ALIAS Meow2Engine)

# Headers generated at configure time live in the build tree, never in git.
set(meow2GeneratedDir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(src/Core/Version.h.in "${meow2GeneratedDir}/Meow2/Core/Version.h" @ONLY)

target_sources(Meow2Engine
    PRIVATE
        src/Core/Version.cpp
    PUBLIC
        # A header FILE_SET makes the base dirs PUBLIC include directories and
        # lists the headers in IDEs. Public headers go in include/Meow2/<Module>/.
        FILE_SET HEADERS
        BASE_DIRS
            include
            "${meow2GeneratedDir}"
        FILES
            include/Meow2/Core/Platform.h
            "${meow2GeneratedDir}/Meow2/Core/Version.h")

target_include_directories(Meow2Engine PRIVATE src)

# Build configuration for Meow2/Core/Platform.h. PUBLIC: everything linking the
# engine sees the same values. $<CONFIG:...> is evaluated per configuration, so
# this also works with multi-config generators (Visual Studio).
target_compile_definitions(Meow2Engine PUBLIC
    MEOW2_CONFIG_DEBUG=$<CONFIG:Debug>
    MEOW2_CONFIG_RELWITHDEBINFO=$<CONFIG:RelWithDebInfo>
    MEOW2_CONFIG_RELEASE=$<CONFIG:Release,MinSizeRel>)
target_compile_features(Meow2Engine PUBLIC cxx_std_20)
meow2_set_warnings(Meow2Engine)

if(MEOW2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
