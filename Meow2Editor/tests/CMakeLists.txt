# Meow2Editor tests: the editor's Qt code, on Qt's offscreen platform (no desktop needed).
meow2_add_test_suite(
    TARGET    Meow2EditorTests
    MODULE    Editor
    SOURCES
        ConsoleDockTests.cpp
        EditorCommandRegistryTests.cpp
        EditorCommandsTests.cpp
        EditorTestRig.h
        EditorThemeTests.cpp
        MainWindowTests.cpp
        QtTestApplication.h
        SettingsDialogTests.cpp
        UndoStackTests.cpp
        ViewportWidgetTests.cpp
        WindowPlacementTests.cpp
    LIBRARIES Meow2EditorCore)
# The theme tests load the bundled font from the source tree.
target_compile_definitions(Meow2EditorTests PRIVATE MEOW2_FONTS_DIR="${PROJECT_SOURCE_DIR}/assets/fonts")
# Deploy after the editor: both copy the same Qt DLLs into bin/, and two copies at once would collide.
add_dependencies(Meow2EditorTests Meow2Editor)
meow2_deploy_qt(Meow2EditorTests)
