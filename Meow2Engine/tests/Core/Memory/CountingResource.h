#pragma once

/// @file
/// @brief Test helpers shared by the memory tests: a counting upstream resource and an alignment check.

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace Meow2::Tests
{
/// @brief Upstream resource that records every allocation it serves, so tests can see what an allocator asks for.
/// @par Thread safety
/// Not thread-safe.
class CountingResource final : public std::pmr::memory_resource
{
public:
    /// @brief Sizes of all allocations so far, in order.
    std::vector<std::size_t> allocations;
    /// @brief Number of deallocations so far.
    std::size_t deallocations = 0;
    /// @brief Bytes currently allocated and not freed.
    std::size_t outstandingBytes = 0;

    /// @brief Clears the recorded history; outstanding bytes are kept.
    void clearHistory()
    {
        allocations.clear();
        deallocations = 0;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        allocations.push_back(bytes);
        outstandingBytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        ++deallocations;
        outstandingBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

/// @brief Whether @p pointer is a multiple of @p alignment.
inline bool isAligned(const void* pointer, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}
} // namespace Meow2::Tests
