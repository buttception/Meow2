# Meow2Player — Standalone game runtime bundled by the editor on export (techstack.md §15).
add_executable(Meow2Player src/main.cpp)
target_link_libraries(Meow2Player PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Player)
set_target_properties(Meow2Player PROPERTIES FOLDER "Apps")
meow2_track_global_new(Meow2Player) # every new/delete through the memory tracker (Debug/RWDI)
meow2_gui_subsystem_in_release(Meow2Player) # no console window in Release
meow2_source_groups(Meow2Player)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder and keep the
# settings in its (git-ignored) config/ folder.
set_target_properties(Meow2Player PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
    "--log-dir \"${PROJECT_SOURCE_DIR}/logs\" --config \"${PROJECT_SOURCE_DIR}/config/Meow2Player.toml\"")

# Smoke runs open the window with SDL's offscreen driver (no desktop needed, e.g. Linux CI), run 3 frames and exit.
# They use their own config file in the build tree, so they never touch the settings next to the exe.
set(meow2SmokeArgs
    --config "${CMAKE_CURRENT_BINARY_DIR}/smoke/$<CONFIG>/Meow2Player.toml"
    --set platform.videoDriver=offscreen --set app.quitAfterFrames=3)
meow2_add_smoke_test(Smoke.Player.Runs Meow2Player "Meow2Player.*${PROJECT_VERSION}" ARGS ${meow2SmokeArgs})
meow2_add_smoke_test(Smoke.Player.OpensWindow Meow2Player "Ran 3 frames" ARGS ${meow2SmokeArgs})
# Debug/RWDI: every allocation is tracked, and the app must end with nothing leaked (Release has no tracking).
meow2_add_smoke_test(Smoke.Player.NoLeaks Meow2Player "$<IF:$<CONFIG:Release,MinSizeRel>,Meow2Player,No memory leaks>"
    ARGS ${meow2SmokeArgs})
