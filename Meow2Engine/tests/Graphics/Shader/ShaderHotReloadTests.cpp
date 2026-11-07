/// @file
/// @brief Tests for ShaderHotReload: edits of sources, includes and `.meta` files reach a running app; broken edits
/// don't.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderCooker.h>
#include <Meow2/Assets/Shader/ShaderHotReload.h>
#include <Meow2/Core/Events/EventBus.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Meow2::Assets
{
namespace
{
namespace fs = std::filesystem;

/// @brief A shader that reads a value from an include, watched by a hot reloader.
class ShaderHotReloadTest : public ::testing::Test
{
protected:
    ShaderHotReloadTest()
        : m_directory(makeDirectory())
        , m_logSystem({.appName = "Test", .directory = m_directory / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_events(*std::pmr::new_delete_resource())
        , m_files(m_log)
        , m_assets(m_files, m_ids, m_log)
    {
    }

    void SetUp() override
    {
        fs::create_directories(m_directory / "shaders");
        fs::create_directories(m_directory / "cooked");
        using FileSystem::MountAccess;
        ASSERT_TRUE(m_files.mountDirectory("shaders", m_directory / "shaders", MountAccess::ReadWrite).has_value());
        ASSERT_TRUE(m_files.mountDirectory("cooked", m_directory / "cooked", MountAccess::ReadWrite).has_value());
        Result<Shader::ShaderCompiler> compiler = Shader::ShaderCompiler::create(m_files);
        ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
        m_compiler.emplace(std::move(*compiler));
        m_cooker.emplace(m_files, *m_compiler, m_jobs, m_log);

        write("Value.hlsli", "#define VALUE 1\n");
        write("Main.hlsl", "#include \"Value.hlsli\"\n"
                           "RWStructuredBuffer<uint> g_values : register(u0);\n"
                           "[numthreads(1, 1, 1)]\n"
                           "void csMain() { g_values[0] = VALUE; }\n");
        m_id = m_ids.next<IdKind::Asset>();
        write("Main.hlsl.meta",
              std::format("id = \"{}\"\n[[entry]]\nname = \"csMain\"\nstage = \"compute\"\n", m_id.uuid()));
        ASSERT_TRUE(m_assets.scan("shaders").has_value());

        m_hotReload.emplace(ShaderHotReloadDeps{m_assets, *m_cooker, m_jobs, m_events, m_log});
        ASSERT_TRUE(m_hotReload->watchAll().has_value());
    }

    void write(const std::string& name, const std::string& text)
    {
        ASSERT_TRUE(m_files.writeText(std::format("shaders/{}", name), text).has_value());
    }

    /// @brief Edits a file with a clearly later timestamp: file times are too coarse for edits within a test.
    void edit(const std::string& name, const std::string& text)
    {
        const fs::path path = m_directory / "shaders" / name;
        const fs::file_time_type before = fs::last_write_time(path);
        write(name, text);
        fs::last_write_time(path, before + std::chrono::seconds{++m_edits * 10});
    }

    void TearDown() override
    {
        m_hotReload.reset(); // before the cooker, compiler and job system it uses
    }

    /// @brief Polls, finishes the job and delivers the events.
    std::size_t pollAndFinish()
    {
        const std::size_t started = m_hotReload->poll();
        m_hotReload->wait();
        m_events.dispatchQueued();
        return started;
    }

    fs::path m_directory;
    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Jobs::JobSystem m_jobs{{.threadCount = 2}, m_log};
    Events::EventBus m_events;
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    AssetManager m_assets;
    std::optional<Shader::ShaderCompiler> m_compiler;
    std::optional<ShaderCooker> m_cooker;
    std::optional<ShaderHotReload> m_hotReload;
    AssetId m_id;
    int m_edits = 0;

private:
    static fs::path makeDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-reload-{}-{}", c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};
} // namespace

TEST_F(ShaderHotReloadTest, NothingChangedDoesNothing)
{
    EXPECT_EQ(m_hotReload->poll(), 0U);
    EXPECT_EQ(m_hotReload->poll(), 0U);
}

TEST_F(ShaderHotReloadTest, EditsReachHandles)
{
    AssetHandle<ShaderAsset> shader = *m_assets.load<ShaderAsset>(m_id);
    const std::vector<std::byte> before{shader.get()->bytecode({}, "csMain", Shader::ShaderTarget::Spirv)->begin(),
                                        shader.get()->bytecode({}, "csMain", Shader::ShaderTarget::Spirv)->end()};
    int reloads = 0;
    const Events::Subscription subscription = m_events.subscribe<ShaderReloaded>(
        [&](const ShaderReloaded& event)
        {
            EXPECT_EQ(event.id, m_id);
            ++reloads;
        });

    edit("Value.hlsli", "#define VALUE 2\n");
    EXPECT_EQ(pollAndFinish(), 1U) << "an included file is watched too";
    EXPECT_EQ(reloads, 1);
    EXPECT_EQ(shader.version(), 2U);
    const std::shared_ptr<const ShaderAsset> after = shader.get();
    const Result<std::span<const std::byte>> code = after->bytecode({}, "csMain", Shader::ShaderTarget::Spirv);
    ASSERT_TRUE(code.has_value());
    EXPECT_FALSE(std::equal(code->begin(), code->end(), before.begin(), before.end())) << "new bytecode";

    EXPECT_EQ(pollAndFinish(), 0U) << "once";
}

TEST_F(ShaderHotReloadTest, BrokenEditKeepsTheOldShader)
{
    AssetHandle<ShaderAsset> shader = *m_assets.load<ShaderAsset>(m_id);
    std::string failure;
    int reloads = 0;
    const Events::Subscription failed =
        m_events.subscribe<ShaderReloadFailed>([&](const ShaderReloadFailed& event) { failure = event.message; });
    const Events::Subscription reloaded = m_events.subscribe<ShaderReloaded>([&](const ShaderReloaded&) { ++reloads; });

    edit("Main.hlsl", "this is not hlsl\n");
    EXPECT_EQ(pollAndFinish(), 1U);
    EXPECT_NE(failure.find("shaders/Main.hlsl:1:"), std::string::npos) << failure;
    EXPECT_EQ(reloads, 0);
    EXPECT_EQ(shader.version(), 1U) << "the old shader stays in use";
    EXPECT_TRUE(shader.get()->bytecode({}, "csMain", Shader::ShaderTarget::Spirv).has_value());

    // Fixing it works without restarting anything.
    edit("Main.hlsl", "RWStructuredBuffer<uint> g_values : register(u0);\n"
                      "[numthreads(1, 1, 1)]\nvoid csMain() { g_values[0] = 9; }\n");
    EXPECT_EQ(pollAndFinish(), 1U);
    EXPECT_EQ(reloads, 1);
    EXPECT_EQ(shader.version(), 2U);
}

TEST_F(ShaderHotReloadTest, NewIncludesAreWatched)
{
    AssetHandle<ShaderAsset> shader = *m_assets.load<ShaderAsset>(m_id);
    write("Extra.hlsli", "#define EXTRA 1\n");
    edit("Main.hlsl", "#include \"Value.hlsli\"\n#include \"Extra.hlsli\"\n"
                      "RWStructuredBuffer<uint> g_values : register(u0);\n"
                      "[numthreads(1, 1, 1)]\nvoid csMain() { g_values[0] = VALUE + EXTRA; }\n");
    EXPECT_EQ(pollAndFinish(), 1U);
    EXPECT_EQ(shader.version(), 2U);

    EXPECT_EQ(pollAndFinish(), 0U) << "picks up the new include list";
    edit("Extra.hlsli", "#define EXTRA 2\n");
    EXPECT_EQ(pollAndFinish(), 1U) << "the include it didn't have at the start";
    EXPECT_EQ(shader.version(), 3U);
}

TEST_F(ShaderHotReloadTest, MetaEditsAddVariants)
{
    AssetHandle<ShaderAsset> shader = *m_assets.load<ShaderAsset>(m_id);
    const std::string meta = *m_files.readText("shaders/Main.hlsl.meta");
    edit("Main.hlsl.meta", meta + "[options]\nEXTRA_A = [0, 1]\n");
    EXPECT_EQ(pollAndFinish(), 1U);
    EXPECT_EQ(variantCount(shader.get()->meta()), 2U);
}
} // namespace Meow2::Assets
