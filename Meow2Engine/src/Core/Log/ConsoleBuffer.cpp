/// @file
/// @brief ConsoleBuffer (ConsoleBuffer.h).

#include "Meow2/Core/Log/ConsoleBuffer.h"

#include <algorithm>
#include <cstring>

namespace Meow2::Log
{
namespace
{
/// @brief `…` in UTF-8, marking a cut message.
constexpr std::string_view c_ellipsis = "\xE2\x80\xA6";

/// @brief The longest prefix of `text` of at most `limit` bytes that doesn't end inside a UTF-8 character.
std::size_t cutLength(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
    {
        return text.size();
    }
    std::size_t length = limit;
    // Continuation bytes are 10xxxxxx: step back to the first byte of the character the limit falls in.
    while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U)
    {
        --length;
    }
    return length;
}

/// @brief Copies `text` (cut to `limit`) into `storage`.
/// @return Bytes copied.
template <std::size_t N>
std::size_t copyInto(std::array<char, N>& storage, std::string_view text, std::size_t limit)
{
    const std::size_t length = cutLength(text, std::min(limit, N));
    std::memcpy(storage.data(), text.data(), length);
    return length;
}
} // namespace

ConsoleBuffer::ConsoleBuffer(std::size_t capacity)
    : m_slots(std::max<std::size_t>(capacity, 1))
{
}

void ConsoleBuffer::add(LogLevel level, std::string_view time, std::string_view logger, std::string_view message)
{
    const std::scoped_lock lock{m_mutex};
    Slot& slot = m_slots[static_cast<std::size_t>(m_next % m_slots.size())];
    slot.level = level;
    slot.timeLength = static_cast<std::uint8_t>(copyInto(slot.time, time, slot.time.size()));
    slot.loggerLength = static_cast<std::uint8_t>(copyInto(slot.logger, logger, c_maxLoggerBytes));
    std::size_t length = copyInto(slot.message, message, c_maxMessageBytes);
    if (length < message.size())
    {
        // There's room: the message array is c_maxMessageBytes plus the ellipsis's 3 bytes.
        std::memcpy(slot.message.data() + length, c_ellipsis.data(), c_ellipsis.size());
        length += c_ellipsis.size();
    }
    slot.messageLength = static_cast<std::uint16_t>(length);
    ++m_next;
}

std::uint64_t ConsoleBuffer::linesSince(std::uint64_t sequence, std::vector<LogLine>& lines) const
{
    const std::scoped_lock lock{m_mutex};
    // The oldest line still in the ring; anything before it was overwritten.
    const std::uint64_t oldest = m_next > m_slots.size() ? m_next - m_slots.size() : 0;
    const std::uint64_t first = std::max(sequence, oldest);
    for (std::uint64_t i = first; i < m_next; ++i)
    {
        const Slot& slot = m_slots[static_cast<std::size_t>(i % m_slots.size())];
        lines.push_back({.sequence = i,
                         .time = std::string{slot.time.data(), slot.timeLength},
                         .level = slot.level,
                         .logger = std::string{slot.logger.data(), slot.loggerLength},
                         .message = std::string{slot.message.data(), slot.messageLength}});
    }
    return first - std::min(sequence, first);
}

std::uint64_t ConsoleBuffer::nextSequence() const
{
    const std::scoped_lock lock{m_mutex};
    return m_next;
}
} // namespace Meow2::Log
