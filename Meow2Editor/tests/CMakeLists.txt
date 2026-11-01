# Meow2Editor tests: the editor's Qt code, on Qt's offscreen platform (no desktop needed).
meow2_add_test_suite(
    TARGET    Meow2EditorTests
    MODULE    Editor
    SOURCES
        QtTestApplication.h
        ViewportWidgetTests.cpp
        WindowPlacementTests.cpp
    LIBRARIES Meow2EditorCore)
# Deploy after the editor: both copy the same Qt DLLs into bin/, and two copies at once would collide.
add_dependencies(Meow2EditorTests Meow2Editor)
meow2_deploy_qt(Meow2EditorTests)
