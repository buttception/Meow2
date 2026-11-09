/// @file
/// @brief cookTexture(), the `.m2texture` file and the cache key.

#include "Assets/Texture/BlockCompression.h"

#include <Meow2/Assets/Texture/CookedTexture.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace Meow2::Assets
{
namespace
{
/// @brief An image of one colour.
Image flatImage(std::uint32_t width, std::uint32_t height, std::array<std::uint8_t, 4> colour)
{
    Image image{.width = width, .height = height, .pixels = {}};
    image.pixels.reserve(std::size_t{width} * height * 4);
    for (std::size_t i = 0; i < std::size_t{width} * height; ++i)
    {
        for (const std::uint8_t c : colour)
        {
            image.pixels.push_back(std::byte{c});
        }
    }
    return image;
}

/// @brief The largest difference of channel `c` from `expected` over every pixel.
int maxError(const std::vector<std::byte>& pixels, std::uint32_t c, std::uint8_t expected)
{
    int worst = 0;
    for (std::size_t i = c; i < pixels.size(); i += 4)
    {
        worst = std::max(worst, std::abs(std::to_integer<int>(pixels[i]) - int{expected}));
    }
    return worst;
}
} // namespace

TEST(CookedTexture, RoundTripsThroughAFile)
{
    const Result<CookedTexture> cooked = cookTexture(flatImage(8, 4, {10, 20, 30, 40}), TextureKind::Data, false);
    ASSERT_TRUE(cooked) << cooked.error().message;
    ASSERT_EQ(cooked->mips.size(), 4U) << "8 × 4, 4 × 2, 2 × 1, 1 × 1";

    std::vector<std::byte> file = encodeM2Texture(*cooked);
    const Result<CookedTexture> read = decodeM2Texture(file);

    ASSERT_TRUE(read) << read.error().message;
    EXPECT_EQ(read->format, CookedFormat::Rgba8Unorm);
    ASSERT_EQ(read->mips.size(), cooked->mips.size());
    for (std::size_t i = 0; i < read->mips.size(); ++i)
    {
        EXPECT_EQ(read->mips[i].width, cooked->mips[i].width);
        EXPECT_EQ(read->mips[i].height, cooked->mips[i].height);
        EXPECT_EQ(read->mips[i].bytes, cooked->mips[i].bytes);
    }

    std::vector<std::byte> badMagic = file;
    badMagic[0] = std::byte{'X'};
    EXPECT_EQ(decodeM2Texture(badMagic).error().code, ErrorCode::ParseError);
    std::vector<std::byte> badVersion = file;
    badVersion[4] = std::byte{99};
    EXPECT_EQ(decodeM2Texture(badVersion).error().code, ErrorCode::ParseError);
    std::vector<std::byte> truncated = file;
    truncated.pop_back();
    EXPECT_EQ(decodeM2Texture(truncated).error().code, ErrorCode::ParseError);

    // Each level consistent with itself, but the second isn't half the first: refused, not uploaded.
    CookedTexture notAChain = *cooked;
    notAChain.mips[1] = notAChain.mips[0];
    EXPECT_EQ(decodeM2Texture(encodeM2Texture(notAChain)).error().code, ErrorCode::ParseError);
}

TEST(CookedTexture, Bc7KeepsTheColour)
{
    // 128 rows: two strips of 64, so the strips are joined. The mips go down to 1 × 1, which pads to one block.
    const Result<CookedTexture> cooked = cookTexture(flatImage(64, 128, {200, 90, 30, 255}), TextureKind::Color, true);

    ASSERT_TRUE(cooked) << cooked.error().message;
    EXPECT_EQ(cooked->format, CookedFormat::Bc7Srgb);
    ASSERT_EQ(cooked->mips.size(), 8U);
    for (const CookedMip& mip : cooked->mips)
    {
        ASSERT_EQ(mip.bytes.size(), std::size_t{(mip.width + 3) / 4} * ((mip.height + 3) / 4) * 16);
        const std::uint32_t width = (mip.width + 3) / 4 * 4;
        const std::uint32_t height = (mip.height + 3) / 4 * 4;
        const Result<std::vector<std::byte>> pixels = decompressBlocks(mip.bytes, width, height, cooked->format);
        ASSERT_TRUE(pixels) << pixels.error().message;
        EXPECT_LE(maxError(*pixels, 0, 200), 2) << mip.width << " × " << mip.height;
        EXPECT_LE(maxError(*pixels, 1, 90), 2) << mip.width << " × " << mip.height;
        EXPECT_LE(maxError(*pixels, 2, 30), 2) << mip.width << " × " << mip.height;
    }
}

TEST(CookedTexture, NormalMapsUseBc5)
{
    const Result<CookedTexture> cooked =
        cookTexture(flatImage(16, 16, {160, 100, 230, 255}), TextureKind::NormalMap, true);

    ASSERT_TRUE(cooked) << cooked.error().message;
    EXPECT_EQ(cooked->format, CookedFormat::Bc5Unorm);
    const Result<std::vector<std::byte>> pixels = decompressBlocks(cooked->mips[0].bytes, 16, 16, cooked->format);
    ASSERT_TRUE(pixels) << pixels.error().message;
    EXPECT_LE(maxError(*pixels, 0, 160), 2) << "X";
    EXPECT_LE(maxError(*pixels, 1, 100), 2) << "Y";
}

TEST(CookedTexture, SizesThatArentBlocksStayRgba8)
{
    const Result<CookedTexture> cooked = cookTexture(flatImage(6, 4, {1, 2, 3, 4}), TextureKind::Color, true);

    ASSERT_TRUE(cooked) << cooked.error().message;
    EXPECT_EQ(cooked->format, CookedFormat::Rgba8Srgb) << "BC needs a top level in whole 4 × 4 blocks";
    EXPECT_FALSE(cookTexture(Image{}, TextureKind::Color, true)) << "an empty image is an error";
}

TEST(CookedTexture, JobsGiveTheSameBlocks)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "Meow2CookedTextureTest";
    Log::LogSystem logSystem({.appName = "Test", .directory = directory / "logs", .console = false});
    Log::Logger log = logSystem.createLogger("Engine");
    Jobs::JobSystem jobs{{.threadCount = 4}, log};
    Image image = flatImage(16, 256, {0, 0, 0, 255});
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
    {
        image.pixels[i] = std::byte{static_cast<std::uint8_t>(i * 7 / 3)};
    }

    const Result<CookedTexture> alone = cookTexture(image, TextureKind::Data, true);
    const Result<CookedTexture> parallel = cookTexture(image, TextureKind::Data, true, &jobs);

    ASSERT_TRUE(alone) << alone.error().message;
    ASSERT_TRUE(parallel) << parallel.error().message;
    ASSERT_EQ(alone->mips.size(), parallel->mips.size());
    for (std::size_t i = 0; i < alone->mips.size(); ++i)
    {
        EXPECT_EQ(alone->mips[i].bytes, parallel->mips[i].bytes) << "level " << i;
    }
}

TEST(CookedTexture, CacheKeyChangesWithTheInput)
{
    const Image image = flatImage(4, 4, {1, 2, 3, 4});
    Image changed = image;
    changed.pixels[5] = std::byte{99};

    EXPECT_EQ(textureCacheKey(image, TextureKind::Color, true), textureCacheKey(image, TextureKind::Color, true));
    EXPECT_EQ(textureCacheKey(image, TextureKind::Color, true).size(), 16U);
    EXPECT_NE(textureCacheKey(image, TextureKind::Color, true), textureCacheKey(changed, TextureKind::Color, true));
    EXPECT_NE(textureCacheKey(image, TextureKind::Color, true), textureCacheKey(image, TextureKind::Data, true));
    EXPECT_NE(textureCacheKey(image, TextureKind::Color, true), textureCacheKey(image, TextureKind::Color, false));
}
} // namespace Meow2::Assets
