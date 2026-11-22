/// @file
/// @brief Tests for the editor viewport's icons: the system (EntityIcons: which entities get one, drawing, clicking)
///        and the camera's icon.

#include "Editing/CameraIcons.h"
#include "Editing/EntityIcons.h"
#include "EditorTestRig.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Scene/Camera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <QJsonObject>
#include <vector>

namespace
{
namespace Math = Meow2::Math;
using Meow2::Editor::GizmoView;
using Meow2::Editor::PlacedIcon;
using namespace Qt::StringLiterals;
using EntityIconsTest = Meow2::Tests::EditorTest;

constexpr float c_width = 800.0F;
constexpr float c_height = 600.0F;

/// @brief A viewer at `eye` looking at the origin, in an 800 × 600 viewport.
GizmoView viewFrom(const Math::Vec3& eye)
{
    const float length = std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
    const Math::Transform camera{.position = eye,
                                 .rotation = Math::lookRotation({-eye.x / length, -eye.y / length, -eye.z / length})};
    return {.matrices = Math::cameraMatrices(camera, {}, c_width / c_height),
            .viewport = {.width = c_width, .height = c_height}};
}

float distance(const Math::Vec3& a, const Math::Vec3& b)
{
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/// @brief A camera icon at a point, looking down +X (sideways to a viewer on the Z axis).
PlacedIcon cameraAt(const Math::Vec3& position, entt::entity entity = entt::entity{1})
{
    return {.entity = entity,
            .world = {.position = position, .rotation = Math::lookRotation({1.0F, 0.0F, 0.0F})},
            .shape = Meow2::Editor::cameraIconShape({}, false, 16.0F / 9.0F)};
}

/// @brief The farthest a triangle corner reaches from a point.
float reach(const Meow2::Editor::GizmoShapes& shapes, const Math::Vec3& from)
{
    float farthest = 0.0F;
    for (const auto& triangle : shapes.triangles)
    {
        for (const Math::Vec3& corner : triangle.corners)
        {
            farthest = std::max(farthest, distance(corner, from));
        }
    }
    return farthest;
}

/// @brief A component nothing draws, for a kind of icon a test adds.
struct Beacon
{
    int strength = 0;
};
} // namespace

// Owner request (#231): cameras aren't invisible in the editor viewport.
TEST(CameraIcons, ShapeAroundTheCamera)
{
    const std::vector<PlacedIcon> icons{cameraAt({0.0F, 0.0F, 0.0F})};
    const GizmoView view = viewFrom({0.0F, 0.0F, -10.0F});
    const Meow2::Editor::GizmoShapes shapes = Meow2::Editor::iconShapes(view, icons);
    ASSERT_FALSE(shapes.triangles.empty()) << "thick outlines and the up marker";
    EXPECT_TRUE(shapes.lines.empty()) << "no view volume unless selected";
    // 48 pixels at 10 m in a 600-pixel-high 60° view is about 0.9 m.
    EXPECT_LT(reach(shapes, icons[0].world.position), 1.0F);
    // The same size on screen further away: in the world it grows with distance.
    const float far = reach(Meow2::Editor::iconShapes(viewFrom({0.0F, 0.0F, -40.0F}), icons), icons[0].world.position);
    EXPECT_NEAR(far / reach(shapes, icons[0].world.position), 4.0F, 0.4F);

    // A wider lens draws a wider opening.
    Math::CameraLens wide;
    wide.verticalFov = Math::toRadians(100.0F);
    const Meow2::Editor::IconShape narrowShape = Meow2::Editor::cameraIconShape({}, false, 1.0F);
    const Meow2::Editor::IconShape wideShape = Meow2::Editor::cameraIconShape(wide, false, 1.0F);
    const auto widest = [](const Meow2::Editor::IconShape& shape)
    {
        float y = 0.0F;
        for (const auto& [a, b] : shape.edges)
        {
            y = std::max({y, a.y, b.y});
        }
        return y;
    };
    EXPECT_GT(widest(wideShape), widest(narrowShape));
}

TEST(CameraIcons, SelectedShowsItsViewInOrange)
{
    PlacedIcon camera = cameraAt({0.0F, 0.0F, 0.0F});
    camera.selected = true;
    const std::vector<PlacedIcon> icons{camera};
    const Meow2::Editor::GizmoShapes shapes = Meow2::Editor::iconShapes(viewFrom({0.0F, 5.0F, -10.0F}), icons);
    ASSERT_EQ(shapes.lines.size(), 12U) << "the view volume's twelve edges";
    float farthest = 0.0F;
    for (const auto& line : shapes.lines)
    {
        farthest = std::max({farthest, line.from.x, line.to.x});
        EXPECT_GT(line.color.r, 0.9F) << "orange";
        EXPECT_LT(line.color.b, 0.3F);
    }
    EXPECT_NEAR(farthest, Meow2::Editor::c_cameraFrustumLength, 0.01F) << "out along the camera's +Z (world +X)";

    // Behind the viewer: nothing to draw.
    const std::vector<PlacedIcon> behind{cameraAt({0.0F, 10.0F, -20.0F})};
    EXPECT_TRUE(Meow2::Editor::iconShapes(viewFrom({0.0F, 5.0F, -10.0F}), behind).triangles.empty());
}

TEST(CameraIcons, ClickPicksTheNearest)
{
    const GizmoView view = viewFrom({0.0F, 0.0F, -10.0F});
    // Two cameras on the line of sight through the centre, the second nearer the viewer.
    const std::vector<PlacedIcon> icons{cameraAt({0.0F, 0.0F, 2.0F}, entt::entity{1}),
                                        cameraAt({0.0F, 0.0F, -2.0F}, entt::entity{2})};
    const auto centre = Meow2::Editor::pickIcon(view, icons, c_width / 2.0F, c_height / 2.0F);
    ASSERT_TRUE(centre);
    EXPECT_EQ(*centre, entt::entity{2}) << "the nearer one";
    EXPECT_FALSE(Meow2::Editor::pickIcon(view, icons, 50.0F, 50.0F)) << "far from both";
    EXPECT_FALSE(Meow2::Editor::pickIcon(view, {}, c_width / 2.0F, c_height / 2.0F)) << "no icons";
}

// The scene's cameras get icons with nothing added to them; another kind of invisible object is one add() away.
TEST_F(EntityIconsTest, EveryCameraGetsOneAndKindsCanBeAdded)
{
    ASSERT_TRUE(m_world.buildDefaultScene(nullptr));
    m_world.update(nullptr);
    Meow2::Scene::Scene& scene = m_world.scene();
    Meow2::Editor::EntityIcons icons;
    Meow2::Editor::registerDefaultIcons(icons);
    EXPECT_EQ(icons.kinds(), std::vector<std::string>{"Camera"});

    const std::size_t cameras = scene.registry().view<Meow2::Scene::Camera>().size();
    ASSERT_GT(cameras, 0U) << "the default scene has a main camera";
    std::vector<PlacedIcon> placed = icons.collect(scene, {}, {});
    ASSERT_EQ(placed.size(), cameras);
    const std::optional<entt::entity> main = Meow2::Scene::findMainCamera(scene);
    const auto mainIcon = std::ranges::find(placed, *main, &PlacedIcon::entity);
    ASSERT_NE(mainIcon, placed.end());
    EXPECT_TRUE(mainIcon->shape.emphasised) << "the main camera is brighter";
    EXPECT_FALSE(mainIcon->selected);

    // Selected: drawn orange.
    const std::vector<Meow2::EntityId> selection{scene.idOf(*main)};
    placed = icons.collect(scene, selection, {});
    EXPECT_TRUE(std::ranges::find(placed, *main, &PlacedIcon::entity)->selected);

    // A new kind: an entity with a Beacon gets its icon; the camera that also has one keeps the camera's.
    icons.add<Beacon>("Beacon",
                      [](Meow2::Scene::Scene&, entt::entity, const Meow2::Editor::IconContext&)
                      {
                          Meow2::Editor::IconShape shape;
                          shape.edges.emplace_back(Math::Vec3{-0.5F, 0.0F, 0.0F}, Math::Vec3{0.5F, 0.0F, 0.0F});
                          return std::optional{shape};
                      });
    // Made through the command, so it has a transform in the hierarchy like any editor entity.
    const QString beaconId =
        m_commands.execute(u"entity.create", {{u"name"_s, u"Beacon"_s}}).value().toObject().value(u"id").toString();
    const Meow2::Result<Meow2::Uuid> uuid = Meow2::Uuid::parse(beaconId.toStdString());
    ASSERT_TRUE(uuid);
    const entt::entity beacon = *scene.find(Meow2::EntityId{*uuid});
    scene.registry().emplace<Beacon>(beacon);
    scene.registry().emplace<Beacon>(*main);
    m_world.update(nullptr); // gives the new entity its world transform
    placed = icons.collect(scene, {}, {});
    EXPECT_EQ(placed.size(), cameras + 1);
    EXPECT_EQ(std::ranges::find(placed, beacon, &PlacedIcon::entity)->shape.edges.size(), 1U);
    EXPECT_TRUE(std::ranges::find(placed, *main, &PlacedIcon::entity)->shape.emphasised) << "the first kind's icon";
}
