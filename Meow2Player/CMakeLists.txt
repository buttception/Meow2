# Meow2Player — Standalone game runtime bundled by the editor on export (techstack.md §15).
add_executable(Meow2Player src/main.cpp)
target_link_libraries(Meow2Player PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2Player)

meow2_add_smoke_test(Smoke.Player.Runs Meow2Player "Meow2Player.*${PROJECT_VERSION}")
