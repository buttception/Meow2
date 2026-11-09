# Meow2Renderer — Rendering and shader test bed / showcase (techstack.md §2).

# Everything but main(), so the tests can use the same code: the ImGui shell, its windows and the open scene.
add_library(Meow2RendererCore STATIC
    src/CameraControls.cpp
    src/CameraControls.h
    src/ConsoleWindow.cpp
    src/ConsoleWindow.h
    src/ImGuiLayer.cpp
    src/ImGuiLayer.h
    src/RenderDebug.cpp
    src/RenderDebug.h
    src/RendererUi.cpp
    src/RendererUi.h
    src/SceneView.cpp
    src/SceneView.h
    src/SettingsWindow.cpp
    src/SettingsWindow.h
    src/UiTheme.cpp
    src/UiTheme.h)
target_include_directories(Meow2RendererCore PUBLIC src)
target_link_libraries(Meow2RendererCore PUBLIC Meow2::Engine meow2_imgui)
# The Scenes window fetches samples by running cmake with the fetch script; development paths, like the samples
# folder (renderer.sampleAssets overrides that one).
target_compile_definitions(Meow2RendererCore PUBLIC
    MEOW2_SAMPLE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets/gltf-sample-assets"
    MEOW2_CMAKE_COMMAND="${CMAKE_COMMAND}"
    MEOW2_FETCH_SCRIPT="${PROJECT_SOURCE_DIR}/cmake/FetchGltfSample.cmake")
meow2_set_warnings(Meow2RendererCore)
set_target_properties(Meow2RendererCore PROPERTIES FOLDER "Apps")
meow2_source_groups(Meow2RendererCore)

add_executable(Meow2Renderer src/main.cpp)
target_link_libraries(Meow2Renderer PRIVATE Meow2RendererCore)
meow2_set_warnings(Meow2Renderer)
if(TARGET Meow2Shaders)
    add_dependencies(Meow2Renderer Meow2Shaders) # building or running Renderer alone still cooks its shaders
endif()
set_target_properties(Meow2Renderer PROPERTIES FOLDER "Apps")
meow2_track_global_new(Meow2Renderer) # every new/delete through the memory tracker (Debug/RWDI)
if(MEOW2_RHI_D3D12)
    meow2_use_agility(Meow2Renderer) # the D3D12 runtime in D3D12/ next to the exe
endif()
meow2_gui_subsystem_in_release(Meow2Renderer) # no console window in Release
meow2_install_app(Meow2Renderer)                # into the release zip (cmake/Meow2Packaging.cmake)
meow2_app_icon(Meow2Renderer Meow2Renderer)    # the cat with an R badge
meow2_app_fonts(Meow2Renderer)                  # fonts/ next to the exe: the UI's Barlow
meow2_source_groups(Meow2Renderer)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder and keep the
# settings in its (git-ignored) config/ folder.
set_target_properties(Meow2Renderer PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
    "--log-dir \"${PROJECT_SOURCE_DIR}/logs\" --config \"${PROJECT_SOURCE_DIR}/config/Meow2Renderer.toml\"")

# Smoke runs open the window with SDL's offscreen driver (no desktop needed, e.g. Linux CI), run 3 frames and exit.
# They use their own config file in the build tree, so they never touch the settings next to the exe.
set(meow2SmokeArgs
    --config "${CMAKE_CURRENT_BINARY_DIR}/smoke/$<CONFIG>/Meow2Renderer.toml"
    --set platform.videoDriver=offscreen --set app.quitAfterFrames=3)
meow2_add_smoke_test(Smoke.Renderer.Runs Meow2Renderer "Meow2Renderer.*${PROJECT_VERSION}" ARGS ${meow2SmokeArgs})
meow2_add_smoke_test(Smoke.Renderer.OpensWindow Meow2Renderer "Ran 3 frames" ARGS ${meow2SmokeArgs})
# Debug/RWDI: every allocation is tracked, and the app must end with nothing leaked. Release and sanitizer builds
# have no tracking (in sanitizer builds LeakSanitizer checks leaks instead), so there it only checks that the app runs.
meow2_add_smoke_test(Smoke.Renderer.NoLeaks Meow2Renderer "$<IF:$<OR:$<CONFIG:Release,MinSizeRel>,$<BOOL:${MEOW2_SANITIZER}>>,Meow2Renderer,No memory leaks>"
    ARGS ${meow2SmokeArgs})

if(MEOW2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
