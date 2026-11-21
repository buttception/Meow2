#pragma once

/// @file
/// @brief SceneDocument: the project folder, the open scene file, unsaved changes and recent scenes, and the commands
///        that work on them (`project.open`, `scene.*`, `asset.*`).

#include "Commands/EditorCommandRegistry.h"
#include "Commands/UndoStack.h"
#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <filesystem>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

namespace Meow2::Editor
{
/// @brief The config key of the project folder (a native path).
inline constexpr const char* c_projectKey = "editor.project";
/// @brief The config key of the recent scenes: project-relative paths, newest first, one per line.
inline constexpr const char* c_recentScenesKey = "editor.recentScenes";
/// @brief How many recent scenes are kept.
inline constexpr std::size_t c_maxRecentScenes = 8;

/// @brief What a SceneDocument uses. Plain data; everything must outlive the document.
struct SceneDocumentDeps
{
    EditorCommandRegistry& commands; ///< Where its commands are registered.
    UndoStack& history;              ///< Its clean mark is "saved"; cleared when another scene opens.
    Config::Config& config;          ///< The project folder and recent scenes.
    EditorWorld& world;              ///< The scene being edited.
    EditorRenderer* renderer;        ///< Its GPU scene is emptied when another scene opens; null without a GPU.
    FileSystem::FileSystem& files;   ///< The project folder's mount.
    Assets::AssetManager& assets;    ///< Scans the project's `.meta` files.
    Log::Logger& log;                ///< What happened.
};

/// @brief The document side of the editor: which project folder is open, which scene file is being edited, whether
///        it has unsaved changes, and the recent scenes.
///
/// It registers these commands (techstack §20), so menus, the Assets dock and MCP all go through them:
///
/// | Command | Kind | Parameters | What it does |
/// |---|---|---|---|
/// | `project.open` | action | `path`, `discard` | Opens (or creates) a project folder and starts a new scene |
/// | `scene.new` | action | `discard` | A new scene: a main camera and an Environment |
/// | `scene.open` | action | `path`, `discard` | Opens a `.m2scene` of the project |
/// | `scene.save` | action | — | Saves to the scene's file (fails for an untitled scene) |
/// | `scene.saveAs` | action | `path` | Saves to a `.m2scene` of the project, which becomes the scene's file |
/// | `scene.info` | query | — | The scene's file, whether it has unsaved changes, the project, the recent scenes |
/// | `asset.list` | query | — | The project's scenes and models |
/// | `asset.importModel` | action | `path` | Brings a `.gltf`/`.glb` of the project into the scene (undoable) |
///
/// Paths are relative to the project folder (`Levels/Main.m2scene`); absolute paths and `..` are refused, so nothing
/// outside the project is read or written. Commands that would drop unsaved changes refuse unless `discard` is true:
/// the window asks the user first, and an agent has to say so.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class SceneDocument
{
public:
    /// @brief Registers the commands and opens the project folder from the config (default: `project/` next to the
    ///        exe), then the most recent scene, or a new one.
    /// @param deps What it uses.
    explicit SceneDocument(const SceneDocumentDeps& deps);

    /// @brief Removes its commands.
    ~SceneDocument();

    SceneDocument(const SceneDocument&) = delete;            ///< Not copyable: registers commands.
    SceneDocument& operator=(const SceneDocument&) = delete; ///< Not copyable: registers commands.

    /// @brief The open project folder.
    /// @return A native path.
    [[nodiscard]] const std::filesystem::path& projectFolder() const
    {
        return m_projectFolder;
    }

    /// @brief The scene's file, relative to the project.
    /// @return The path, or empty for an untitled scene.
    [[nodiscard]] const QString& scenePath() const
    {
        return m_scenePath;
    }

    /// @brief Whether the scene changed since it was opened, started or saved.
    /// @return True when there are unsaved changes.
    [[nodiscard]] bool hasUnsavedChanges() const
    {
        return !m_deps.history.isClean();
    }

    /// @brief The recent scenes, newest first.
    /// @return Project-relative paths.
    [[nodiscard]] const QStringList& recentScenes() const
    {
        return m_recent;
    }

    /// @brief A project-relative path as a native path, for the Assets dock and file dialogs.
    /// @param path Relative to the project.
    /// @return The native path.
    [[nodiscard]] std::filesystem::path nativePath(const QString& path) const;

    /// @brief A native path as a path relative to the project, for what a file dialog returns.
    /// @param path A native path.
    /// @return The relative path (generic separators), or empty if it isn't inside the project.
    [[nodiscard]] QString projectPath(const std::filesystem::path& path) const;

    /// @brief Copies a model from outside the project into it, with the rest of its folder (its `.bin` buffers and
    ///        textures), to `Models/<model name>/`. A model already copied there is reused.
    ///
    /// Only the window calls it, for a file the user picked in a dialog: commands never read outside the project
    /// (techstack §20), so an agent works with what's in the project.
    /// @param model The `.gltf` or `.glb`, a native path outside the project.
    /// @return Its copy's path relative to the project, or the error.
    [[nodiscard]] Result<QString> copyIntoProject(const std::filesystem::path& model);

private:
    /// @brief Mounts a project folder (creating it if needed) and scans its `.meta` files.
    [[nodiscard]] Result<void> mountProject(const std::filesystem::path& folder);
    /// @brief A project-relative path as a virtual path in the project's mount, after checking it.
    [[nodiscard]] Result<std::string> virtualPath(const QString& path,
                                                  std::initializer_list<const char*> extensions) const;
    /// @brief Fails when there are unsaved changes and `discard` isn't set.
    [[nodiscard]] Result<void> checkUnsaved(const QJsonObject& parameters) const;
    /// @brief Starts a new, untitled scene.
    void startNewScene();
    /// @brief Opens a scene file of the project (already checked).
    [[nodiscard]] Result<void> openScene(const QString& path);
    /// @brief Saves to a scene file of the project (already checked) and makes it the scene's file.
    [[nodiscard]] Result<void> saveScene(const QString& path);
    /// @brief Puts a scene at the front of the recent list and saves the config.
    void rememberScene(QString path);
    /// @brief The empty GPU scene for a new or opened scene; null without a GPU.
    [[nodiscard]] Renderer::GpuScene* freshGpu();
    /// @brief The GPU scene, or null.
    [[nodiscard]] Renderer::GpuScene* gpu();
    void registerCommands();

    SceneDocumentDeps m_deps;
    std::filesystem::path m_projectFolder; ///< Native.
    std::string m_mount;                   ///< The project folder's mount: a name from a hash of its path.
    QString m_scenePath;                   ///< Relative to the project; empty for an untitled scene.
    QStringList m_recent;                  ///< Newest first.
    std::vector<QString> m_commandNames;   ///< What it registered.
};
} // namespace Meow2::Editor
