#pragma once

/// @file
/// @brief PhysicsLibrary: owns the physics library's process-wide state, for as long as physics is used.

#include <Meow2/Core/Log/Logger.h>

#include <memory>
#include <string_view>

namespace Meow2::Physics
{
/// @brief The physics library this build was made with: `"Jolt"` or `"None"` (`MEOW2_PHYSICS_BACKEND`).
/// @return Its name.
[[nodiscard]] std::string_view physicsBackendName() noexcept;

/// @brief Whether this build has physics at all (`MEOW2_PHYSICS_BACKEND` isn't `None`).
/// @return False in a build without physics: PhysicsWorld::create() then fails with ErrorCode::Unsupported.
[[nodiscard]] bool physicsAvailable() noexcept;

/// @brief Sets up the physics library's process-wide state while it lives, and takes it down after.
///
/// Physics libraries keep some state for the whole process: Jolt has a type registry and global function pointers
/// for allocation, tracing and asserts. This class is that state's one owner (techstack.md §18, "third-party
/// libraries with process-global state"), created by the composition root before any PhysicsWorld:
/// - **Memory:** with memory tracking (Debug, RelWithDebInfo), every allocation of the library is counted under
///   MemTag::Physics.
/// - **Messages:** the library's trace output goes to the logger as debug lines, and its failed asserts are
///   reported as ours (MEOW2_ASSERT's handler).
///
/// Only one may exist at a time (asserted). In a build without physics it does nothing.
/// @par Thread safety
/// Construct and destroy on the main thread, while no PhysicsWorld exists.
/// @par Lifetime
/// Must outlive every PhysicsWorld. The Logger must outlive it.
class PhysicsLibrary
{
public:
    /// @brief Sets the library up.
    /// @param log Receives its messages.
    explicit PhysicsLibrary(Log::Logger& log);

    /// @brief Takes the library down.
    ~PhysicsLibrary();

    PhysicsLibrary(const PhysicsLibrary&) = delete;            ///< Not copyable: owns process-wide state.
    PhysicsLibrary& operator=(const PhysicsLibrary&) = delete; ///< Not copyable.

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Physics
