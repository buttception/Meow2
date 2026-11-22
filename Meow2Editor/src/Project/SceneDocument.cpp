/// @file
/// @brief SceneDocument (SceneDocument.h).

#include "Project/SceneDocument.h"

#include "Commands/CommandSchema.h"

#include <Meow2/Assets/SceneFile.h>
#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Process.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <memory>
#include <QJsonArray>
#include <QJsonObject>
#include <system_error>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
std::string toUtf8(const QString& text)
{
    return text.toStdString(); // QString::toStdString() is UTF-8
}

/// @brief A path's extension in lower case ("GLB" files exist).
std::string lowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

QJsonObject pathParameter(const QString& description)
{
    return propertySchema(u"string"_s, description);
}

QJsonObject discardParameter()
{
    return propertySchema(u"boolean"_s,
                          u"Drop the scene's unsaved changes; without it the command fails when there are some."_s);
}
} // namespace

SceneDocument::SceneDocument(const SceneDocumentDeps& deps)
    : m_deps(deps)
{
    registerCommands();
    const std::string configured = deps.config.getString(c_projectKey, "");
    const std::filesystem::path folder =
        configured.empty() ? executablePath().parent_path() / "project" : std::filesystem::path{configured};
    if (Result<void> mounted = mountProject(folder); !mounted)
    {
        MEOW2_LOG_ERROR(m_deps.log, "No project folder ({}): {}", folder.string(), mounted.error());
    }
    for (const QString& line : QString::fromStdString(deps.config.getString(c_recentScenesKey, "")).split(u'\n'))
    {
        if (!line.isEmpty())
        {
            m_recent.push_back(line);
        }
    }
    // Back where the user left off: the most recent scene, if it still opens.
    if (!m_recent.isEmpty() && !m_mount.empty())
    {
        if (Result<void> opened = openScene(QString{m_recent.front()}); opened)
        {
            return;
        }
        else
        {
            MEOW2_LOG_WARN(m_deps.log, "{} didn't open: {}", toUtf8(m_recent.front()), opened.error());
        }
    }
    startNewScene();
}

SceneDocument::~SceneDocument()
{
    for (const QString& name : m_commandNames)
    {
        m_deps.commands.remove(name);
    }
}

std::filesystem::path SceneDocument::nativePath(const QString& path) const
{
    return m_projectFolder / std::filesystem::path{reinterpret_cast<const char8_t*>(toUtf8(path).c_str())};
}

QString SceneDocument::projectPath(const std::filesystem::path& path) const
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, m_projectFolder, error);
    if (error || relative.empty() || *relative.begin() == "..")
    {
        return {};
    }
    const std::u8string text = relative.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}

Result<QString> SceneDocument::copyIntoProject(const std::filesystem::path& model)
{
    if (m_mount.empty())
    {
        return fail(ErrorCode::NotFound, "no project folder is open");
    }
    const std::string extension = lowerExtension(model);
    if (extension != ".gltf" && extension != ".glb")
    {
        return fail(ErrorCode::InvalidArgument, std::format("{} isn't a .gltf or .glb file", model.string()));
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error))
    {
        return fail(ErrorCode::NotFound, model.string());
    }
    // The whole folder: a .gltf names its buffers and textures by paths relative to itself.
    const std::filesystem::path target = m_projectFolder / "Assets" / "Models" / model.stem();
    if (!std::filesystem::exists(target / model.filename(), error))
    {
        std::filesystem::create_directories(target, error);
        std::filesystem::copy(model.parent_path(), target,
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                              error);
        if (error)
        {
            return fail(ErrorCode::IoError, std::format("can't copy {} into the project: {}",
                                                        model.parent_path().string(), error.message()));
        }
        MEOW2_LOG_INFO(m_deps.log, "Copied {} into {}", model.parent_path().string(), target.string());
    }
    return projectPath(target / model.filename());
}

Result<void> SceneDocument::mountProject(const std::filesystem::path& folder)
{
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error)
    {
        return fail(ErrorCode::IoError, std::format("can't create {}: {}", folder.string(), error.message()));
    }
    // A mount named after the folder's path: the asset manager keeps its records by virtual path, so each project
    // folder gets its own name rather than one "project" mount pointed at different folders over time.
    const std::filesystem::path normal = std::filesystem::absolute(folder, error).lexically_normal();
    const std::string mount =
        std::format("p{:016x}", static_cast<std::uint64_t>(std::hash<std::string>{}(normal.generic_string())));
    if (!m_deps.files.isMounted(mount))
    {
        if (Result<void> mounted = m_deps.files.mountDirectory(mount, normal, FileSystem::MountAccess::ReadWrite);
            !mounted)
        {
            return Unexpected<Error>{mounted.error()};
        }
    }
    // The project's layout (as in Unity): Assets/ for everything imported, Scenes/ for scenes, and a project file
    // that names it. Made when missing, so any folder opened becomes a project.
    std::filesystem::create_directories(normal / "Assets", error);
    std::filesystem::create_directories(normal / "Scenes", error);
    bool hasProjectFile = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(normal, error))
    {
        hasProjectFile = hasProjectFile || entry.path().extension() == c_projectExtension;
    }
    if (!hasProjectFile)
    {
        const std::string name = normal.filename().string();
        const std::string text = std::format("# Meow2 project: Assets/ holds what's imported, Scenes/ the scenes.\n"
                                             "format = \"meow2-project\"\nversion = 1\nname = \"{}\"\n",
                                             name);
        if (Result<void> written =
                m_deps.files.writeText(std::format("{}/{}{}", mount, name, c_projectExtension), text);
            !written)
        {
            MEOW2_LOG_WARN(m_deps.log, "Project {}: no project file: {}", normal.string(), written.error());
        }
    }
    if (Result<std::size_t> scanned = m_deps.assets.scan(mount); !scanned)
    {
        MEOW2_LOG_WARN(m_deps.log, "Project {}: {}", normal.string(), scanned.error());
    }
    m_projectFolder = normal;
    m_mount = mount;
    MEOW2_LOG_INFO(m_deps.log, "Project: {}", normal.string());
    return {};
}

Result<std::string> SceneDocument::virtualPath(const QString& path, std::initializer_list<const char*> extensions) const
{
    if (m_mount.empty())
    {
        return fail(ErrorCode::NotFound, "no project folder is open");
    }
    const std::string text = toUtf8(path);
    const std::filesystem::path relative{reinterpret_cast<const char8_t*>(text.c_str())};
    // Inside the project only (techstack §20): no drive, no leading slash, no step up.
    if (text.empty())
    {
        return fail(ErrorCode::InvalidArgument, "no path given (a file outside the project folder has none)");
    }
    if (relative.has_root_name() || relative.has_root_directory() ||
        std::ranges::any_of(relative, [](const std::filesystem::path& part) { return part == ".."; }))
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("'{}' isn't a path inside the project (relative, without ..)", text));
    }
    const std::string extension = lowerExtension(relative);
    if (std::ranges::none_of(extensions, [&extension](const char* wanted) { return extension == wanted; }))
    {
        std::string allowed;
        for (const char* wanted : extensions)
        {
            allowed += allowed.empty() ? wanted : std::format(" or {}", wanted);
        }
        return fail(ErrorCode::InvalidArgument, std::format("'{}' isn't a {} file", text, allowed));
    }
    const std::u8string generic = relative.lexically_normal().generic_u8string();
    return m_mount + "/" + std::string{reinterpret_cast<const char*>(generic.data()), generic.size()};
}

Result<void> SceneDocument::checkUnsaved(const QJsonObject& parameters) const
{
    if (hasUnsavedChanges() && !parameters.value(u"discard").toBool(false))
    {
        return fail(ErrorCode::InvalidArgument,
                    "the scene has unsaved changes: save it first, or pass discard = true to drop them");
    }
    return {};
}

Renderer::GpuScene* SceneDocument::gpu()
{
    return m_deps.renderer != nullptr ? &m_deps.renderer->scene() : nullptr;
}

Renderer::GpuScene* SceneDocument::freshGpu()
{
    if (m_deps.renderer == nullptr)
    {
        return nullptr;
    }
    if (Result<void> reset = m_deps.renderer->resetScene(); !reset)
    {
        MEOW2_LOG_ERROR(m_deps.log, "The GPU scene wasn't emptied, old meshes stay loaded: {}", reset.error());
    }
    return &m_deps.renderer->scene();
}

void SceneDocument::startNewScene()
{
    m_deps.world.newScene([this] { return freshGpu(); });
    m_scenePath.clear();
    m_deps.history.clear(); // undo steps refer to the old scene's entities
}

Result<void> SceneDocument::openScene(const QString& path)
{
    Result<std::string> file = virtualPath(path, {".m2scene"});
    if (!file)
    {
        return Unexpected<Error>{file.error()};
    }
    Result<Assets::SceneReadReport> read = m_deps.world.openScene(*file, [this] { return freshGpu(); });
    if (!read)
    {
        return Unexpected<Error>{read.error()};
    }
    m_scenePath = path;
    m_deps.history.clear();
    rememberScene(path);
    MEOW2_LOG_INFO(m_deps.log, "Opened {}: {} entities ({} warnings)", toUtf8(path), read->entities,
                   read->warnings.size());
    return {};
}

Result<void> SceneDocument::saveScene(const QString& path)
{
    Result<std::string> file = virtualPath(path, {".m2scene"});
    if (!file)
    {
        return Unexpected<Error>{file.error()};
    }
    if (Result<void> saved = m_deps.world.saveScene(*file); !saved)
    {
        return Unexpected<Error>{saved.error()};
    }
    m_scenePath = path;
    m_deps.history.markClean();
    rememberScene(path);
    MEOW2_LOG_INFO(m_deps.log, "Saved {}", toUtf8(path));
    return {};
}

void SceneDocument::rememberScene(QString path)
{
    // By value: `path` may be an element of m_recent itself (reopening the most recent scene), which removeAll() below
    // would destroy while it's still needed.

    m_recent.removeAll(path);
    m_recent.prepend(path);
    while (static_cast<std::size_t>(m_recent.size()) > c_maxRecentScenes)
    {
        m_recent.removeLast();
    }
    m_deps.config.setString(c_recentScenesKey, toUtf8(m_recent.join(u'\n')));
    if (Result<void> saved = m_deps.config.save(); !saved)
    {
        MEOW2_LOG_WARN(m_deps.log, "Recent scenes not saved: {}", saved.error());
    }
}

void SceneDocument::registerCommands()
{
    const auto add = [this](CommandInfo info, CommandHandler handler)
    {
        m_commandNames.push_back(info.name);
        [[maybe_unused]] const Result<void> added = m_deps.commands.add(std::move(info), std::move(handler));
        MEOW2_ASSERT(added, "scene commands are registered once, by the one document");
    };

    add({.name = u"project.open"_s,
         .description =
             u"Opens a project: a folder (made if it doesn't exist) with Assets/ for what's imported, Scenes/ for "
             u"scenes and a <name>.m2project file, added if missing. Starts a new scene in it."_s,
         .parameters = objectSchema(
             {{.name = u"path"_s, .schema = pathParameter(u"The folder, a full native path."_s), .required = true},
              {.name = u"discard"_s, .schema = discardParameter(), .required = false}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (Result<void> unsaved = checkUnsaved(parameters); !unsaved)
            {
                return Unexpected<Error>{unsaved.error()};
            }
            const std::string text = toUtf8(parameters.value(u"path").toString());
            const std::filesystem::path folder{reinterpret_cast<const char8_t*>(text.c_str())};
            if (!folder.is_absolute())
            {
                return fail(ErrorCode::InvalidArgument, "the project folder must be a full path");
            }
            if (Result<void> mounted = mountProject(folder); !mounted)
            {
                return Unexpected<Error>{mounted.error()};
            }
            m_deps.config.setString(c_projectKey, m_projectFolder.string());
            m_recent.clear(); // recent scenes are paths in the old project
            m_deps.config.setString(c_recentScenesKey, "");
            static_cast<void>(m_deps.config.save());
            startNewScene();
            return CommandOutcome{};
        });

    add({.name = u"scene.new"_s,
         .description = u"Starts a new, untitled scene with a main camera and an Environment (sun and exposure)."_s,
         .parameters = objectSchema({{.name = u"discard"_s, .schema = discardParameter(), .required = false}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (Result<void> unsaved = checkUnsaved(parameters); !unsaved)
            {
                return Unexpected<Error>{unsaved.error()};
            }
            startNewScene();
            return CommandOutcome{};
        });

    add({.name = u"scene.open"_s,
         .description = u"Opens a scene file (.m2scene) of the project, with the models it uses."_s,
         .parameters =
             objectSchema({{.name = u"path"_s,
                            .schema = pathParameter(u"Relative to the project folder, e.g. Levels/Main.m2scene."_s),
                            .required = true},
                           {.name = u"discard"_s, .schema = discardParameter(), .required = false}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (Result<void> unsaved = checkUnsaved(parameters); !unsaved)
            {
                return Unexpected<Error>{unsaved.error()};
            }
            if (Result<void> opened = openScene(parameters.value(u"path").toString()); !opened)
            {
                return Unexpected<Error>{opened.error()};
            }
            return CommandOutcome{};
        });

    add({.name = u"scene.save"_s,
         .description = u"Saves the scene to its file. An untitled scene needs scene.saveAs."_s,
         .isEnabled =
             [this]
         {
             return !m_scenePath.isEmpty();
         }},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            if (m_scenePath.isEmpty())
            {
                return fail(ErrorCode::InvalidArgument, "the scene is untitled: use scene.saveAs with a path");
            }
            if (Result<void> saved = saveScene(m_scenePath); !saved)
            {
                return Unexpected<Error>{saved.error()};
            }
            return CommandOutcome{};
        });

    add({.name = u"scene.saveAs"_s,
         .description = u"Saves the scene to a .m2scene file of the project, which becomes the scene's file."_s,
         .parameters = objectSchema({{.name = u"path"_s,
                                      .schema = pathParameter(u"Relative to the project folder, ending in .m2scene."_s),
                                      .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (Result<void> saved = saveScene(parameters.value(u"path").toString()); !saved)
            {
                return Unexpected<Error>{saved.error()};
            }
            return CommandOutcome{};
        });

    add({.name = u"scene.info"_s,
         .description = u"The scene's file, whether it has unsaved changes, the project folder and recent scenes."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            return CommandOutcome{
                .result = QJsonObject{{u"path"_s, m_scenePath},
                                      {u"unsaved"_s, hasUnsavedChanges()},
                                      {u"project"_s, QString::fromStdString(m_projectFolder.string())},
                                      {u"recent"_s, QJsonArray::fromStringList(m_recent)},
                                      {u"entities"_s, static_cast<qint64>(m_deps.world.scene().entityCount())}}};
        });

    add({.name = u"asset.list"_s,
         .description = u"The project's scene files (.m2scene) and models (.gltf, .glb), relative to the project."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            QStringList scenes;
            QStringList models;
            std::error_code error;
            for (auto entry = std::filesystem::recursive_directory_iterator{m_projectFolder, error};
                 !error && entry != std::filesystem::recursive_directory_iterator{}; entry.increment(error))
            {
                if (!entry->is_regular_file(error))
                {
                    continue;
                }
                const std::string extension = lowerExtension(entry->path());
                if (extension == Assets::c_sceneExtension)
                {
                    scenes.push_back(projectPath(entry->path()));
                }
                else if (extension == ".gltf" || extension == ".glb")
                {
                    models.push_back(projectPath(entry->path()));
                }
            }
            scenes.sort();
            models.sort();
            return CommandOutcome{.result = QJsonObject{{u"scenes"_s, QJsonArray::fromStringList(scenes)},
                                                        {u"models"_s, QJsonArray::fromStringList(models)}}};
        });

    add({.name = u"asset.importModel"_s,
         .description = u"Brings a glTF model (.gltf or .glb) of the project into the scene, under one root entity."_s,
         .parameters = objectSchema(
             {{.name = u"path"_s, .schema = pathParameter(u"Relative to the project folder."_s), .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const QString path = parameters.value(u"path").toString();
            Result<std::string> source = virtualPath(path, {".gltf", ".glb"});
            if (!source)
            {
                return Unexpected<Error>{source.error()};
            }
            Result<std::vector<entt::entity>> made = m_deps.world.importModel(*source, gpu());
            if (!made)
            {
                return Unexpected<Error>{made.error()};
            }
            // Undo destroys what was made; redo imports again (new entities, the same meshes and mesh IDs).
            auto entities = std::make_shared<std::vector<entt::entity>>(std::move(*made));
            const std::string file = *source;
            const QString root =
                m_deps.world.scene().registry().get<Scene::NameComponent>(entities->front()).name.c_str();
            return CommandOutcome{
                .result = QJsonObject{{u"root"_s, QString::fromStdString(
                                                      m_deps.world.scene().idOf(entities->front()).uuid().toString())},
                                      {u"entities"_s, static_cast<qint64>(entities->size())}},
                .undoStep = UndoStep{.label = u"Import "_s + root,
                                     .undo = [this, entities] { m_deps.world.destroyEntities(*entities); },
                                     .redo =
                                         [this, entities, file]
                                     {
                                         if (Result<std::vector<entt::entity>> again =
                                                 m_deps.world.importModel(file, gpu()))
                                         {
                                             *entities = std::move(*again);
                                         }
                                     }}};
        });
}
} // namespace Meow2::Editor
