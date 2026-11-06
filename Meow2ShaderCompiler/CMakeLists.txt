# Meow2ShaderCompiler — cooks shader assets during the build (techstack.md §12). Not shipped.
add_executable(Meow2ShaderCompiler src/main.cpp)
target_link_libraries(Meow2ShaderCompiler PRIVATE Meow2::Engine)
meow2_set_warnings(Meow2ShaderCompiler)
set_target_properties(Meow2ShaderCompiler PROPERTIES FOLDER "Tools")
meow2_copy_dxc_runtime(Meow2ShaderCompiler)
meow2_source_groups(Meow2ShaderCompiler)
