# Meow2Editor — Qt editor (techstack.md §2). Placeholder until M10 adds Qt 6.8.
add_executable(Meow2Editor src/main.cpp)
target_link_libraries(Meow2Editor PRIVATE Meow2::Engine)

meow2_add_smoke_test(Smoke.Editor.Runs Meow2Editor "Meow2Editor.*${PROJECT_VERSION}")
