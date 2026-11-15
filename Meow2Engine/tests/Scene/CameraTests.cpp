/// @file
/// @brief Tests for the Camera component (Camera.h): finding the main camera and where it looks.

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <gtest/gtest.h>

#include <memory_resource>

namespace Meow2::Scene
{
namespace
{
/// @brief A scene with a hierarchy and the Camera component registered.
class CameraTest : public ::testing::Test
{
protected:
    CameraTest()
    {
        m_scene.registerComponent<Camera>();
    }

    /// @brief A camera entity at `position`, turned by `yaw` radians about +Y.
    entt::entity spawnCamera(const Math::Vec3& position, float yaw, bool main)
    {
        const entt::entity entity = m_scene.createTransient();
        m_hierarchy.add(
            entity,
            Math::Transform{position, Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, yaw), {2.0F, 2.0F, 2.0F}});
        m_scene.registry().emplace<Camera>(entity, Camera{.main = main});
        return entity;
    }

    Log::LogSystem m_logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger m_log{m_logSystem.createLogger("Engine")};
    Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    UuidGenerator m_ids;
    Scene m_scene{m_ids, *std::pmr::new_delete_resource(), m_jobs.threadCount()};
    TransformHierarchy m_hierarchy{m_scene, m_jobs};
};
} // namespace

TEST_F(CameraTest, FindsTheMainCamera)
{
    EXPECT_FALSE(findMainCamera(m_scene)) << "no camera at all";

    const entt::entity first = spawnCamera({0.0F, 0.0F, 0.0F}, 0.0F, false);
    EXPECT_EQ(findMainCamera(m_scene), first) << "none is main: the first one";

    const entt::entity main = spawnCamera({1.0F, 2.0F, 3.0F}, 0.0F, true);
    static_cast<void>(spawnCamera({5.0F, 0.0F, 0.0F}, 0.0F, false));
    EXPECT_EQ(findMainCamera(m_scene), main);
}

TEST_F(CameraTest, LooksAlongItsEntitysZ)
{
    const entt::entity camera = spawnCamera({1.0F, 2.0F, 3.0F}, Math::c_pi / 2.0F, true);
    m_hierarchy.update();
    const std::optional<Math::Transform> transform = cameraTransform(m_scene, camera);
    ASSERT_TRUE(transform);
    EXPECT_NEAR(transform->position.x, 1.0F, 1e-5F);
    EXPECT_NEAR(transform->position.z, 3.0F, 1e-5F);
    const Math::Vec3 forward = Math::rotate(transform->rotation, Math::Vec3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(forward.x, 1.0F, 1e-5F) << "a quarter turn about +Y looks towards +X";
    EXPECT_FLOAT_EQ(transform->scale.x, 1.0F) << "scale is dropped";

    EXPECT_FALSE(cameraTransform(m_scene, m_scene.createTransient())) << "no world transform";
}
} // namespace Meow2::Scene
