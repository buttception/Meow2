# Meow2Editor — Qt editor (techstack.md §2, §20). Qt >= 6.8 (Widgets), tested with 6.12; see docs/wiki/Editor.md.
#
# Without Qt the editor is skipped with a warning, so the engine, Player and Renderer still build. In CI (CI=true)
# a missing Qt is an error instead, so CI never silently stops building the editor.
if(DEFINED ENV{CI})
    set(meow2QtFailMode FATAL_ERROR)
else()
    set(meow2QtFailMode WARNING)
endif()

find_package(Qt6 6.8 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(${meow2QtFailMode}
        "Qt >= 6.8 (Widgets) not found: Meow2Editor is skipped.\n"
        "Point CMake at Qt's MSVC kit, e.g. -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 or the CMAKE_PREFIX_PATH "
        "environment variable (docs/wiki/Building.md#qt), or pass -DMEOW2_BUILD_EDITOR=OFF to silence this.")
    return()
endif()
message(STATUS "Qt ${Qt6_VERSION}: ${Qt6_DIR}")

# meow2_deploy_qt(<target>)
#   Windows: after each build, windeployqt copies the Qt DLLs and plugins <target> needs next to it (bin/), so it
#   runs from the build tree and from Visual Studio without PATH changes. qoffscreen is added for the tests and smoke
#   runs (platform.videoDriver = "offscreen"). Linux finds Qt through the RPATH CMake sets; nothing to do.
function(meow2_deploy_qt target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND Qt6::windeployqt --verbose 0 --no-translations --no-system-d3d-compiler --no-opengl-sw
                    --no-compiler-runtime --include-plugins qoffscreen "$<TARGET_FILE:${target}>"
            COMMENT "Deploying Qt next to ${target}"
            VERBATIM)
    endif()
endfunction()

# Everything but main(), so the tests can use the same code.
add_library(Meow2EditorCore STATIC
    src/EditorMainWindow.cpp
    src/EditorMainWindow.h
    src/ViewportWidget.cpp
    src/ViewportWidget.h
    src/WindowPlacement.cpp
    src/WindowPlacement.h)
target_include_directories(Meow2EditorCore PUBLIC src)
target_link_libraries(Meow2EditorCore PUBLIC Meow2::Engine Qt6::Widgets)
meow2_set_warnings(Meow2EditorCore)
set_target_properties(Meow2EditorCore PROPERTIES FOLDER "Apps")
meow2_source_groups(Meow2EditorCore)

add_executable(Meow2Editor src/main.cpp)
target_link_libraries(Meow2Editor PRIVATE Meow2EditorCore)
meow2_set_warnings(Meow2Editor)
set_target_properties(Meow2Editor PROPERTIES FOLDER "Apps")
# No meow2_track_global_new(): on Windows each DLL binds operator new/delete itself, and Qt's DLLs delete objects
# our exe allocated (a parent widget deletes its children), so a replaced operator new would mix allocators and
# corrupt the heap. The editor uses the normal heap; library allocator hooks are still tracked (techstack.md §6).
meow2_deploy_qt(Meow2Editor)
meow2_gui_subsystem_in_release(Meow2Editor) # no console window in Release

# Release zip (cmake/Meow2Packaging.cmake): the editor, then Qt's deploy script, which runs windeployqt on the
# installed exe and copies Qt's DLLs, plugins and a qt.conf next to it. qoffscreen is included so the packaged editor
# can be smoke-tested without a desktop (cmake/TestPackage.cmake). This lives here, not in the packaging module,
# because Qt's CMake functions only exist where find_package(Qt6) ran.
meow2_install_app(Meow2Editor)
qt_generate_deploy_app_script(
    TARGET Meow2Editor
    OUTPUT_SCRIPT meow2EditorDeployScript
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_TOOL_OPTIONS --include-plugins qoffscreen --no-system-d3d-compiler --no-opengl-sw)
install(CODE "set(QT_DEPLOY_BIN_DIR \"${MEOW2_INSTALL_DIR}\")") # the script defaults to bin/
install(SCRIPT "${meow2EditorDeployScript}")
meow2_source_groups(Meow2Editor)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder and keep the
# settings in its (git-ignored) config/ folder.
set_target_properties(Meow2Editor PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
    "--log-dir \"${PROJECT_SOURCE_DIR}/logs\" --config \"${PROJECT_SOURCE_DIR}/config/Meow2Editor.toml\"")

# Smoke runs use Qt's offscreen platform (no desktop needed, e.g. Linux CI), run 3 frames and exit. They use their own
# config file in the build tree, so they never touch the settings next to the exe.
set(meow2SmokeArgs
    --config "${CMAKE_CURRENT_BINARY_DIR}/smoke/$<CONFIG>/Meow2Editor.toml"
    --set platform.videoDriver=offscreen --set app.quitAfterFrames=3)
meow2_add_smoke_test(Smoke.Editor.Runs Meow2Editor "Meow2Editor.*${PROJECT_VERSION}" ARGS ${meow2SmokeArgs})
meow2_add_smoke_test(Smoke.Editor.OpensWindow Meow2Editor "Ran 3 frames" ARGS ${meow2SmokeArgs})

if(MEOW2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
