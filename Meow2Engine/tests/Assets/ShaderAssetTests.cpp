/// @file
/// @brief Tests for `.m2shader` files and ShaderAsset. Loading must not need DXC, which LoadsWithoutACompiler checks.

#include <Meow2/Assets/Shader/ShaderAsset.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Process.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace Meow2::Assets
{
namespace
{
namespace fs = std::filesystem;

const std::string c_metaText = "[[entry]]\nname = \"vsMain\"\nstage = \"vertex\"\n"
                               "[[entry]]\nname = \"psMain\"\nstage = \"pixel\"\n"
                               "[options]\nSKINNED = [0, 1]\n";

std::vector<std::byte> bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> result;
    for (const int value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

CookedShader sample()
{
    CookedShader shader;
    shader.cacheKey = 0x1234567890ABCDEFULL;
    shader.metaText = c_metaText;
    shader.dependencies = {"shaders/Common.hlsli", "shaders/lib/Value.hlsli"};
    shader.blobs.push_back({0, 0, Shader::ShaderTarget::Spirv, bytes({1, 2, 3})});
    shader.blobs.push_back({1, 1, Shader::ShaderTarget::Spirv, bytes({4, 5})});
    shader.blobs.push_back({1, 1, Shader::ShaderTarget::Dxil, bytes({6})});
    return shader;
}

bool sameBytes(std::span<const std::byte> a, const std::vector<std::byte>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}
} // namespace

TEST(ShaderFile, RoundTrip)
{
    const CookedShader original = sample();
    const Result<CookedShader> back = deserializeShader(serializeShader(original));
    ASSERT_TRUE(back.has_value()) << back.error().message;
    EXPECT_EQ(back->cacheKey, original.cacheKey);
    EXPECT_EQ(back->metaText, original.metaText);
    EXPECT_EQ(back->dependencies, original.dependencies);
    ASSERT_EQ(back->blobs.size(), 3U);
    EXPECT_EQ(back->blobs[2].target, Shader::ShaderTarget::Dxil);
    EXPECT_EQ(back->blobs[1].bytecode, bytes({4, 5}));
}

TEST(ShaderFile, CorruptFilesAreErrors)
{
    const std::vector<std::byte> good = serializeShader(sample());
    // Every truncation of a valid file must fail cleanly, never read past the end.
    for (std::size_t size = 0; size < good.size(); ++size)
    {
        EXPECT_FALSE(deserializeShader(std::span{good}.first(size)).has_value()) << "cut at " << size;
    }

    std::vector<std::byte> wrongMagic = good;
    wrongMagic[0] = std::byte{'X'};
    EXPECT_EQ(deserializeShader(wrongMagic).error().code, ErrorCode::ParseError);

    std::vector<std::byte> newerVersion = good;
    newerVersion[4] = std::byte{99};
    EXPECT_EQ(deserializeShader(newerVersion).error().code, ErrorCode::Unsupported);

    // A huge blob count mustn't allocate gigabytes: the count is checked against the file's size.
    std::vector<std::byte> hugeCount = good;
    const std::size_t lastBlobs = sizeof(std::uint32_t) * 2 + 8;
    hugeCount[lastBlobs + 0] = std::byte{0xFF};
    hugeCount[lastBlobs + 1] = std::byte{0xFF};
    hugeCount[lastBlobs + 2] = std::byte{0xFF};
    hugeCount[lastBlobs + 3] = std::byte{0xFF};
    EXPECT_FALSE(deserializeShader(hugeCount).has_value());
}

TEST(ShaderAsset, FindsItsBytecode)
{
    const Result<ShaderAsset> shader = ShaderAsset::fromCooked(sample());
    ASSERT_TRUE(shader.has_value()) << shader.error().message;
    ASSERT_EQ(shader->meta().entries.size(), 2U);

    const std::vector<ShaderOptionChoice> skinned{{"SKINNED", "1"}};
    const Result<ShaderVariantKey> key = variantKey(shader->meta(), skinned);
    ASSERT_TRUE(key.has_value());
    const Result<std::span<const std::byte>> pixel = shader->bytecode(*key, "psMain", Shader::ShaderTarget::Spirv);
    ASSERT_TRUE(pixel.has_value()) << pixel.error().message;
    EXPECT_TRUE(sameBytes(*pixel, bytes({4, 5})));

    const Result<std::span<const std::byte>> vertex = shader->bytecode({}, "vsMain", Shader::ShaderTarget::Spirv);
    ASSERT_TRUE(vertex.has_value());
    EXPECT_TRUE(sameBytes(*vertex, bytes({1, 2, 3})));

    EXPECT_EQ(shader->bytecode({}, "nothing", Shader::ShaderTarget::Spirv).error().code, ErrorCode::NotFound);
    EXPECT_EQ(shader->bytecode({}, "vsMain", Shader::ShaderTarget::Dxil).error().code, ErrorCode::NotFound)
        << "a combination that wasn't cooked";
}

TEST(ShaderAsset, LoadsWithoutACompiler)
{
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const fs::path directory =
        fs::temp_directory_path() / std::format("meow2-shader-{}-{}", c_configName, info->name());
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    fs::create_directories(directory / "cooked");

    Log::LogSystem logSystem{{.appName = "Test", .directory = directory / "logs", .console = false}};
    Log::Logger log = logSystem.createLogger("Engine");
    FileSystem::FileSystem files{log};
    ASSERT_TRUE(files.mountDirectory("cooked", directory / "cooked", FileSystem::MountAccess::ReadWrite).has_value());
    UuidGenerator ids;
    AssetManager assets{files, ids, log};

    // cmake/RunWithoutDxc.cmake runs this test from an empty folder: like Meow2Player, no DXC next to the executable.
    const fs::path executableFolder = executablePath().parent_path();
    if (!fs::exists(executableFolder / "dxcompiler.dll") && !fs::exists(executableFolder / "libdxcompiler.so"))
    {
        EXPECT_FALSE(Shader::ShaderCompiler::create(files).has_value());
    }

    const AssetId id = ids.next<IdKind::Asset>();
    EXPECT_EQ(assets.load<ShaderAsset>(id).error().code, ErrorCode::NotFound) << "not cooked yet";

    ASSERT_TRUE(files.writeFile(cookedShaderPath(id), serializeShader(sample())).has_value());
    Result<AssetHandle<ShaderAsset>> shader = assets.load<ShaderAsset>(id);
    ASSERT_TRUE(shader.has_value()) << shader.error().message;
    EXPECT_EQ(shader->get()->meta().entries[0].name, "vsMain");
    EXPECT_TRUE(shader->get()->bytecode({}, "vsMain", Shader::ShaderTarget::Spirv).has_value());
}
} // namespace Meow2::Assets
