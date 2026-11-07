/// @file
/// @brief Tests for the bindless contract: the repository's shaders compile for both backends and validate.

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Graphics/Shader/BindlessLayout.h>
#include <Meow2/Graphics/Shader/ShaderCompiler.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Shader
{
namespace
{
namespace fs = std::filesystem;

/// @brief One entry point of a repository shader.
struct ShaderCase
{
    std::string_view path;
    std::string_view entry;
    ShaderStage stage;
};

constexpr std::array c_shaders{
    ShaderCase{"shaders/BindlessProbe.hlsl", "vsMain", ShaderStage::Vertex},
    ShaderCase{"shaders/BindlessProbe.hlsl", "psMain", ShaderStage::Pixel},
    ShaderCase{"shaders/BindlessProbe.hlsl", "csMain", ShaderStage::Compute},
    ShaderCase{"shaders/BindlessTexture.hlsl", "vsMain", ShaderStage::Vertex},
    ShaderCase{"shaders/BindlessTexture.hlsl", "psMain", ShaderStage::Pixel},
    ShaderCase{"shaders/BindlessTriangle.hlsl", "vsMain", ShaderStage::Vertex},
    ShaderCase{"shaders/BindlessTriangle.hlsl", "psMain", ShaderStage::Pixel},
    ShaderCase{"shaders/GpuDriven.hlsl", "csCull", ShaderStage::Compute},
    ShaderCase{"shaders/GpuDriven.hlsl", "vsMain", ShaderStage::Vertex},
    ShaderCase{"shaders/GpuDriven.hlsl", "psMain", ShaderStage::Pixel},
    ShaderCase{"shaders/Triangle.hlsl", "vsMain", ShaderStage::Vertex},
    ShaderCase{"shaders/Triangle.hlsl", "psMain", ShaderStage::Pixel},
};

const std::vector<ShaderDefine> c_defines{{"GRAYSCALE", "1"}};

/// @brief The repository's `shaders/` folder, read-only, and a compiler.
class BindlessTest : public ::testing::Test
{
protected:
    BindlessTest()
        : m_logSystem({.appName = "Test", .directory = workFolder() / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_files(m_log)
    {
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("shaders", MEOW2_SHADERS_DIR).has_value());
    }

    static fs::path workFolder()
    {
        return fs::temp_directory_path() / std::format("meow2-bindless-{}", c_configName);
    }

    Result<ShaderCompileOutput> compile(ShaderCompiler& compiler, const ShaderCase& shader, ShaderTarget target)
    {
        return compiler.compile({.path = std::string{shader.path},
                                 .entryPoint = std::string{shader.entry},
                                 .stage = shader.stage,
                                 .target = target,
                                 .defines = c_defines});
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    FileSystem::FileSystem m_files;
};

bool contains(const std::vector<std::byte>& bytecode, std::string_view text)
{
    const auto* first = reinterpret_cast<const char*>(bytecode.data());
    return std::string_view{first, bytecode.size()}.find(text) != std::string_view::npos;
}
} // namespace

TEST_F(BindlessTest, CompilesForSpirv)
{
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    for (const ShaderCase& shader : c_shaders)
    {
        const Result<ShaderCompileOutput> output = compile(*compiler, shader, ShaderTarget::Spirv);
        ASSERT_TRUE(output.has_value()) << shader.path << ": " << output.error().message;
    }
    const Result<ShaderCompileOutput> probe = compile(*compiler, c_shaders[1], ShaderTarget::Spirv);
    ASSERT_TRUE(probe.has_value());
    EXPECT_TRUE(contains(probe->bytecode, "SPV_EXT_descriptor_indexing"))
        << "the heap is emulated with descriptor indexing on Vulkan";
    EXPECT_EQ(std::count(probe->includes.begin(), probe->includes.end(), "shaders/Meow2Shared.hlsli"), 1);
}

TEST_F(BindlessTest, CompilesForDxil)
{
#if !defined(_WIN32)
    GTEST_SKIP() << "DXIL is for D3D12, which is Windows-only";
#else
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    for (const ShaderCase& shader : c_shaders)
    {
        const Result<ShaderCompileOutput> output = compile(*compiler, shader, ShaderTarget::Dxil);
        ASSERT_TRUE(output.has_value()) << shader.path << ": " << output.error().message;
    }
#endif
}

TEST_F(BindlessTest, ResourceHeapIsAlwaysBindingOne)
{
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    // No sampler in this shader: left to itself, DXC would put the heap on binding 0.
    const ShaderCase shader{"shaders/BindlessTriangle.hlsl", "vsMain", ShaderStage::Vertex};
    const Result<ShaderCompileOutput> output = compile(*compiler, shader, ShaderTarget::Spirv);
    ASSERT_TRUE(output.has_value()) << output.error().message;

    // SPIR-V is a stream of words: the first five are the header, and each instruction starts with its length and
    // opcode. OpDecorate (71) with Binding (33) is: target, 33, value.
    constexpr std::uint32_t c_opDecorate = 71;
    constexpr std::uint32_t c_decorationBinding = 33;
    std::vector<std::uint32_t> words(output->bytecode.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), output->bytecode.data(), words.size() * sizeof(std::uint32_t));
    int found = 0;
    for (std::size_t i = 5; i < words.size(); i += words[i] >> 16U)
    {
        if ((words[i] & 0xFFFFU) == c_opDecorate && words[i + 2] == c_decorationBinding)
        {
            EXPECT_EQ(words[i + 3], 1U);
            ++found;
        }
    }
    EXPECT_GT(found, 0);
}

TEST_F(BindlessTest, SpirvPassesTheValidator)
{
#if !defined(MEOW2_SPIRV_VAL)
    GTEST_SKIP() << "spirv-val not found (Vulkan SDK, or the spirv-tools package)";
#else
    Result<ShaderCompiler> compiler = ShaderCompiler::create(m_files);
    ASSERT_TRUE(compiler.has_value()) << compiler.error().message;
    fs::create_directories(workFolder());
    const fs::path file = workFolder() / "shader.spv";
    for (const ShaderCase& shader : c_shaders)
    {
        const Result<ShaderCompileOutput> output = compile(*compiler, shader, ShaderTarget::Spirv);
        ASSERT_TRUE(output.has_value()) << output.error().message;
        {
            std::ofstream stream{file, std::ios::binary};
            stream.write(reinterpret_cast<const char*>(output->bytecode.data()),
                         static_cast<std::streamsize>(output->bytecode.size()));
        }
        std::string command = std::format("\"{}\" --target-env vulkan1.3 \"{}\"", MEOW2_SPIRV_VAL, file.string());
#if defined(_WIN32)
        command = std::format("\"{}\"", command); // cmd.exe strips one pair of quotes when a command starts with one
#endif
        EXPECT_EQ(std::system(command.c_str()), 0) << shader.path << " " << shader.entry;
    }
#endif
}
} // namespace Meow2::Shader
