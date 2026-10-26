# Meow2Renderer — Rendering and shader test bed / showcase (techstack.md §2).
add_executable(Meow2Renderer src/main.cpp)
target_link_libraries(Meow2Renderer PRIVATE Meow2::Engine)

meow2_add_smoke_test(Smoke.Renderer.Runs Meow2Renderer "Meow2Renderer.*${PROJECT_VERSION}")
