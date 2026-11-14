# Meow2Renderer tests: the ImGui shell's input mapping and the scene picker's logic (no GPU needed).
meow2_add_test_suite(
    TARGET    Meow2RendererTests
    MODULE    RendererApp
    SOURCES
        CameraControlsTests.cpp
        DebugOverlaysTests.cpp
        FrameInspectorTests.cpp
        RendererUiTests.cpp
        ShellWindowsTests.cpp
    LIBRARIES Meow2RendererCore)

# Golden images (techstack.md §13): every suggested sample on each backend against an approved PNG. They need a GPU, a
# desktop and the samples, so they're built only with MEOW2_GPU_TESTS and skip samples that aren't downloaded.
if(MEOW2_GPU_TESTS AND MEOW2_SHADER_TOOLS)
    meow2_add_test_suite(
        TARGET    Meow2RendererGoldenTests
        MODULE    Visual
        LABELS    gpu visual
        SOURCES
            DebugOverlayGpuTests.cpp
            GoldenImageTests.cpp
        LIBRARIES Meow2RendererCore)
    add_dependencies(Meow2RendererGoldenTests Meow2Shaders)
    target_compile_definitions(Meow2RendererGoldenTests PRIVATE
        MEOW2_COOKED_DIR="${MEOW2_COOKED_DIR}"
        MEOW2_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/visual/goldens"
        MEOW2_VISUAL_OUTPUT_DIR="${PROJECT_BINARY_DIR}/visual")
    if(MEOW2_RHI_D3D12)
        meow2_use_agility(Meow2RendererGoldenTests) # the D3D12 runtime in D3D12/ next to the exe
    endif()
endif()
