/// @file
/// @brief The process-wide tracker behind global `operator new`/`delete` (GlobalTracking.h).

#include "Meow2/Core/Memory/GlobalTracking.h"

#include "Meow2/Core/Memory/MallocResource.h"

#include <utility>

namespace Meow2::Memory
{
namespace
{
/// @brief Holds a T that is constructed at compile time (constinit) and never destroyed.
///
/// A union member isn't destroyed automatically, and the destructor below is empty, so `value` stays usable until
/// the process ends, even inside other objects' static destructors.
template <typename T>
union NeverDestroyed
{
    T value; ///< The object.

    /// @brief Constructs the object in place.
    /// @param args Constructor arguments of T.
    template <typename... Args>
    constexpr explicit NeverDestroyed(Args&&... args)
        : value(std::forward<Args>(args)...)
    {
    }

    ~NeverDestroyed() {} // intentionally empty: value is never destroyed
};

// GLOBAL: operator new has no parameters to receive an allocator, and memory it hands out may be freed after main
// returns (static destructors), so the tracker must exist before the first and after the last allocation. constinit
// makes both constant-initialised (no static initialisation order problem); NeverDestroyed keeps them alive to the end.
// techstack.md §18 lists memory tracking as an exception.
constinit NeverDestroyed<MallocResource> s_mallocResource{};
constinit NeverDestroyed<TrackingAllocator> s_globalTracker{s_mallocResource.value};
} // namespace

TrackingAllocator& globalTracker()
{
    return s_globalTracker.value;
}

namespace Detail
{
void* globalNew(std::size_t bytes, std::size_t alignment)
{
    return s_globalTracker.value.allocateTagged(bytes, alignment, currentMemTag());
}

void globalDelete(void* pointer) noexcept
{
    if (pointer != nullptr)
    {
        s_globalTracker.value.deallocateUnsized(pointer);
    }
}
} // namespace Detail
} // namespace Meow2::Memory
