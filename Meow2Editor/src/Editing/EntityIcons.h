#pragma once

/// @file
/// @brief EntityIcons: icons in the editor viewport for entities nothing draws (cameras now; lights, sound sources
///        and the like later), and clicking them.

#include "Editing/Gizmo.h"

#include <Meow2/Core/Id/Uuid.h>
#include <Meow2/Math/Transform.h>
#include <Meow2/Scene/Scene.h>

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Meow2::Editor
{
/// @brief An icon's shape in its entity's own space (+Z forward, +Y up). Plain data.
///
/// `edges` and `fills` are in **icon units**: the icon is scaled so 1.3 units take c_iconPixels on screen at any
/// distance, like the gizmo, so a far object stays findable. `selectedLines` are in **metres**, drawn only while the
/// entity is selected (a camera's view volume).
struct IconShape
{
    std::vector<std::pair<Math::Vec3, Math::Vec3>> edges{};         ///< Outlines, drawn a few pixels thick.
    std::vector<std::array<Math::Vec3, 3>> fills{};                 ///< Filled triangles (a marker, a face).
    std::vector<std::pair<Math::Vec3, Math::Vec3>> selectedLines{}; ///< Thin lines in metres, when selected.
    Math::Vec3 centre{};                                            ///< The middle of the click target, icon units.
    bool emphasised = false; ///< Brighter: the one that matters most of its kind (the game's main camera).
};

/// @brief What icon shapes may depend on besides their entity. Plain data.
struct IconContext
{
    float aspect = 16.0F / 9.0F; ///< The game viewport's width / height (a camera's lens shape).
};

/// @brief Makes an entity's icon shape, or nothing to draw none for it.
using IconProvider = std::function<std::optional<IconShape>(Scene::Scene&, entt::entity, const IconContext&)>;

/// @brief An icon placed in the world. Plain data.
struct PlacedIcon
{
    entt::entity entity = entt::null; ///< Its entity.
    Math::Transform world{};          ///< Where the entity is, which way it faces; scale left at 1.
    IconShape shape{};                ///< Its shape.
    bool selected = false;            ///< Drawn orange, with its selectedLines.
};

/// @brief How large an icon is on screen: 1.3 icon units take this many pixels, whatever the distance.
inline constexpr float c_iconPixels = 48.0F;

/// @brief Which kinds of entity get an icon in the editor viewport, and their shapes.
///
/// Icons belong to the editor, not to the scene: nothing is added to an entity for it, and the game never sees them.
/// A kind is a component type that nothing draws (a camera, later a light or a sound source) and an IconProvider that
/// shapes its icon. An entity with several such components gets the icon of the kind added first.
/// @par Thread safety
/// Not thread-safe; the main thread.
class EntityIcons
{
public:
    /// @brief Adds a kind: every entity with a `Component` gets an icon shaped by `provider`.
    /// @tparam Component The component type that marks it.
    /// @param name     A name for it (debugging, tests).
    /// @param provider Shapes its icon.
    template <class Component>
    void add(std::string name, IconProvider provider)
    {
        m_kinds.push_back(Kind{.name = std::move(name),
                               .entities =
                                   [](Scene::Scene& scene)
                               {
                                   std::vector<entt::entity> found;
                                   for (const entt::entity entity : scene.registry().view<Component>())
                                   {
                                       found.push_back(entity);
                                   }
                                   return found;
                               },
                               .provider = std::move(provider)});
    }

    /// @brief The kinds' names, in the order they were added.
    /// @return The names.
    [[nodiscard]] std::vector<std::string> kinds() const;

    /// @brief Every icon of a scene, placed: one per entity of a kind that has a world transform.
    /// @param scene     The scene.
    /// @param selection The selected entities (their icons are drawn orange).
    /// @param context   What shapes may depend on.
    /// @return The icons.
    [[nodiscard]] std::vector<PlacedIcon> collect(Scene::Scene& scene, std::span<const EntityId> selection,
                                                  const IconContext& context) const;

private:
    /// @brief A kind of entity with an icon. Plain data.
    struct Kind
    {
        std::string name;                                                 ///< For debugging.
        std::function<std::vector<entt::entity>(Scene::Scene&)> entities; ///< The entities of the kind.
        IconProvider provider;                                            ///< Their shapes.
    };

    std::vector<Kind> m_kinds;
};

/// @brief The editor's icons: the camera (CameraIcons.h). Called once by the viewport controller.
/// @param icons Where to add them.
void registerDefaultIcons(EntityIcons& icons);

/// @brief What the icons draw: thick outlines and fills at c_iconPixels on screen, grey (light when emphasised,
///        orange when selected), plus the selected ones' lines.
/// @param view  The editor viewport's camera.
/// @param icons The placed icons.
/// @return Triangles for the outlines and fills, lines for selectedLines.
[[nodiscard]] GizmoShapes iconShapes(const GizmoView& view, std::span<const PlacedIcon> icons);

/// @brief The icon under a pixel: within half the icon's size of its centre on screen, nearest the viewer first.
/// @param view  The editor viewport's camera.
/// @param icons The placed icons.
/// @param x     Pixel column in the viewport.
/// @param y     Pixel row.
/// @return The icon's entity, or nothing.
[[nodiscard]] std::optional<entt::entity> pickIcon(const GizmoView& view, std::span<const PlacedIcon> icons, float x,
                                                   float y);
} // namespace Meow2::Editor
