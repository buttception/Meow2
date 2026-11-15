/// @file
/// @brief Tests for Meow2Renderer's scene-file helpers: where a model's scene goes, telling scenes from models, the
///        folder mounts, and the sun saved as an Environment.

#include "RendererUi.h"
#include "SceneView.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Meow2::RendererApp
{
TEST(SceneFiles, SavedNextToTheModel)
{
    const std::filesystem::path model = std::filesystem::path{"samples"} / "Duck" / "glTF" / "Duck.gltf";
    EXPECT_EQ(scenePathFor(model), std::filesystem::path{"samples"} / "Duck" / "glTF" / "Duck.m2scene");
    EXPECT_EQ(scenePathFor("Lantern.glb").filename(), std::filesystem::path{"Lantern.m2scene"});
}

TEST(SceneFiles, OpenedByExtension)
{
    EXPECT_TRUE(isSceneFile("level/Main.m2scene"));
    EXPECT_FALSE(isSceneFile("level/Main.gltf"));
    EXPECT_FALSE(isSceneFile("level/Main.glb"));
    EXPECT_FALSE(isSceneFile("m2scene"));
}

TEST(SceneFiles, OneMountPerFolder)
{
    const std::string duck = folderMountName("samples/Duck/glTF");
    EXPECT_EQ(folderMountName("samples/Duck/glTF"), duck) << "the same folder, the same mount, every time";
    EXPECT_EQ(folderMountName("samples/Duck/./glTF"), duck) << "spelled differently, still the same folder";
    EXPECT_NE(folderMountName("samples/Box/glTF"), duck);
    EXPECT_EQ(duck.size(), 17U);
    EXPECT_EQ(duck.front(), 'f');
}

TEST(SceneFiles, SunRoundTripsThroughTheEnvironment)
{
    SunControls sun;
    sun.elevationDegrees = 12.5F;
    sun.azimuthDegrees = 200.0F;
    sun.intensity = 6.0F;
    sun.exposure = 0.75F;
    const Scene::Environment environment = environmentFrom(sun);
    EXPECT_FLOAT_EQ(environment.sunElevation, 12.5F);
    EXPECT_FLOAT_EQ(environment.exposure, 0.75F);

    SunControls restored;
    applyEnvironment(environment, restored);
    EXPECT_FLOAT_EQ(restored.elevationDegrees, sun.elevationDegrees);
    EXPECT_FLOAT_EQ(restored.azimuthDegrees, sun.azimuthDegrees);
    EXPECT_FLOAT_EQ(restored.intensity, sun.intensity);
    EXPECT_FLOAT_EQ(restored.exposure, sun.exposure);
    // The direction the shading uses agrees with the sliders' own.
    const Math::Vec3 a = sunDirection(sun);
    const Math::Vec3 b = Scene::sunDirection(environment);
    EXPECT_NEAR(a.x, b.x, 1e-5F);
    EXPECT_NEAR(a.y, b.y, 1e-5F);
    EXPECT_NEAR(a.z, b.z, 1e-5F);
}
} // namespace Meow2::RendererApp
