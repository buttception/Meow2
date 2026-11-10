# Meow2Renderer tests: the ImGui shell's input mapping and the scene picker's logic (no GPU needed).
meow2_add_test_suite(
    TARGET    Meow2RendererTests
    MODULE    RendererApp
    SOURCES
        CameraControlsTests.cpp
        FrameInspectorTests.cpp
        RendererUiTests.cpp
        ShellWindowsTests.cpp
    LIBRARIES Meow2RendererCore)
