/// @file
/// @brief Tests for ShaderCompiler: both targets, defines, includes through the FileSystem, and errors.

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/SharedLibrary.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace Meow2::Shader
{
namespace
{
namespace fs = std::filesystem;

/// @brief A read-write `shaders` mount in a fresh folder, and a compiler.
class ShaderCompilerTest : public ::testing::Test
{
protected:
    ShaderCompilerTest()
        : m_directory(makeDirectory())
        , m_logSystem({.appName = "Test", .directory = m_directory / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_files(m_log)
    {
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("shaders", m_directory, FileSystem::MountAccess::ReadWrite).has_value());
    }

    void write(const std::string& path, const std::string& text)
    {
        ASSERT_TRUE(m_files.writeText(path, text).has_value());
    }

    static ShaderCompileDesc computeDesc(ShaderTarget target)
    {
        return {.path = "shaders/Main.hlsl", .entryPoint = "csMain", .stage = ShaderStage::Compute, .target = target};
    }

    fs::path m_directory;
    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    FileSystem::FileSystem m_files;

private:
    static fs::path makeDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-shaders-{}-{}", c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        fs::create_directories(directory);
        return directory;
    }
};

constexpr const char* c_compute = "RWStructuredBuffer<uint> g_values : register(u0);\n"
                                  "[numthreads(8, 1, 1)]\n"
                                  "void csMain(uint3 id : SV_DispatchThreadID) { g_values[id.x] = id.x * 2; }\n";

bool isSpirv(const std::vector<std::byte>& bytecode)
{
    std::uint32_t magic = 0;
    if (bytecode.size() < sizeof(magic))
    {
        return false;
    }
    std::memcpy(&magic, bytecode.data(), sizeof(magic));
    return magic == 0x07230203U;
}

#if defined(_WIN32)
bool isDxil(const std::vector<std::byte>& bytecode)
{
    return bytecode.size() > 4 && std::memcmp(bytecode.data(), "DXBC", 4) == 0;
}
#endif
} // namespace

TEST_F(ShaderCompilerTest, CompilesToSpirv)
{
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    write("shaders/Main.hlsl", c_compute);
    for (const ShaderOptimization optimization : {ShaderOptimization::Debug, ShaderOptimization::Release})
    {
        ShaderCompileDesc desc = computeDesc(ShaderTarget::Spirv);
        desc.optimization = optimization;
        const Result<ShaderCompileOutput> output = compiler->compile(desc);
        ASSERT_TRUE(output.has_value()) << output.error().message;
        EXPECT_TRUE(isSpirv(output->bytecode));
    }
}

TEST_F(ShaderCompilerTest, CompilesToDxil)
{
#if !defined(_WIN32)
    GTEST_SKIP() << "DXIL is for D3D12, which is Windows-only";
#else
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    write("shaders/Main.hlsl", c_compute);
    for (const ShaderOptimization optimization : {ShaderOptimization::Debug, ShaderOptimization::Release})
    {
        ShaderCompileDesc desc = computeDesc(ShaderTarget::Dxil);
        desc.optimization = optimization;
        const Result<ShaderCompileOutput> output = compiler->compile(desc);
        ASSERT_TRUE(output.has_value()) << output.error().message;
        EXPECT_TRUE(isDxil(output->bytecode));
    }
#endif
}

TEST_F(ShaderCompilerTest, DefinesChangeTheOutput)
{
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    write("shaders/Main.hlsl", "RWStructuredBuffer<uint> g_values : register(u0);\n"
                               "[numthreads(1, 1, 1)]\n"
                               "void csMain() { g_values[0] = VALUE; }\n");
    ShaderCompileDesc desc = computeDesc(ShaderTarget::Spirv);
    const Result<ShaderCompileOutput> missing = compiler->compile(desc);
    ASSERT_FALSE(missing.has_value()) << "VALUE is undefined";

    const std::vector<ShaderDefine> one{{"VALUE", "1"}};
    const std::vector<ShaderDefine> two{{"VALUE", "2"}};
    desc.defines = one;
    const Result<ShaderCompileOutput> first = compiler->compile(desc);
    desc.defines = two;
    const Result<ShaderCompileOutput> second = compiler->compile(desc);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_NE(first->bytecode, second->bytecode);
}

TEST_F(ShaderCompilerTest, IncludesComeFromTheFileSystem)
{
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    write("shaders/Common.hlsli", "#pragma once\n#include \"lib/Value.hlsli\"\n");
    write("shaders/lib/Value.hlsli", "#define VALUE 7\n");
    write("shaders/Main.hlsl", "#include \"Common.hlsli\"\n"
                               "RWStructuredBuffer<uint> g_values : register(u0);\n"
                               "[numthreads(1, 1, 1)]\n"
                               "void csMain() { g_values[0] = VALUE; }\n");
    const Result<ShaderCompileOutput> output = compiler->compile(computeDesc(ShaderTarget::Spirv));
    ASSERT_TRUE(output.has_value()) << output.error().message;
    EXPECT_EQ(output->includes, (std::vector<std::string>{"shaders/Common.hlsli", "shaders/lib/Value.hlsli"}));
}

TEST_F(ShaderCompilerTest, ErrorsNameFileAndLine)
{
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    write("shaders/Main.hlsl", "[numthreads(1, 1, 1)]\nvoid csMain() { this is not hlsl; }\n");
    const Result<ShaderCompileOutput> broken = compiler->compile(computeDesc(ShaderTarget::Spirv));
    ASSERT_FALSE(broken.has_value());
    EXPECT_EQ(broken.error().code, ErrorCode::ParseError);
    EXPECT_NE(broken.error().message.find("shaders/Main.hlsl:2:"), std::string::npos) << broken.error().message;

    const Result<ShaderCompileOutput> missing =
        compiler->compile({.path = "shaders/Nothing.hlsl", .entryPoint = "main", .stage = ShaderStage::Compute});
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);

    write("shaders/Main.hlsl", "#include \"Gone.hlsli\"\n[numthreads(1, 1, 1)]\nvoid csMain() {}\n");
    const Result<ShaderCompileOutput> noInclude = compiler->compile(computeDesc(ShaderTarget::Spirv));
    ASSERT_FALSE(noInclude.has_value());
    EXPECT_NE(noInclude.error().message.find("Gone.hlsli"), std::string::npos) << noInclude.error().message;
}

TEST(SharedLibrary, MissingFileIsAnError)
{
    const Result<SharedLibrary> library = SharedLibrary::open("does-not-exist-meow2.library");
    ASSERT_FALSE(library.has_value());
    EXPECT_EQ(library.error().code, ErrorCode::NotFound);
}
} // namespace Meow2::Shader
