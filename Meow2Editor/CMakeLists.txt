# Meow2Editor — Qt editor (techstack.md §2). Placeholder until M10 adds Qt 6.8.
add_executable(Meow2Editor src/main.cpp)
target_link_libraries(Meow2Editor PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Editor)
set_target_properties(Meow2Editor PROPERTIES FOLDER "Apps")
meow2_track_global_new(Meow2Editor) # every new/delete through the memory tracker (Debug/RWDI)
meow2_source_groups(Meow2Editor)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder and keep the
# settings in its (git-ignored) config/ folder.
set_target_properties(Meow2Editor PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
    "--log-dir \"${PROJECT_SOURCE_DIR}/logs\" --config \"${PROJECT_SOURCE_DIR}/config/Meow2Editor.toml\"")

meow2_add_smoke_test(Smoke.Editor.Runs Meow2Editor "Meow2Editor.*${PROJECT_VERSION}")
# Debug/RWDI: every allocation is tracked, and the app must end with nothing leaked (Release has no tracking).
meow2_add_smoke_test(Smoke.Editor.NoLeaks Meow2Editor "$<IF:$<CONFIG:Release,MinSizeRel>,Meow2Editor,No memory leaks>")
