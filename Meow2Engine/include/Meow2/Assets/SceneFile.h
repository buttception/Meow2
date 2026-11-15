#pragma once

/// @file
/// @brief Scene files (`.m2scene`): a Scene's saved entities and components as TOML that merges in git, with versions
///        and migrations. See docs/wiki/SceneFiles.md.

#include <Meow2/Core/Result.h>
#include <Meow2/Scene/ComponentRegistry.h>
#include <Meow2/Scene/Scene.h>
#include <Meow2/Scene/TransformHierarchy.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::FileSystem
{
class FileSystem;
}

namespace Meow2::Assets
{
/// @brief The scene file format's version: raised only when the layout of the file itself changes (components carry
///        their own versions). A file from a newer format is refused rather than misread.
inline constexpr std::uint32_t c_sceneFormatVersion = 1;

/// @brief The extension of scene files.
inline constexpr std::string_view c_sceneExtension = ".m2scene";

/// @brief How a scene is written. Plain data.
struct SceneWriteOptions
{
    /// @brief Component name → the version to write it in, older than the current one (down migrations), so a
    ///        teammate on an older build can open the file. Components not listed are written in their current version.
    std::map<std::string, std::uint32_t, std::less<>> componentVersions{};
};

/// @brief What reading a scene found. Plain data.
struct SceneReadReport
{
    std::size_t entities = 0;          ///< Entities created.
    std::size_t migrated = 0;          ///< Components upgraded from an older version.
    std::size_t unknown = 0;           ///< Components kept as UnknownComponents (unknown, or from a newer version).
    std::vector<std::string> warnings; ///< Problems that were skipped over, one line each (also logged by callers).
};

/// @brief Writes a scene's saved entities as `.m2scene` text.
///
/// Deterministic: entities in EntityId order, each in one block (`[[entity]]` then a table per component, components
/// by name, fields in their descriptor's order), so the same scene always gives the same text and edits to different
/// entities merge. Transient entities (no EntityId) aren't written; references to them are written as "".
/// Components the registry doesn't know but the entity carries as UnknownComponents are written back as they were.
/// @param scene    The scene.
/// @param registry The components to write; others an entity has (runtime state, such as WorldTransform) are skipped.
/// @param options  Older component versions to write, if any.
/// @return The text, or an error when a down migration is missing.
[[nodiscard]] Result<std::string> writeScene(const Scene::Scene& scene, const Scene::ComponentRegistry& registry,
                                             const SceneWriteOptions& options = {});

/// @brief Reads `.m2scene` text into a scene, adding its entities.
///
/// Every component the registry knows is created with the file's fields (migrated up from an older version); others,
/// and components from a newer version than this build knows, are kept on their entity as UnknownComponents. Entities
/// with a LocalTransform are attached to the hierarchy. Problems in single fields, parents that don't exist and
/// parent cycles are skipped with a warning; nothing in a file can make the engine assert.
/// @param text      The file's text.
/// @param scene     Where the entities go; the registry's components must be registered in it (registerAll()).
/// @param hierarchy The scene's transform hierarchy.
/// @param registry  The components this app knows.
/// @param source    The file's name, for messages.
/// @return What was read, or an error, with nothing added, when the file isn't a scene this build can read: broken
///         TOML, another format, a newer format version, an entity without a valid ID, or an ID used twice or already
///         in the scene.
[[nodiscard]] Result<SceneReadReport> readScene(std::string_view text, Scene::Scene& scene,
                                                Scene::TransformHierarchy& hierarchy,
                                                const Scene::ComponentRegistry& registry, std::string_view source = {});

/// @brief writeScene() into a file, replaced atomically.
/// @param files    The file system.
/// @param path     Virtual path of the `.m2scene` file (a writable mount).
/// @param scene    The scene.
/// @param registry The components to write.
/// @param options  Older component versions to write, if any.
/// @return Nothing, or the error.
[[nodiscard]] Result<void> saveScene(FileSystem::FileSystem& files, std::string_view path, const Scene::Scene& scene,
                                     const Scene::ComponentRegistry& registry, const SceneWriteOptions& options = {});

/// @brief readScene() from a file.
/// @param files     The file system.
/// @param path      Virtual path of the `.m2scene` file.
/// @param scene     Where the entities go.
/// @param hierarchy The scene's transform hierarchy.
/// @param registry  The components this app knows.
/// @return What was read, or the error.
[[nodiscard]] Result<SceneReadReport> loadScene(FileSystem::FileSystem& files, std::string_view path,
                                                Scene::Scene& scene, Scene::TransformHierarchy& hierarchy,
                                                const Scene::ComponentRegistry& registry);
} // namespace Meow2::Assets
