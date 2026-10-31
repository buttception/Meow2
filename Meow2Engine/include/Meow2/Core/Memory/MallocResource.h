#pragma once

/// @file
/// @brief MallocResource: the bottom of the engine's allocator stack, straight to `malloc`/`free`.
/// @see techstack.md §6, docs/wiki/Memory.md

#include <cstddef>
#include <memory_resource>

namespace Meow2::Memory
{
/// @brief `std::pmr::memory_resource` over `malloc`/`free`, the upstream of every other engine allocator.
///
/// It calls `malloc` directly, never `operator new`, so it stays usable underneath a global `operator new` override
/// (which would otherwise call itself). Any power-of-two alignment works: alignments up to `alignof(std::max_align_t)`
/// come straight from `malloc`; larger ones over-allocate and keep the original pointer just before the aligned
/// address. Out of memory throws `std::bad_alloc`, as the `memory_resource` contract requires.
/// @par Thread safety
/// Thread-safe (stateless; `malloc` and `free` are thread-safe).
class MallocResource final : public std::pmr::memory_resource
{
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
};
} // namespace Meow2::Memory
