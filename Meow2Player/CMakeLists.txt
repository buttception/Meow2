# Meow2Player — Standalone game runtime bundled by the editor on export (techstack.md §15).
add_executable(Meow2Player src/main.cpp)
target_link_libraries(Meow2Player PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Player)
set_target_properties(Meow2Player PROPERTIES FOLDER "Apps")
meow2_source_groups(Meow2Player)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder.
set_target_properties(Meow2Player PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "--log-dir \"${PROJECT_SOURCE_DIR}/logs\"")

meow2_add_smoke_test(Smoke.Player.Runs Meow2Player "Meow2Player.*${PROJECT_VERSION}")
