/// @file
/// @brief Tests for SceneDocument: saving and opening scenes, unsaved changes, paths kept inside the project, recent
///        scenes, importing models (on Qt's offscreen platform, without a GPU).

#include "EditorTestRig.h"

#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Scene/Camera.h>
#include <Meow2/Scene/Environment.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>

namespace
{
using namespace Qt::StringLiterals;
using SceneDocumentTest = Meow2::Tests::EditorTest;

/// @brief A glTF file with one triangle under a node, its buffer embedded as a data URI.
std::string triangleGltf()
{
    const std::array<float, 9> positions{0.0F, 0.0F, 3.0F, 1.0F, 0.0F, 3.0F, 0.0F, 1.0F, 3.0F};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    QByteArray buffer(42, '\0');
    std::memcpy(buffer.data(), positions.data(), sizeof(positions));
    std::memcpy(buffer.data() + sizeof(positions), indices.data(), sizeof(indices));
    return std::format(
        R"({{"asset": {{"version": "2.0"}},
  "buffers": [{{"byteLength": 42, "uri": "data:application/octet-stream;base64,{}"}}],
  "bufferViews": [{{"buffer": 0, "byteOffset": 0, "byteLength": 36}}, {{"buffer": 0, "byteOffset": 36, "byteLength": 6}}],
  "accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 3], "max": [1, 1, 3]}},
                {{"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}}],
  "meshes": [{{"name": "Triangle", "primitives": [{{"attributes": {{"POSITION": 0}}, "indices": 1}}]}}],
  "nodes": [{{"name": "Child", "mesh": 0}}],
  "scenes": [{{"nodes": [0]}}], "scene": 0}})",
        buffer.toBase64().toStdString());
}

/// @brief scene.info's answer.
QJsonObject info(Meow2::Editor::EditorCommandRegistry& commands)
{
    return commands.execute(u"scene.info").value().toObject();
}
} // namespace

TEST_F(SceneDocumentTest, SaveAndOpenRoundTrip)
{
    // The document starts with a new, untitled scene: a main camera, and the Environment as its setting.
    EXPECT_TRUE(info(m_commands).value(u"path").toString().isEmpty());
    EXPECT_EQ(m_world.scene().entityCount(), 1U);
    EXPECT_TRUE(Meow2::Scene::hasEnvironment(m_world.scene()));
    ASSERT_TRUE(Meow2::Scene::findMainCamera(m_world.scene()));
    EXPECT_FALSE(m_commands.execute(u"scene.save")) << "untitled: scene.saveAs first";
    EXPECT_FALSE(m_commands.isEnabled(u"scene.save"));

    ASSERT_TRUE(m_commands.execute(u"scene.saveAs", {{u"path"_s, u"Levels/First.m2scene"_s}}));
    EXPECT_TRUE(std::filesystem::exists(m_project / "Levels" / "First.m2scene"));
    EXPECT_EQ(info(m_commands).value(u"path").toString(), u"Levels/First.m2scene"_s);
    EXPECT_TRUE(m_commands.isEnabled(u"scene.save"));

    ASSERT_TRUE(m_commands.execute(u"scene.new"));
    const Meow2::EntityId camera = m_world.scene().idOf(*Meow2::Scene::findMainCamera(m_world.scene()));
    ASSERT_TRUE(m_commands.execute(u"scene.open", {{u"path"_s, u"Levels/First.m2scene"_s}}));
    EXPECT_EQ(m_world.scene().entityCount(), 1U);
    EXPECT_TRUE(Meow2::Scene::hasEnvironment(m_world.scene())) << "read from the file's settings";
    EXPECT_NE(m_world.scene().idOf(*Meow2::Scene::findMainCamera(m_world.scene())), camera)
        << "the saved camera, not the new scene's";
    EXPECT_TRUE(m_world.environment().has_value());
    EXPECT_EQ(info(m_commands).value(u"path").toString(), u"Levels/First.m2scene"_s);

    EXPECT_FALSE(m_commands.execute(u"scene.open", {{u"path"_s, u"Levels/Missing.m2scene"_s}}));
    EXPECT_EQ(info(m_commands).value(u"path").toString(), u"Levels/First.m2scene"_s) << "a failed open changes nothing";
}

TEST_F(SceneDocumentTest, UnsavedChangesNeedDiscard)
{
    ASSERT_TRUE(m_commands.execute(u"scene.saveAs", {{u"path"_s, u"Main.m2scene"_s}}));
    EXPECT_FALSE(info(m_commands).value(u"unsaved").toBool());
    pushValueStep(0, 1); // any undoable change
    EXPECT_TRUE(info(m_commands).value(u"unsaved").toBool());
    EXPECT_TRUE(m_document->hasUnsavedChanges());

    EXPECT_FALSE(m_commands.execute(u"scene.new")) << "would drop the change";
    EXPECT_FALSE(m_commands.execute(u"scene.open", {{u"path"_s, u"Main.m2scene"_s}}));
    ASSERT_TRUE(m_commands.execute(u"scene.save"));
    EXPECT_FALSE(m_document->hasUnsavedChanges()) << "saved";

    pushValueStep(1, 2);
    ASSERT_TRUE(m_commands.execute(u"scene.open", {{u"path"_s, u"Main.m2scene"_s}, {u"discard"_s, true}}));
    EXPECT_FALSE(m_document->hasUnsavedChanges());
    EXPECT_FALSE(m_history.canUndo()) << "opening a scene clears the history: its steps were for the old scene";
}

TEST_F(SceneDocumentTest, PathsStayInTheProject)
{
    const std::string outside = (m_project.parent_path() / "Outside.m2scene").string();
    for (const QString& path : {QString::fromStdString(outside), u"../Outside.m2scene"_s, u"Levels/../../Up.m2scene"_s,
                                u"/Root.m2scene"_s, u"Scene.txt"_s, QString{}})
    {
        EXPECT_FALSE(m_commands.execute(u"scene.saveAs", {{u"path"_s, path}})) << path.toStdString();
    }
    EXPECT_FALSE(std::filesystem::exists(outside));
    EXPECT_TRUE(m_commands.execute(u"scene.saveAs", {{u"path"_s, u"Levels/./Two.M2SCENE"_s}}))
        << "the extension in any case";
    EXPECT_EQ(m_document->projectPath(m_project / "Levels" / "Two.M2SCENE"), u"Levels/Two.M2SCENE"_s);
    EXPECT_TRUE(m_document->projectPath(m_project.parent_path() / "Elsewhere.m2scene").isEmpty());
}

TEST_F(SceneDocumentTest, RecentScenes)
{
    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(
            m_commands.execute(u"scene.saveAs", {{u"path"_s, QString::fromStdString(std::format("S{}.m2scene", i))}}));
    }
    ASSERT_TRUE(m_commands.execute(u"scene.open", {{u"path"_s, u"S3.m2scene"_s}}));
    const QStringList recent = m_document->recentScenes();
    ASSERT_EQ(recent.size(), static_cast<qsizetype>(Meow2::Editor::c_maxRecentScenes));
    EXPECT_EQ(recent.front(), u"S3.m2scene"_s) << "the latest first";
    EXPECT_EQ(recent.at(1), u"S9.m2scene"_s);
    EXPECT_EQ(recent.count(u"S3.m2scene"_s), 1) << "no repeats";
    EXPECT_NE(m_config.getString(Meow2::Editor::c_recentScenesKey, "").find("S3.m2scene"), std::string::npos)
        << "kept in the config";

    // A new document (the editor starting again) reopens the most recent scene.
    m_document.reset();
    m_document = std::make_unique<Meow2::Editor::SceneDocument>(Meow2::Editor::SceneDocumentDeps{.commands = m_commands,
                                                                                                 .history = m_history,
                                                                                                 .config = m_config,
                                                                                                 .world = m_world,
                                                                                                 .renderer = nullptr,
                                                                                                 .files = m_files,
                                                                                                 .assets = m_assets,
                                                                                                 .log = m_log});
    EXPECT_EQ(m_document->scenePath(), u"S3.m2scene"_s);
}

TEST_F(SceneDocumentTest, ImportModelIsUndoable)
{
    std::filesystem::create_directories(m_project / "Models");
    std::ofstream{m_project / "Models" / "Triangle.gltf", std::ios::binary} << triangleGltf();

    const QJsonObject assets = m_commands.execute(u"asset.list").value().toObject();
    EXPECT_EQ(assets.value(u"models").toArray(), (QJsonArray{u"Models/Triangle.gltf"_s}));

    const std::size_t before = m_world.scene().entityCount();
    ASSERT_TRUE(m_commands.execute(u"asset.importModel", {{u"path"_s, u"Models/Triangle.gltf"_s}}));
    EXPECT_EQ(m_world.scene().entityCount(), before + 2) << "a root named after the file, and the model's node";
    EXPECT_TRUE(std::filesystem::exists(m_project / "Models" / "Triangle.gltf.meta")) << "stable mesh IDs";
    EXPECT_TRUE(m_document->hasUnsavedChanges());

    // Saved and opened again, the mesh is the same asset (its ID is in the model's .meta).
    ASSERT_TRUE(m_commands.execute(u"scene.saveAs", {{u"path"_s, u"WithModel.m2scene"_s}}));
    ASSERT_TRUE(m_commands.execute(u"scene.open", {{u"path"_s, u"WithModel.m2scene"_s}}));
    std::size_t renderers = 0;
    for (const auto [entity, renderer] : m_world.scene().registry().view<Meow2::Renderer::MeshRenderer>().each())
    {
        static_cast<void>(entity);
        EXPECT_TRUE(m_assets.findSubAsset(renderer.mesh).has_value()) << "found through the model's .meta";
        ++renderers;
    }
    EXPECT_EQ(renderers, 1U);

    ASSERT_TRUE(m_commands.execute(u"asset.importModel", {{u"path"_s, u"Models/Triangle.gltf"_s}}));
    EXPECT_EQ(m_world.scene().entityCount(), before + 4);
    ASSERT_TRUE(m_commands.execute(u"edit.undo"));
    EXPECT_EQ(m_world.scene().entityCount(), before + 2) << "undo removes what the import made";
    ASSERT_TRUE(m_commands.execute(u"edit.redo"));
    EXPECT_EQ(m_world.scene().entityCount(), before + 4) << "redo imports again";

    EXPECT_FALSE(m_commands.execute(u"asset.importModel", {{u"path"_s, u"Models/Missing.gltf"_s}}));
    EXPECT_FALSE(m_commands.execute(u"asset.importModel", {{u"path"_s, u"WithModel.m2scene"_s}})) << "not a model";
}

TEST_F(SceneDocumentTest, ModelsOutsideTheProjectAreCopiedIn)
{
    // A model in a folder of its own outside the project, with a file next to it (its textures, say).
    const std::filesystem::path outside = m_project.parent_path() / "Downloads" / "Triangle";
    std::filesystem::create_directories(outside);
    std::ofstream{outside / "Triangle.gltf", std::ios::binary} << triangleGltf();
    std::ofstream{outside / "Texture.png", std::ios::binary} << "png";

    EXPECT_TRUE(m_document->projectPath(outside / "Triangle.gltf").isEmpty()) << "not in the project";
    EXPECT_FALSE(m_commands.execute(u"asset.importModel", {{u"path"_s, QString{}}})) << "commands stay in the project";

    const Meow2::Result<QString> copied = m_document->copyIntoProject(outside / "Triangle.gltf");
    ASSERT_TRUE(copied) << copied.error().message;
    EXPECT_EQ(*copied, u"Assets/Models/Triangle/Triangle.gltf"_s) << "imports go in the project's Assets/";
    EXPECT_TRUE(std::filesystem::exists(m_project / "Assets" / "Models" / "Triangle" / "Texture.png"))
        << "the folder comes along";
    ASSERT_TRUE(m_commands.execute(u"asset.importModel", {{u"path"_s, *copied}}));
    EXPECT_EQ(*m_document->copyIntoProject(outside / "Triangle.gltf"), *copied) << "a second import reuses the copy";
    EXPECT_FALSE(m_document->copyIntoProject(outside / "Texture.png")) << "only models";
}

// Owner decision (#229): projects are laid out as in Unity: Assets/, Scenes/ and a project file, made when missing.
TEST_F(SceneDocumentTest, ProjectsHaveAssetsAndScenes)
{
    EXPECT_TRUE(std::filesystem::is_directory(m_project / "Assets"));
    EXPECT_TRUE(std::filesystem::is_directory(m_project / "Scenes"));
    EXPECT_TRUE(std::filesystem::exists(m_project / "project.m2project")) << "named after the folder";

    // A new project somewhere else: made with the same layout.
    const std::filesystem::path other = m_project.parent_path() / "Fresh Game";
    std::error_code ignored;
    std::filesystem::remove_all(other, ignored);
    ASSERT_TRUE(m_commands.execute(u"project.open",
                                   {{u"path"_s, QString::fromStdWString(other.wstring())}, {u"discard"_s, true}}));
    EXPECT_TRUE(std::filesystem::is_directory(other / "Assets"));
    EXPECT_TRUE(std::filesystem::is_directory(other / "Scenes"));
    std::ifstream file(other / "Fresh Game.m2project");
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_NE(text.find("name = \"Fresh Game\""), std::string::npos) << text;
    file.close();
    std::filesystem::remove_all(other, ignored);
}

// Owner request (#229): a model edited on disk while the editor runs is reloaded (here without a GPU: re-imported,
// so its mesh's new name shows).
TEST_F(SceneDocumentTest, ChangedModelsReload)
{
    std::filesystem::create_directories(m_project / "Assets" / "Models");
    const std::filesystem::path model = m_project / "Assets" / "Models" / "Triangle.gltf";
    std::ofstream{model, std::ios::binary} << triangleGltf();
    ASSERT_TRUE(m_commands.execute(u"asset.importModel", {{u"path"_s, u"Assets/Models/Triangle.gltf"_s}}));
    const std::vector<std::pair<Meow2::AssetId, std::string>> before = m_world.meshNames();
    ASSERT_EQ(before.size(), 1U);
    EXPECT_EQ(before.front().second, "Triangle: Triangle");
    EXPECT_EQ(m_world.reloadChangedModels(nullptr, true), 0U) << "nothing changed yet";

    // Saved again with another mesh name, a little later than the first (file times must differ).
    std::string edited = triangleGltf();
    edited.replace(edited.find("\"name\": \"Triangle\""), 18, "\"name\": \"Renamed!\"");
    std::ofstream{model, std::ios::binary} << edited;
    std::filesystem::last_write_time(model, std::filesystem::last_write_time(model) + std::chrono::seconds(2));
    EXPECT_EQ(m_world.reloadChangedModels(nullptr, true), 1U);
    EXPECT_EQ(m_world.meshName(before.front().first), "Triangle: Renamed!") << "the same mesh ID, the new contents";
    EXPECT_EQ(m_world.reloadChangedModels(nullptr, true), 0U) << "once per change";

    // A broken save keeps the old model.
    std::ofstream{model, std::ios::binary} << "not json";
    std::filesystem::last_write_time(model, std::filesystem::last_write_time(model) + std::chrono::seconds(4));
    EXPECT_EQ(m_world.reloadChangedModels(nullptr, true), 0U);
    EXPECT_EQ(m_world.meshName(before.front().first), "Triangle: Renamed!");
}
