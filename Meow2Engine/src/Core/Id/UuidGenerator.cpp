/// @file
/// @brief UuidGenerator (UuidGenerator.h).

#include "Meow2/Core/Id/UuidGenerator.h"

#include <algorithm>
#include <chrono>
#include <random>

namespace Meow2
{
namespace
{
constexpr std::uint64_t c_sequenceBits = 12;
constexpr std::uint64_t c_version = 8;
constexpr std::uint64_t c_variant = 0b10;
constexpr std::uint64_t c_timeMask = (std::uint64_t{1} << 48) - 1;

std::uint64_t randomNode()
{
    std::random_device device;
    const std::uint64_t value = (std::uint64_t{device()} << 32) ^ std::uint64_t{device()};
    return value & UuidGenerator::c_maxNode;
}

std::uint64_t nowUnixMilliseconds()
{
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
}
} // namespace

UuidGenerator::UuidGenerator()
    : UuidGenerator(randomNode())
{
}

UuidGenerator::UuidGenerator(std::uint64_t node)
    : m_node(node & c_maxNode)
{
}

Uuid UuidGenerator::next(IdKind kind)
{
    return next(kind, nowUnixMilliseconds());
}

Uuid UuidGenerator::next(IdKind kind, std::uint64_t unixMilliseconds)
{
    MEOW2_ASSERT(kind != IdKind::Invalid, "IDs need a kind");

    // Time and sequence as one number, `ms << 12 | seq`. The next value is the larger of "this millisecond, sequence
    // 0" and "the last value + 1": so IDs never go backwards when the clock does, and sequence 4095 + 1 carries into
    // the next millisecond. One compare-and-swap makes it lock-free; on contention another thread won, so retry.
    const std::uint64_t now = (unixMilliseconds & c_timeMask) << c_sequenceBits;
    std::uint64_t last = m_lastTimeAndSequence.load();
    std::uint64_t value = std::max(now, last + 1);
    while (!m_lastTimeAndSequence.compare_exchange_weak(last, value))
    {
        value = std::max(now, last + 1); // `last` now holds the other thread's value
    }

    const std::uint64_t milliseconds = (value >> c_sequenceBits) & c_timeMask;
    const std::uint64_t sequence = value & ((std::uint64_t{1} << c_sequenceBits) - 1);
    const std::uint64_t high = (milliseconds << 16) | (c_version << 12) | sequence;
    const std::uint64_t low = (c_variant << 62) | (std::uint64_t{static_cast<std::uint8_t>(kind)} << 54) | m_node;
    return Uuid::fromParts(high, low);
}

std::uint64_t UuidGenerator::node() const
{
    return m_node;
}
} // namespace Meow2
