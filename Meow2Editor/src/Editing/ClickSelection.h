#pragma once

/// @file
/// @brief ClickSelection: turns what a viewport click hit into the new selection.

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Scene/Scene.h>

#include <cstdint>
#include <span>
#include <vector>

namespace Meow2::Editor
{
/// @brief Decides what a click in the editor viewport selects, from the surfaces under the cursor (nearest first).
///
/// - A plain click selects the nearest hit.
/// - Clicking the same spot again (within c_sameSpotPixels) with the same hits under it selects the next one, and
///   wraps round: that's how a surface behind glass, or behind another object, is reached.
/// - Ctrl (`toggle`) adds the hit to the selection, or removes it if it's already selected; on nothing it changes
///   nothing.
/// - A plain click on nothing clears the selection.
///
/// The hits come from a GPU pick a frame or two after the click, so resolve() drops entities that are gone by then.
/// @par Thread safety
/// Not thread-safe.
class ClickSelection
{
public:
    /// @brief How far (in pixels, each way) a second click may be from the first and still step through the hits.
    static constexpr std::uint32_t c_sameSpotPixels = 2;

    /// @brief The saved IDs of the entities a pick hit: valid ones with an ID, each once, in the order given.
    /// @param scene The scene now.
    /// @param hits  The entities under the cursor, nearest first (entt::null and destroyed ones are skipped).
    /// @return Their IDs.
    [[nodiscard]] static std::vector<EntityId> resolve(const Scene::Scene& scene, std::span<const entt::entity> hits);

    /// @brief The selection after a click.
    /// @param current The selection before it.
    /// @param hits    What's under the cursor, nearest first (from resolve()).
    /// @param x       The click's pixel.
    /// @param y       The click's pixel.
    /// @param toggle  Ctrl was held: add or remove instead of replacing.
    /// @return The new selection.
    [[nodiscard]] std::vector<EntityId> click(std::span<const EntityId> current, std::span<const EntityId> hits,
                                              std::uint32_t x, std::uint32_t y, bool toggle);

private:
    std::vector<EntityId> m_lastHits; ///< What the last click had under it.
    std::uint32_t m_lastX = 0;        ///< Where it was.
    std::uint32_t m_lastY = 0;
    std::size_t m_lastIndex = 0; ///< Which of m_lastHits it chose.
};
} // namespace Meow2::Editor
