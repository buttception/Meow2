#pragma once

/// @file
/// @brief Test fixture with everything the editor's commands and windows need: a settings file in a fresh folder,
///        the settings, the history, the registry with the built-in commands, a console buffer, an input system and a
///        world without a GPU.

#include "Commands/EditorCommandRegistry.h"
#include "Commands/EditorCommands.h"
#include "Commands/EditorSettings.h"
#include "Commands/UndoStack.h"
#include "EditorMainWindow.h"
#include "Project/SceneDocument.h"
#include "QtTestApplication.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Id/UuidGenerator.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/ConsoleBuffer.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Graphics/Renderer/SceneModel.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>
#include <Meow2/Platform/Input/InputSystem.h>
#include <Meow2/Scene/ComponentRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <system_error>

namespace Meow2::Tests
{
/// @brief The editor's composition root in miniature, for tests (QApplication on the offscreen platform included).
class EditorTest : public QtTest
{
protected:
    EditorTest()
        : m_config(Config::Config::empty(configPath(), m_log))
        , m_settings(m_config)
        , m_commands(m_history, m_log)
    {
        const Result<void> registered = Editor::registerEditorCommands(
            {.commands = m_commands, .history = m_history, .settings = m_settings, .console = m_console});
        EXPECT_TRUE(registered) << registered.error().message;
        // A project folder of its own, next to the test's settings file, before the document opens it.
        m_project = m_config.path().parent_path() / "project";
        m_config.setString(Editor::c_projectKey, m_project.string());
        m_document = std::make_unique<Editor::SceneDocument>(Editor::SceneDocumentDeps{.commands = m_commands,
                                                                                       .history = m_history,
                                                                                       .config = m_config,
                                                                                       .world = m_world,
                                                                                       .renderer = nullptr,
                                                                                       .files = m_files,
                                                                                       .assets = m_assets,
                                                                                       .log = m_log});
    }

    /// @brief What the main window needs, made from the members.
    Editor::EditorMainWindowDeps windowDeps()
    {
        return {.commands = m_commands,
                .settings = m_settings,
                .console = m_console,
                .log = m_log,
                .world = m_world,
                .input = m_input,
                .document = *m_document,
                .renderer = nullptr};
    }

    /// @brief Records a step that sets `m_value` from `from` to `to`, as a command would.
    void pushValueStep(int from, int to)
    {
        m_value = to;
        m_history.push({.label = std::format("Set value to {}", to).c_str(),
                        .undo = [this, from] { m_value = from; },
                        .redo =
                            [this, to]
                        {
                            m_value = to;
                        }});
    }

    Log::ConsoleBuffer m_console{100};
    Config::Config m_config;
    Editor::EditorSettings m_settings;
    Editor::UndoStack m_history;
    Editor::EditorCommandRegistry m_commands;
    int m_value = 0; ///< What pushValueStep() changes.
    Events::EventBus m_events{Memory::globalTracker()};
    Input::InputSystem m_input{m_events};
    Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    UuidGenerator m_ids;
    FileSystem::FileSystem m_files{m_log};
    Assets::AssetManager m_assets{m_files, m_ids, m_log};
    /// @brief The engine's and the renderer's components, as the editor registers them.
    Scene::ComponentRegistry m_components = []
    {
        Scene::ComponentRegistry registry;
        Scene::registerSceneComponents(registry);
        Renderer::registerRenderComponents(registry);
        return registry;
    }();
    /// @brief Without a GPU: entities and cameras only.
    Editor::EditorWorld m_world{
        {.ids = m_ids, .jobs = m_jobs, .components = m_components, .assets = m_assets, .log = m_log}};
    std::filesystem::path m_project;                   ///< The project folder (temporary, per test).
    std::unique_ptr<Editor::SceneDocument> m_document; ///< Made in the constructor, after the project is set.

private:
    /// @brief A settings file in a fresh folder per test.
    static std::filesystem::path configPath()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            std::format("meow2-editor-config-{}-{}-{}", c_configName, info->test_suite_name(), info->name());
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory, ignored);
        return directory / "Meow2Editor.toml";
    }
};
} // namespace Meow2::Tests
