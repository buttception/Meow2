/// @file
/// @brief Uuid text form (Uuid.h).

#include "Meow2/Core/Id/Uuid.h"

#include <array>
#include <cstddef>

namespace Meow2
{
namespace
{
constexpr std::size_t c_textLength = 36;
constexpr std::array<std::size_t, 4> c_dashPositions{8, 13, 18, 23};
constexpr std::string_view c_hexDigits = "0123456789abcdef";

/// @brief The value of a hex digit, or -1.
constexpr int hexValue(char digit)
{
    if (digit >= '0' && digit <= '9')
    {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f')
    {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F')
    {
        return digit - 'A' + 10;
    }
    return -1;
}

constexpr bool isDashPosition(std::size_t index)
{
    for (const std::size_t dash : c_dashPositions)
    {
        if (dash == index)
        {
            return true;
        }
    }
    return false;
}
} // namespace

std::string Uuid::toString() const
{
    std::string text;
    text.reserve(c_textLength);
    int bit = 124; // top nibble of the 128 bits
    for (std::size_t i = 0; i < c_textLength; ++i)
    {
        if (isDashPosition(i))
        {
            text.push_back('-');
            continue;
        }
        const std::uint64_t half = bit >= 64 ? m_high : m_low;
        text.push_back(c_hexDigits[(half >> (bit % 64)) & 0xFU]);
        bit -= 4;
    }
    return text;
}

Result<Uuid> Uuid::parse(std::string_view text)
{
    if (text.size() != c_textLength)
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("'{}' isn't a UUID: {} characters, not 36", text, text.size()));
    }
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    int nibbles = 0;
    for (std::size_t i = 0; i < c_textLength; ++i)
    {
        if (isDashPosition(i))
        {
            if (text[i] != '-')
            {
                return fail(ErrorCode::InvalidArgument, std::format("'{}' isn't a UUID: no dash at {}", text, i));
            }
            continue;
        }
        const int value = hexValue(text[i]);
        if (value < 0)
        {
            return fail(ErrorCode::InvalidArgument, std::format("'{}' isn't a UUID: '{}' isn't hex", text, text[i]));
        }
        std::uint64_t& half = nibbles < 16 ? high : low;
        half = (half << 4) | static_cast<std::uint64_t>(value);
        ++nibbles;
    }

    const Uuid id = fromParts(high, low);
    if (!id.isValid())
    {
        return fail(ErrorCode::InvalidArgument,
                    std::format("'{}' isn't an engine ID (needs version 8, variant 10 and a kind)", text));
    }
    return id;
}
} // namespace Meow2
