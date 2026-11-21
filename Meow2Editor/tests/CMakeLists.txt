# Meow2Editor tests: the editor's Qt code, on Qt's offscreen platform (no desktop needed).
meow2_add_test_suite(
    TARGET    Meow2EditorTests
    MODULE    Editor
    SOURCES
        ClickSelectionTests.cpp
        ConsoleDockTests.cpp
        EditingDocksTests.cpp
        EditorCommandRegistryTests.cpp
        EditorCameraTests.cpp
        EditorCommandsTests.cpp
        EditorTestRig.h
        EditorThemeTests.cpp
        GizmoTests.cpp
        MainWindowTests.cpp
        QtInputSourceTests.cpp
        QtTestApplication.h
        SceneDocumentTests.cpp
        SceneEditorTests.cpp
        SettingsDialogTests.cpp
        UndoStackTests.cpp
        ViewportCommandsTests.cpp
        ViewportWidgetTests.cpp
        WindowPlacementTests.cpp
    LIBRARIES Meow2EditorCore)
# The theme tests load the bundled font from the source tree.
target_compile_definitions(Meow2EditorTests PRIVATE MEOW2_FONTS_DIR="${PROJECT_SOURCE_DIR}/assets/fonts")
# Deploy after the editor: both copy the same Qt DLLs into bin/, and two copies at once would collide.
add_dependencies(Meow2EditorTests Meow2Editor)
meow2_deploy_qt(Meow2EditorTests)

# The editor's renderer on a real GPU (offscreen viewports, no window): off by default like the engine's GPU tests.
if(MEOW2_GPU_TESTS AND MEOW2_SHADER_TOOLS)
    meow2_add_test_suite(
        TARGET    Meow2EditorGpuTests
        MODULE    EditorGpu
        LABELS    gpu
        SOURCES   EditorRendererTests.cpp
        LIBRARIES Meow2EditorCore)
    target_compile_definitions(Meow2EditorGpuTests PRIVATE MEOW2_COOKED_DIR="${MEOW2_COOKED_DIR}")
    # It uses no Qt itself (windeployqt refuses such an exe); Qt's DLLs, if any part needs them, are already in bin/
    # from the editor's deploy.
    add_dependencies(Meow2EditorGpuTests Meow2Editor Meow2Shaders)
endif()
