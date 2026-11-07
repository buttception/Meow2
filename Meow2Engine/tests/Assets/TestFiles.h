/// @file
/// @brief Builds small test files in memory: PNG images, base64 text and `.glb` containers.
///        So the importer tests need no sample assets, and run in CI.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Assets::Testing
{
/// @brief Appends a 32-bit big-endian number.
inline void putBigEndian(std::vector<std::byte>& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

/// @brief Appends a 32-bit little-endian number.
inline void putLittleEndian(std::vector<std::byte>& out, std::uint32_t value)
{
    for (int shift = 0; shift <= 24; shift += 8)
    {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

/// @brief CRC-32 as PNG chunks use it.
inline std::uint32_t crc32(std::span<const std::byte> bytes)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes)
    {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

/// @brief A PNG file, uncompressed (zlib "stored" blocks), so no compressor is needed.
/// @param width     Width in pixels.
/// @param height    Height in pixels.
/// @param channels  1 (grey) or 4 (RGBA).
/// @param pixels    width × height × channels bytes, top row first.
/// @return The file's bytes.
inline std::vector<std::byte> encodePng(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                        std::span<const std::uint8_t> pixels)
{
    std::vector<std::byte> png{std::byte{0x89}, std::byte{'P'},  std::byte{'N'},  std::byte{'G'},
                               std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    const auto chunk = [&png](std::string_view type, const std::vector<std::byte>& data)
    {
        putBigEndian(png, static_cast<std::uint32_t>(data.size()));
        std::vector<std::byte> typed;
        for (const char c : type)
        {
            typed.push_back(static_cast<std::byte>(c));
        }
        typed.insert(typed.end(), data.begin(), data.end());
        png.insert(png.end(), typed.begin(), typed.end());
        putBigEndian(png, crc32(typed));
    };

    std::vector<std::byte> header;
    putBigEndian(header, width);
    putBigEndian(header, height);
    header.push_back(std::byte{8});                                          // bit depth
    header.push_back(static_cast<std::byte>(channels == 4 ? 6 : 0));         // colour type: RGBA or grey
    header.insert(header.end(), {std::byte{0}, std::byte{0}, std::byte{0}}); // deflate, adaptive filters, no interlace
    chunk("IHDR", header);

    // Each row starts with filter type 0 (none).
    std::vector<std::byte> raw;
    const std::size_t rowBytes = std::size_t{width} * channels;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(std::byte{0});
        for (std::size_t x = 0; x < rowBytes; ++x)
        {
            raw.push_back(static_cast<std::byte>(pixels[y * rowBytes + x]));
        }
    }
    std::vector<std::byte> zlib{std::byte{0x78}, std::byte{0x01}};
    for (std::size_t offset = 0; offset < raw.size() || offset == 0; offset += 65535)
    {
        const std::size_t length = std::min<std::size_t>(65535, raw.size() - offset);
        zlib.push_back(static_cast<std::byte>(offset + length >= raw.size() ? 1 : 0)); // final block?
        zlib.push_back(static_cast<std::byte>(length & 0xFFU));
        zlib.push_back(static_cast<std::byte>(length >> 8U));
        zlib.push_back(static_cast<std::byte>(~length & 0xFFU));
        zlib.push_back(static_cast<std::byte>((~length >> 8U) & 0xFFU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        if (raw.empty())
        {
            break;
        }
    }
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::byte byte : raw)
    {
        a = (a + std::to_integer<std::uint32_t>(byte)) % 65521U;
        b = (b + a) % 65521U;
    }
    putBigEndian(zlib, (b << 16U) | a);
    chunk("IDAT", zlib);
    chunk("IEND", {});
    return png;
}

/// @brief Base64 text of some bytes.
inline std::string base64(std::span<const std::byte> bytes)
{
    constexpr std::string_view c_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        std::uint32_t group = std::to_integer<std::uint32_t>(bytes[i]) << 16U;
        if (i + 1 < bytes.size())
        {
            group |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8U;
        }
        if (i + 2 < bytes.size())
        {
            group |= std::to_integer<std::uint32_t>(bytes[i + 2]);
        }
        out += c_alphabet[(group >> 18U) & 63U];
        out += c_alphabet[(group >> 12U) & 63U];
        out += i + 1 < bytes.size() ? c_alphabet[(group >> 6U) & 63U] : '=';
        out += i + 2 < bytes.size() ? c_alphabet[group & 63U] : '=';
    }
    return out;
}

/// @brief The bytes of some plain values, for glTF buffers.
template <class T>
std::vector<std::byte> bytesOf(std::span<const T> values)
{
    const std::span<const std::byte> bytes = std::as_bytes(values);
    return {bytes.begin(), bytes.end()};
}

/// @brief A `.glb` file: the JSON chunk, then the binary chunk, each padded to 4 bytes.
inline std::vector<std::byte> makeGlb(std::string_view json, std::span<const std::byte> binary)
{
    std::vector<std::byte> jsonChunk;
    for (const char c : json)
    {
        jsonChunk.push_back(static_cast<std::byte>(c));
    }
    while (jsonChunk.size() % 4 != 0)
    {
        jsonChunk.push_back(std::byte{' '});
    }
    std::vector<std::byte> binChunk(binary.begin(), binary.end());
    while (binChunk.size() % 4 != 0)
    {
        binChunk.push_back(std::byte{0});
    }
    std::vector<std::byte> glb;
    putLittleEndian(glb, 0x46546C67U); // "glTF"
    putLittleEndian(glb, 2);
    putLittleEndian(glb, static_cast<std::uint32_t>(12 + 8 + jsonChunk.size() + 8 + binChunk.size()));
    putLittleEndian(glb, static_cast<std::uint32_t>(jsonChunk.size()));
    putLittleEndian(glb, 0x4E4F534AU); // "JSON"
    glb.insert(glb.end(), jsonChunk.begin(), jsonChunk.end());
    putLittleEndian(glb, static_cast<std::uint32_t>(binChunk.size()));
    putLittleEndian(glb, 0x004E4942U); // "BIN"
    glb.insert(glb.end(), binChunk.begin(), binChunk.end());
    return glb;
}
} // namespace Meow2::Assets::Testing
