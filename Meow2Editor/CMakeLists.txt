# Meow2Editor — Qt editor (techstack.md §2). Placeholder until M10 adds Qt 6.8.
add_executable(Meow2Editor src/main.cpp)
target_link_libraries(Meow2Editor PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Editor)
set_target_properties(Meow2Editor PROPERTIES FOLDER "Apps")
meow2_source_groups(Meow2Editor)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder.
set_target_properties(Meow2Editor PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "--log-dir \"${PROJECT_SOURCE_DIR}/logs\"")

meow2_add_smoke_test(Smoke.Editor.Runs Meow2Editor "Meow2Editor.*${PROJECT_VERSION}")
