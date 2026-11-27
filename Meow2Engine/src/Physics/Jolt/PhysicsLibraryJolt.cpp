/// @file
/// @brief PhysicsLibrary over Jolt: its allocator, trace and assert hooks, its factory and type registry.

#include <Meow2/Core/Assert.h>
#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Physics/PhysicsLibrary.h>

// Jolt.h comes before every other Jolt header (it sets up Jolt's configuration), so it's kept out of the sorted
// block below.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Meow2::Physics
{
std::string_view physicsBackendName() noexcept
{
    return "Jolt";
}

bool physicsAvailable() noexcept
{
    return true;
}

namespace
{
// GLOBAL: Jolt's trace hook is a plain function pointer without a context argument, so the logger it writes to is
// reached through this pointer. Set by the PhysicsLibrary while it lives, null otherwise (techstack.md §18).
std::atomic<Log::Logger*> s_log{nullptr};

// GLOBAL: there is only one Jolt per process (its hooks and type registry are process-wide), so only one
// PhysicsLibrary may exist at a time; this flag checks that.
std::atomic<bool> s_alive{false};

/// @brief Jolt's alignment for plain allocations (`JPH_DEFAULT_ALLOCATE_ALIGNMENT`): what `operator new` gives.
constexpr std::size_t c_defaultAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

// The allocator hooks: every allocation of Jolt's goes to the process-wide tracker under MemTag::Physics
// (techstack.md §6). [[maybe_unused]]: without memory tracking (Release, sanitizer builds) Jolt's default allocator
// is used instead.
[[maybe_unused]] void* joltAllocate(std::size_t size)
{
    return Memory::globalTracker().allocateTagged(size, c_defaultAlignment, Memory::MemTag::Physics);
}

[[maybe_unused]] void joltFree(void* block)
{
    if (block != nullptr)
    {
        Memory::globalTracker().deallocateUnsized(block);
    }
}

[[maybe_unused]] void* joltReallocate(void* block, std::size_t oldSize, std::size_t newSize)
{
    void* moved = joltAllocate(newSize);
    if (block != nullptr)
    {
        std::memcpy(moved, block, std::min(oldSize, newSize));
        joltFree(block);
    }
    return moved;
}

[[maybe_unused]] void* joltAlignedAllocate(std::size_t size, std::size_t alignment)
{
    return Memory::globalTracker().allocateTagged(size, std::max(alignment, c_defaultAlignment),
                                                  Memory::MemTag::Physics);
}

/// @brief Jolt's trace output (warnings about its own state, e.g. a full cache): a debug line in our log.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void joltTrace(const char* format, ...)
{
    std::array<char, 1024> line{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (Log::Logger* log = s_log.load(); log != nullptr)
    {
        MEOW2_LOG_DEBUG(*log, "Jolt: {}", line.data());
    }
}

#ifdef JPH_ENABLE_ASSERTS
/// @brief A failed assert inside Jolt, reported through ours (the handler decides: break, continue or abort).
/// @return True to break into the debugger at Jolt's assert.
bool joltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    return Detail::reportAssertFailure(AssertInfo{.expression = expression,
                                                  .message = message != nullptr ? message : "",
                                                  .file = file,
                                                  .line = static_cast<int>(line),
                                                  .function = "Jolt"});
}
#endif
} // namespace

/// @brief Jolt's factory (it builds shapes and constraints by type), owned here; Jolt reaches it through a static
///        pointer (`JPH::Factory::sInstance`).
struct PhysicsLibrary::Impl
{
    std::unique_ptr<JPH::Factory> factory;
};

PhysicsLibrary::PhysicsLibrary(Log::Logger& log)
    : m_impl(std::make_unique<Impl>())
{
    [[maybe_unused]] const bool wasAlive = s_alive.exchange(true);
    MEOW2_ASSERT(!wasAlive, "only one PhysicsLibrary may exist at a time");
    s_log.store(&log);
    // The hooks first: the factory and the type registry already allocate.
    if constexpr (Memory::c_memoryTrackingAvailable)
    {
        JPH::Allocate = joltAllocate;
        JPH::Reallocate = joltReallocate;
        JPH::Free = joltFree;
        JPH::AlignedAllocate = joltAlignedAllocate;
        JPH::AlignedFree = joltFree;
    }
    else
    {
        JPH::RegisterDefaultAllocator();
    }
    JPH::Trace = joltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = joltAssertFailed;)
    m_impl->factory = std::make_unique<JPH::Factory>();
    JPH::Factory::sInstance = m_impl->factory.get();
    JPH::RegisterTypes();
    MEOW2_LOG_INFO(log, "Physics: Jolt {}.{}.{}", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
}

PhysicsLibrary::~PhysicsLibrary()
{
    JPH::UnregisterTypes();
    JPH::Factory::sInstance = nullptr;
    m_impl->factory.reset();
    s_log.store(nullptr);
    s_alive.store(false);
}
} // namespace Meow2::Physics
