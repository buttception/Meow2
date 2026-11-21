/// @file
/// @brief Tests for ClickSelection: what a click in the editor viewport selects from the surfaces under it.

#include "Editing/ClickSelection.h"

#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Scene/Scene.h>

#include <gtest/gtest.h>

#include <memory_resource>
#include <vector>

namespace
{
using Meow2::EntityId;
using Meow2::Editor::ClickSelection;
} // namespace

TEST(ViewportPickingTest, ClicksSelect)
{
    Meow2::UuidGenerator ids;
    Meow2::Scene::Scene scene{ids, *std::pmr::new_delete_resource()};
    const entt::entity near = scene.create("Near");
    const entt::entity middle = scene.create("Middle");
    const entt::entity far = scene.create("Far");
    const entt::entity gone = scene.create("Gone");
    const auto idOf = [&scene](entt::entity entity)
    {
        return scene.registry().get<Meow2::Scene::IdComponent>(entity).id;
    };
    const EntityId nearId = idOf(near);
    const EntityId middleId = idOf(middle);
    const EntityId farId = idOf(far);
    scene.destroy(gone);

    // The pick's entities, nearest first: an entity in two parts is listed once, gone and null ones are skipped.
    const std::vector<entt::entity> picked{near, gone, near, middle, entt::null, far};
    const std::vector<EntityId> hits = ClickSelection::resolve(scene, picked);
    ASSERT_EQ(hits, (std::vector<EntityId>{nearId, middleId, farId}));

    ClickSelection clicks;
    // A click selects the nearest; the same spot again steps behind it, and wraps round.
    std::vector<EntityId> selection = clicks.click({}, hits, 100, 50, false);
    EXPECT_EQ(selection, std::vector<EntityId>{nearId});
    selection = clicks.click(selection, hits, 101, 49, false);
    EXPECT_EQ(selection, std::vector<EntityId>{middleId}) << "within two pixels: the next one";
    selection = clicks.click(selection, hits, 100, 50, false);
    EXPECT_EQ(selection, std::vector<EntityId>{farId});
    selection = clicks.click(selection, hits, 100, 50, false);
    EXPECT_EQ(selection, std::vector<EntityId>{nearId}) << "wraps round";

    // Elsewhere, or with other hits under it, starts again from the nearest.
    selection = clicks.click(selection, hits, 100, 50, false); // middle
    selection = clicks.click(selection, hits, 140, 50, false);
    EXPECT_EQ(selection, std::vector<EntityId>{nearId}) << "another spot";
    const std::vector<EntityId> others{middleId, farId};
    selection = clicks.click(selection, others, 140, 50, false);
    EXPECT_EQ(selection, std::vector<EntityId>{middleId}) << "something else under the cursor";

    // Ctrl adds and removes; on nothing it keeps the selection.
    selection = clicks.click(std::vector<EntityId>{farId}, std::vector<EntityId>{nearId}, 10, 10, true);
    EXPECT_EQ(selection, (std::vector<EntityId>{farId, nearId}));
    selection = clicks.click(selection, std::vector<EntityId>{farId}, 300, 300, true);
    EXPECT_EQ(selection, std::vector<EntityId>{nearId}) << "already selected: removed";
    selection = clicks.click(selection, {}, 300, 300, true);
    EXPECT_EQ(selection, std::vector<EntityId>{nearId}) << "Ctrl on nothing changes nothing";

    // A plain click on nothing clears it.
    selection = clicks.click(selection, {}, 300, 300, false);
    EXPECT_TRUE(selection.empty());
}
