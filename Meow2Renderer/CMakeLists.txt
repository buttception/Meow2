# Meow2Renderer — Rendering and shader test bed / showcase (techstack.md §2).
add_executable(Meow2Renderer src/main.cpp)
target_link_libraries(Meow2Renderer PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Renderer)
set_target_properties(Meow2Renderer PROPERTIES FOLDER "Apps")
meow2_track_global_new(Meow2Renderer) # every new/delete through the memory tracker (Debug/RWDI)
meow2_gui_subsystem_in_release(Meow2Renderer) # no console window in Release
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
# Debug/RWDI: every allocation is tracked, and the app must end with nothing leaked (Release has no tracking).
meow2_add_smoke_test(Smoke.Renderer.NoLeaks Meow2Renderer "$<IF:$<CONFIG:Release,MinSizeRel>,Meow2Renderer,No memory leaks>"
    ARGS ${meow2SmokeArgs})
