/// @file
/// @brief decodeImage(): PNG to RGBA8.

#include "Assets/TestFiles.h"

#include <Meow2/Assets/Texture/Image.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Meow2::Assets
{
namespace
{
std::uint8_t channel(const Image& image, std::uint32_t x, std::uint32_t y, std::uint32_t c)
{
    return std::to_integer<std::uint8_t>(image.pixels[(std::size_t{y} * image.width + x) * 4 + c]);
}
} // namespace

TEST(Image, DecodesRgbaPng)
{
    // 2 × 2: red, green / blue, half-transparent white. Top row first.
    const std::array<std::uint8_t, 16> pixels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
    const std::vector<std::byte> png = Testing::encodePng(2, 2, 4, pixels);

    const Result<Image> image = decodeImage(png);

    ASSERT_TRUE(image) << image.error().message;
    EXPECT_EQ(image->width, 2U);
    EXPECT_EQ(image->height, 2U);
    ASSERT_EQ(image->pixels.size(), 16U);
    EXPECT_EQ(channel(*image, 0, 0, 0), 255) << "top left is red";
    EXPECT_EQ(channel(*image, 1, 0, 1), 255) << "top right is green";
    EXPECT_EQ(channel(*image, 0, 1, 2), 255) << "bottom left is blue";
    EXPECT_EQ(channel(*image, 1, 1, 3), 128) << "alpha is kept";
}

TEST(Image, GreyBecomesOpaqueRgba)
{
    const std::array<std::uint8_t, 2> pixels{10, 200};
    const Result<Image> image = decodeImage(Testing::encodePng(2, 1, 1, pixels));

    ASSERT_TRUE(image) << image.error().message;
    EXPECT_EQ(channel(*image, 1, 0, 0), 200);
    EXPECT_EQ(channel(*image, 1, 0, 1), 200);
    EXPECT_EQ(channel(*image, 1, 0, 2), 200);
    EXPECT_EQ(channel(*image, 1, 0, 3), 255);
}

TEST(Image, GarbageIsAParseError)
{
    const std::vector<std::byte> garbage(64, std::byte{0x42});
    const Result<Image> image = decodeImage(garbage);
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error().code, ErrorCode::ParseError);
}

TEST(Image, EmptyIsAParseError)
{
    const Result<Image> image = decodeImage({});
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error().code, ErrorCode::ParseError);
}
TEST(Image, MipChainHalvesToOnePixel)
{
    const Image image{.width = 4, .height = 4, .pixels = std::vector<std::byte>(4 * 4 * 4, std::byte{200})};
    const std::vector<Image> chain = makeMipChain(image, false);
    ASSERT_EQ(chain.size(), 3U);
    EXPECT_EQ(chain[1].width, 2U);
    EXPECT_EQ(chain[1].height, 2U);
    EXPECT_EQ(chain[2].width, 1U);
    EXPECT_EQ(chain[2].pixels.size(), 4U);
    EXPECT_EQ(channel(chain[2], 0, 0, 0), 200) << "a flat image stays flat";
    EXPECT_EQ(mipLevelCount(4, 4), 3U);
    EXPECT_EQ(mipLevelCount(5, 3), 3U) << "odd sizes round down";
    EXPECT_EQ(mipLevelCount(1, 1), 1U);
}

TEST(Image, SrgbMipsAverageInLinearLight)
{
    // Black and white side by side: half the light is 0.5 linear, which sRGB encodes as 188, not 128.
    const Image image{.width = 2,
                      .height = 1,
                      .pixels = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
                                 std::byte{255}, std::byte{255}, std::byte{255}}};
    const std::vector<Image> colour = makeMipChain(image, true);
    ASSERT_EQ(colour.size(), 2U);
    EXPECT_NEAR(channel(colour[1], 0, 0, 0), 188, 1);
    EXPECT_EQ(channel(colour[1], 0, 0, 3), 255) << "alpha is averaged as it is";
    const std::vector<Image> data = makeMipChain(image, false);
    EXPECT_NEAR(channel(data[1], 0, 0, 0), 128, 1) << "data images are averaged as they are";
}
} // namespace Meow2::Assets
