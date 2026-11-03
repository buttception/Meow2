#pragma once

/// @file
/// @brief UuidGenerator: makes engine IDs (Uuid.h) from any thread, without locks or coordination.
///
/// @code
/// Meow2::UuidGenerator ids;                                   // composition root; injected where needed
/// const Meow2::EntityId player = ids.next<Meow2::IdKind::Entity>();
/// MEOW2_LOG_INFO(log, "Spawned {}", player.uuid());            // 019a4b1c-7d2e-8001-8040-123456789abc
/// @endcode
/// @see techstack.md §24, docs/wiki/Ids.md

#include <Meow2/Core/Id/Uuid.h>

#include <atomic>
#include <cstdint>

namespace Meow2
{
/// @brief Makes Uuids: current time in milliseconds, a sequence number, the kind, and this generator's node.
///
/// - **Increasing:** every ID is greater than the one before it from the same generator, also when the system clock
///   is set back and when more than 4096 IDs are made in one millisecond (the sequence spills into the next
///   millisecond; the timestamp may then run ahead of the clock by a little).
/// - **Unique across machines** without coordination: the 54-bit node is random (std::random_device) unless given.
/// - **Lock-free:** time and sequence are one `std::atomic<std::uint64_t>`, advanced with compare-and-swap.
/// @par Thread safety
/// Thread-safe: next() may be called from any number of threads at once.
class UuidGenerator
{
public:
    /// @brief Highest node value (54 bits).
    static constexpr std::uint64_t c_maxNode = (std::uint64_t{1} << 54) - 1;

    /// @brief A generator with a random node.
    UuidGenerator();

    /// @brief A generator with a given node, e.g. for reproducible tests.
    /// @param node The node bits; only the low 54 bits are used.
    explicit UuidGenerator(std::uint64_t node);

    UuidGenerator(const UuidGenerator&) = delete;            ///< Not copyable: two copies would repeat IDs.
    UuidGenerator& operator=(const UuidGenerator&) = delete; ///< Not copyable: two copies would repeat IDs.

    /// @brief Makes an ID of `kind` at the current time.
    /// @param kind What it names.
    /// @pre `kind != IdKind::Invalid` (asserted).
    /// @return A new, valid ID.
    [[nodiscard]] Uuid next(IdKind kind);

    /// @brief Makes an ID of `kind` as if the time were `unixMilliseconds` (tests, replays).
    /// @param kind             What it names.
    /// @param unixMilliseconds The time, in milliseconds since 1970; earlier than the last ID's time still gives a
    ///                         greater ID.
    /// @pre `kind != IdKind::Invalid` (asserted).
    /// @return A new, valid ID.
    [[nodiscard]] Uuid next(IdKind kind, std::uint64_t unixMilliseconds);

    /// @brief Makes a typed ID at the current time.
    /// @tparam K What it names.
    /// @return A new ID of kind `K`.
    template <IdKind K>
    [[nodiscard]] TypedId<K> next()
    {
        return TypedId<K>{next(K)};
    }

    /// @brief This generator's node.
    /// @return The 54 node bits every ID it makes carries.
    [[nodiscard]] std::uint64_t node() const;

private:
    std::uint64_t m_node;
    std::atomic<std::uint64_t> m_lastTimeAndSequence{0}; ///< `milliseconds << 12 | sequence` of the last ID.
};
} // namespace Meow2
