/// @file
/// @brief ClickSelection (ClickSelection.h).

#include "Editing/ClickSelection.h"

#include <algorithm>

namespace Meow2::Editor
{
std::vector<EntityId> ClickSelection::resolve(const Scene::Scene& scene, std::span<const entt::entity> hits)
{
    std::vector<EntityId> ids;
    const auto& registry = scene.registry();
    for (const entt::entity entity : hits)
    {
        if (entity == entt::null || !registry.valid(entity))
        {
            continue; // destroyed since the pick was drawn
        }
        const auto* id = registry.try_get<Scene::IdComponent>(entity);
        if (id == nullptr)
        {
            continue; // a transient entity: never selected (it has no saved ID to name it by)
        }
        if (std::ranges::find(ids, id->id) == ids.end())
        {
            ids.push_back(id->id); // an entity drawn in several parts is listed once, at its nearest
        }
    }
    return ids;
}

std::vector<EntityId> ClickSelection::click(std::span<const EntityId> current, std::span<const EntityId> hits,
                                            std::uint32_t x, std::uint32_t y, bool toggle)
{
    const auto distance = [](std::uint32_t a, std::uint32_t b)
    {
        return a > b ? a - b : b - a;
    };
    const bool sameSpot = !m_lastHits.empty() && distance(x, m_lastX) <= c_sameSpotPixels &&
                          distance(y, m_lastY) <= c_sameSpotPixels && std::ranges::equal(hits, m_lastHits);
    const std::size_t index = sameSpot ? (m_lastIndex + 1) % hits.size() : 0;
    m_lastHits.assign(hits.begin(), hits.end());
    m_lastX = x;
    m_lastY = y;
    m_lastIndex = index;

    std::vector<EntityId> selection(current.begin(), current.end());
    if (hits.empty())
    {
        if (!toggle)
        {
            selection.clear();
        }
        return selection;
    }
    const EntityId chosen = hits[index];
    if (!toggle)
    {
        return {chosen};
    }
    if (const auto found = std::ranges::find(selection, chosen); found != selection.end())
    {
        selection.erase(found);
    }
    else
    {
        selection.push_back(chosen);
    }
    return selection;
}
} // namespace Meow2::Editor
