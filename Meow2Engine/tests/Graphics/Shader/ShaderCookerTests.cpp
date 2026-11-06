/// @file
/// @brief Tests for ShaderCooker: every combination compiled, the cache, and failures that keep the old file.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Assets/Shader/ShaderCooker.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Meow2::Assets
{
namespace
{
namespace fs = std::filesystem;

constexpr const char* c_source = "#include \"Common.hlsli\"\n"
                                 "RWStructuredBuffer<uint> g_values : register(u0);\n"
                                 "[numthreads(1, 1, 1)]\n"
                                 "void csMain() { g_values[0] = VALUE + SCALE; }\n"
                                 "[numthreads(2, 1, 1)]\n"
                                 "void csOther() { g_values[1] = VALUE; }\n";

constexpr const char* c_metaBody = "[[entry]]\nname = \"csMain\"\nstage = \"compute\"\n"
                                   "[[entry]]\nname = \"csOther\"\nstage = \"compute\"\n"
                                   "[defines]\nVALUE = 3\n"
                                   "[options]\nSCALE = [1, 2, 3]\n";

/// @brief `shaders` and `cooked` mounts in a fresh folder, a job system, and a cooker.
class ShaderCookerTest : public ::testing::Test
{
protected:
    ShaderCookerTest()
        : m_directory(makeDirectory())
        , m_logSystem({.appName = "Test", .directory = m_directory / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
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

        write("shaders/Common.hlsli", "#define COMMON 1\n");
        write("shaders/Main.hlsl", c_source);
        write("shaders/Main.hlsl.meta", c_metaBody);
        m_record = {m_ids.next<IdKind::Asset>(), "shaders/Main.hlsl", "shaders/Main.hlsl.meta"};
    }

    void write(const std::string& path, const std::string& text)
    {
        ASSERT_TRUE(m_files.writeText(path, text).has_value());
    }

    Result<ShaderCookResult> cook(const ShaderCookOptions& options = {})
    {
        return m_cooker->cook(m_record, options);
    }

    fs::path m_directory;
    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    Jobs::JobSystem m_jobs{{.threadCount = 4}, m_log};
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    AssetManager m_assets;
    std::optional<Shader::ShaderCompiler> m_compiler;
    std::optional<ShaderCooker> m_cooker;
    AssetRecord m_record;

private:
    static fs::path makeDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-cook-{}-{}", c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};
} // namespace

TEST_F(ShaderCookerTest, CooksEveryCombination)
{
    const Result<ShaderCookResult> result = cook();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->upToDate);
    const auto targets = static_cast<std::uint32_t>(defaultShaderTargets().size());
    EXPECT_EQ(result->compiled, 3U * 2U * targets) << "3 variants × 2 entry points × targets";
    EXPECT_EQ(result->includes, std::vector<std::string>{"shaders/Common.hlsli"});

    Result<AssetHandle<ShaderAsset>> shader = m_assets.load<ShaderAsset>(m_record.id);
    ASSERT_TRUE(shader.has_value()) << shader.error().message;
    const std::shared_ptr<const ShaderAsset> loaded = shader->get();
    const std::vector<ShaderOptionChoice> scale2{{"SCALE", "2"}};
    const Result<ShaderVariantKey> key = variantKey(loaded->meta(), scale2);
    ASSERT_TRUE(key.has_value());
    const Result<std::span<const std::byte>> code = loaded->bytecode(*key, "csOther", Shader::ShaderTarget::Spirv);
    ASSERT_TRUE(code.has_value()) << code.error().message;
    EXPECT_FALSE(code->empty());
}

TEST_F(ShaderCookerTest, SecondCookDoesNothing)
{
    ASSERT_TRUE(cook().has_value());
    const Result<ShaderCookResult> second = cook();
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->upToDate);
    EXPECT_EQ(second->compiled, 0U);
    EXPECT_FALSE(cook({.force = true})->upToDate) << "force always cooks";
}

TEST_F(ShaderCookerTest, ChangesInvalidateTheCache)
{
    ASSERT_TRUE(cook().has_value());
    write("shaders/Common.hlsli", "#define COMMON 2\n");
    EXPECT_FALSE(cook()->upToDate) << "an included file changed";
    EXPECT_TRUE(cook()->upToDate);

    write("shaders/Main.hlsl", std::string{c_source} + "// a comment\n");
    EXPECT_FALSE(cook()->upToDate) << "the source changed";

    write("shaders/Main.hlsl.meta", std::string{c_metaBody} + "EXTRA = [0, 1]\n");
    const Result<ShaderCookResult> newOption = cook();
    ASSERT_TRUE(newOption.has_value());
    EXPECT_FALSE(newOption->upToDate) << "the .meta changed";

    EXPECT_TRUE(cook()->upToDate);
    EXPECT_FALSE(cook({.optimization = Shader::ShaderOptimization::Debug})->upToDate) << "other flags";
}

TEST_F(ShaderCookerTest, OtherShadersStayUpToDate)
{
    write("shaders/Other.hlsl", "RWStructuredBuffer<uint> g_v : register(u0);\n"
                                "[numthreads(1, 1, 1)]\nvoid csMain() { g_v[0] = 1; }\n");
    write("shaders/Other.hlsl.meta", "[[entry]]\nname = \"csMain\"\nstage = \"compute\"\n");
    const AssetRecord other{m_ids.next<IdKind::Asset>(), "shaders/Other.hlsl", "shaders/Other.hlsl.meta"};
    ASSERT_TRUE(cook().has_value());
    ASSERT_TRUE(m_cooker->cook(other, {}).has_value());

    write("shaders/Common.hlsli", "#define COMMON 2\n");
    EXPECT_FALSE(cook()->upToDate) << "includes it";
    EXPECT_TRUE(m_cooker->cook(other, {})->upToDate) << "doesn't include it";
}

TEST_F(ShaderCookerTest, FailureKeepsTheOldFile)
{
    ASSERT_TRUE(cook().has_value());
    const std::vector<std::byte> before = *m_files.readFile(cookedShaderPath(m_record.id));

    write("shaders/Main.hlsl", "this is not hlsl\n");
    const Result<ShaderCookResult> broken = cook();
    ASSERT_FALSE(broken.has_value());
    EXPECT_NE(broken.error().message.find("shaders/Main.hlsl:1:"), std::string::npos) << broken.error().message;
    EXPECT_EQ(*m_files.readFile(cookedShaderPath(m_record.id)), before) << "the last good cook stays";
}

TEST_F(ShaderCookerTest, CorruptCacheIsRecooked)
{
    ASSERT_TRUE(cook().has_value());
    ASSERT_TRUE(m_files.writeText(cookedShaderPath(m_record.id), "garbage").has_value());
    const Result<ShaderCookResult> result = cook();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->upToDate);
    EXPECT_TRUE(m_assets.load<ShaderAsset>(m_record.id).has_value());
}
} // namespace Meow2::Assets
