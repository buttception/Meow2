/// @file
/// @brief Tests for Gizmo: hit tests, drawing size and what dragging each handle does to a transform.

#include "Editing/Gizmo.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Scalar.h>

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

namespace
{
namespace Math = Meow2::Math;
using Meow2::Editor::Gizmo;
using Meow2::Editor::GizmoHandle;
using Meow2::Editor::GizmoMode;
using Meow2::Editor::GizmoSnap;
using Meow2::Editor::GizmoSpace;
using Meow2::Editor::GizmoView;

constexpr float c_width = 800.0F;
constexpr float c_height = 600.0F;

/// @brief A camera at `eye` looking at the origin, in an 800 × 600 viewport.
GizmoView viewFrom(const Math::Vec3& eye)
{
    const float length = std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
    const Math::Transform camera{.position = eye,
                                 .rotation = Math::lookRotation({-eye.x / length, -eye.y / length, -eye.z / length})};
    return {.matrices = Math::cameraMatrices(camera, {}, c_width / c_height),
            .viewport = {.width = c_width, .height = c_height}};
}

/// @brief Where a world point appears in the view.
Math::Vec3 screenOf(const GizmoView& view, const Math::Vec3& world)
{
    const std::optional<Math::Vec3> screen = Math::worldToScreen(world, view.matrices, view.viewport);
    EXPECT_TRUE(screen);
    return screen.value_or(Math::Vec3{});
}

/// @brief The world-space tip of an axis handle: the end of its first line (translate and scale draw the axis first).
Math::Vec3 tipOf(const Gizmo& gizmo, const GizmoView& view, const Math::Transform& target, int axis)
{
    const std::size_t linesPerAxis = gizmo.mode() == GizmoMode::Translate ? 9U : 13U;
    return gizmo.lines(view, target, GizmoHandle::None).at(static_cast<std::size_t>(axis) * linesPerAxis).to;
}

Math::Vec3 lerp(const Math::Vec3& a, const Math::Vec3& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

const GizmoView c_view = viewFrom({6.0F, 5.0F, -8.0F});
const Math::Transform c_origin{};
} // namespace

TEST(GizmoTest, TranslatesAlongTheAxis)
{
    Gizmo gizmo;
    const Math::Vec3 tipX = tipOf(gizmo, c_view, c_origin, 0);
    const Math::Vec3 grab = lerp(c_origin.position, tipX, 0.6F);
    const Math::Vec3 grabOnScreen = screenOf(c_view, grab);
    ASSERT_EQ(gizmo.hitTest(c_view, c_origin, grabOnScreen.x, grabOnScreen.y), GizmoHandle::X);
    ASSERT_TRUE(gizmo.beginDrag(c_view, c_origin, GizmoHandle::X, grabOnScreen.x, grabOnScreen.y));
    EXPECT_EQ(gizmo.dragged(), GizmoHandle::X);

    // The cursor over a point 1.3 m further along X: the entity moves 1.3 m along X, nothing else.
    const Math::Vec3 to = screenOf(c_view, {grab.x + 1.3F, grab.y, grab.z});
    const std::optional<Math::Transform> moved = gizmo.drag(c_view, to.x, to.y, {});
    ASSERT_TRUE(moved);
    EXPECT_NEAR(moved->position.x, 1.3F, 1e-3F);
    EXPECT_NEAR(moved->position.y, 0.0F, 1e-5F);
    EXPECT_NEAR(moved->position.z, 0.0F, 1e-5F);

    // Off the axis on screen it still only moves along X.
    const Math::Vec3 off = screenOf(c_view, {grab.x + 1.0F, grab.y + 0.7F, grab.z});
    const std::optional<Math::Transform> along = gizmo.drag(c_view, off.x, off.y, {});
    ASSERT_TRUE(along);
    EXPECT_NEAR(along->position.y, 0.0F, 1e-5F);
    EXPECT_NEAR(along->position.z, 0.0F, 1e-5F);

    // Snapping: whole 0.5 m steps.
    const std::optional<Math::Transform> snapped = gizmo.drag(c_view, to.x, to.y, {.enabled = true, .translate = 0.5F});
    ASSERT_TRUE(snapped);
    EXPECT_NEAR(snapped->position.x, 1.5F, 1e-5F);
    gizmo.endDrag();
    EXPECT_FALSE(gizmo.drag(c_view, to.x, to.y, {})) << "not dragging";

    // Local axes: an entity turned 90° about Y moves along its own X, which is the world's -Z.
    gizmo.setSpace(GizmoSpace::Local);
    const Math::Transform turned{.position = {}, .rotation = Math::fromAxisAngle(Math::c_up, Math::toRadians(90.0F))};
    const Math::Vec3 localTip = tipOf(gizmo, c_view, turned, 0);
    EXPECT_NEAR(localTip.x, 0.0F, 1e-4F);
    EXPECT_LT(localTip.z, 0.0F);
    const Math::Vec3 localGrab = lerp(turned.position, localTip, 0.6F);
    const Math::Vec3 localGrabOnScreen = screenOf(c_view, localGrab);
    ASSERT_TRUE(gizmo.beginDrag(c_view, turned, GizmoHandle::X, localGrabOnScreen.x, localGrabOnScreen.y));
    const Math::Vec3 localTo = screenOf(c_view, {localGrab.x, localGrab.y, localGrab.z - 1.0F});
    const std::optional<Math::Transform> localMoved = gizmo.drag(c_view, localTo.x, localTo.y, {});
    ASSERT_TRUE(localMoved);
    EXPECT_NEAR(localMoved->position.z, -1.0F, 1e-3F);
    EXPECT_NEAR(localMoved->position.x, 0.0F, 1e-4F);
}

TEST(GizmoTest, RotatesAroundTheAxis)
{
    Gizmo gizmo;
    gizmo.setMode(GizmoMode::Rotate);
    // The Y ring lies in the XZ plane, radius = the gizmo's size (the length of the move arrow).
    Gizmo arrows;
    const Math::Vec3 tipX = tipOf(arrows, c_view, c_origin, 0);
    const float size = tipX.x;
    const auto onRing = [size](float degrees)
    {
        const float angle = Math::toRadians(degrees);
        return Math::Vec3{std::sin(angle) * size, 0.0F, std::cos(angle) * size};
    };
    // At 20°: the X ring crosses the Y ring at 0° and 180°, so grab where only the Y ring is.
    const Math::Vec3 grab = screenOf(c_view, onRing(20.0F));
    ASSERT_EQ(gizmo.hitTest(c_view, c_origin, grab.x, grab.y), GizmoHandle::Y);
    ASSERT_TRUE(gizmo.beginDrag(c_view, c_origin, GizmoHandle::Y, grab.x, grab.y));

    // Sweeping 40° round the ring turns the entity 40° about Y: its forward points where the cursor went.
    const Math::Vec3 to = screenOf(c_view, onRing(60.0F));
    const std::optional<Math::Transform> turned = gizmo.drag(c_view, to.x, to.y, {});
    ASSERT_TRUE(turned);
    const Math::Vec3 forward = Math::rotate(turned->rotation, Math::c_forward);
    EXPECT_NEAR(forward.x, std::sin(Math::toRadians(40.0F)), 1e-3F);
    EXPECT_NEAR(forward.y, 0.0F, 1e-4F);
    EXPECT_NEAR(forward.z, std::cos(Math::toRadians(40.0F)), 1e-3F);
    EXPECT_EQ(turned->position.x, 0.0F) << "turning doesn't move it";

    // Snapping: 15° steps, so 40° becomes 45°.
    const std::optional<Math::Transform> snapped =
        gizmo.drag(c_view, to.x, to.y, {.enabled = true, .angleDegrees = 15.0F});
    ASSERT_TRUE(snapped);
    EXPECT_NEAR(Math::toDegrees(Math::angleBetween(snapped->rotation, Math::Quat{})), 45.0F, 0.05F);

    // Turned onto an entity that's already turned: the turn adds to its rotation.
    gizmo.endDrag();
    const Math::Transform already{.position = {}, .rotation = Math::fromAxisAngle(Math::c_up, Math::toRadians(30.0F))};
    ASSERT_TRUE(gizmo.beginDrag(c_view, already, GizmoHandle::Y, grab.x, grab.y));
    const std::optional<Math::Transform> more = gizmo.drag(c_view, to.x, to.y, {});
    ASSERT_TRUE(more);
    EXPECT_NEAR(Math::toDegrees(Math::angleBetween(more->rotation, Math::Quat{})), 70.0F, 0.1F);
}

TEST(GizmoTest, ScalesAndHitTests)
{
    Gizmo gizmo;
    gizmo.setMode(GizmoMode::Scale);
    const Math::Vec3 tipX = tipOf(gizmo, c_view, c_origin, 0);
    const Math::Vec3 tipY = tipOf(gizmo, c_view, c_origin, 1);
    const Math::Vec3 tipZ = tipOf(gizmo, c_view, c_origin, 2);
    const Math::Vec3 centre = screenOf(c_view, c_origin.position);

    // Each handle where it's drawn; the centre box in the middle; nothing far away.
    for (const auto& [tip, handle] :
         {std::pair{tipX, GizmoHandle::X}, std::pair{tipY, GizmoHandle::Y}, std::pair{tipZ, GizmoHandle::Z}})
    {
        const Math::Vec3 onScreen = screenOf(c_view, lerp(c_origin.position, tip, 0.7F));
        EXPECT_EQ(gizmo.hitTest(c_view, c_origin, onScreen.x, onScreen.y), handle);
    }
    EXPECT_EQ(gizmo.hitTest(c_view, c_origin, centre.x, centre.y), GizmoHandle::Uniform);
    EXPECT_EQ(gizmo.hitTest(c_view, c_origin, centre.x + 300.0F, centre.y + 200.0F), GizmoHandle::None);

    // Dragging the X box half as far again out scales X by 1.5 only.
    const Math::Vec3 grab = screenOf(c_view, tipX);
    ASSERT_TRUE(gizmo.beginDrag(c_view, c_origin, GizmoHandle::X, grab.x, grab.y));
    const Math::Vec3 to = screenOf(c_view, lerp(c_origin.position, tipX, 1.5F));
    const std::optional<Math::Transform> wider = gizmo.drag(c_view, to.x, to.y, {});
    ASSERT_TRUE(wider);
    EXPECT_NEAR(wider->scale.x, 1.5F, 1e-3F);
    EXPECT_FLOAT_EQ(wider->scale.y, 1.0F);
    EXPECT_FLOAT_EQ(wider->scale.z, 1.0F);
    const std::optional<Math::Transform> snapped = gizmo.drag(c_view, to.x, to.y, {.enabled = true, .scale = 0.25F});
    ASSERT_TRUE(snapped);
    EXPECT_NEAR(snapped->scale.x, 1.5F, 1e-5F) << "a multiple of 0.25";
    gizmo.endDrag();

    // The centre box: a gizmo's length to the right doubles every axis.
    ASSERT_TRUE(gizmo.beginDrag(c_view, c_origin, GizmoHandle::Uniform, centre.x, centre.y));
    const std::optional<Math::Transform> bigger = gizmo.drag(c_view, centre.x + Gizmo::c_sizePixels, centre.y, {});
    ASSERT_TRUE(bigger);
    EXPECT_NEAR(bigger->scale.x, 2.0F, 1e-4F);
    EXPECT_NEAR(bigger->scale.y, 2.0F, 1e-4F);
    EXPECT_NEAR(bigger->scale.z, 2.0F, 1e-4F);
    const std::optional<Math::Transform> tiny =
        gizmo.drag(c_view, centre.x - 10.0F * Gizmo::c_sizePixels, centre.y, {});
    ASSERT_TRUE(tiny);
    EXPECT_GT(tiny->scale.x, 0.0F) << "never collapses to nothing";

    // The same size on screen near and far: about c_sizePixels long either way.
    for (const float distance : {1.0F, 4.0F})
    {
        const GizmoView view = viewFrom({6.0F * distance, 5.0F * distance, -8.0F * distance});
        Gizmo arrows;
        const Math::Vec3 a = screenOf(view, c_origin.position);
        // Measured along the camera's up: the Y axis seen from above is foreshortened, so use a vertical offset.
        const Math::Vec3 tip = tipOf(arrows, view, c_origin, 1);
        const Math::Vec3 b = screenOf(view, tip);
        const float pixels = std::hypot(b.x - a.x, b.y - a.y);
        EXPECT_GT(pixels, 0.5F * Gizmo::c_sizePixels) << "distance " << distance;
        EXPECT_LT(pixels, 1.1F * Gizmo::c_sizePixels) << "distance " << distance;
    }
}
