# Meow2Renderer — Rendering and shader test bed / showcase (techstack.md §2).
add_executable(Meow2Renderer src/main.cpp)
target_link_libraries(Meow2Renderer PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Renderer)
set_target_properties(Meow2Renderer PROPERTIES FOLDER "Apps")
meow2_source_groups(Meow2Renderer)
# Started from the Visual Studio solution, log into the repository's (git-ignored) logs/ folder.
set_target_properties(Meow2Renderer PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "--log-dir \"${PROJECT_SOURCE_DIR}/logs\"")

meow2_add_smoke_test(Smoke.Renderer.Runs Meow2Renderer "Meow2Renderer.*${PROJECT_VERSION}")
