#pragma once

/// @file
/// @brief PhysicsWorld: rigid bodies, stepped at a fixed rate, with queries and contact events.

#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Physics/PhysicsLibrary.h>
#include <Meow2/Physics/PhysicsTypes.h>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Meow2::Physics
{
/// @brief What a PhysicsWorld uses. Plain data (references); everything must outlive the world.
struct PhysicsWorldDeps
{
    PhysicsLibrary& library; ///< The library's process-wide state.
    Jobs::JobSystem& jobs;   ///< Runs the step's work on all cores.
    Log::Logger& log;        ///< Receives warnings (a step that ran out of room for pairs or contacts).
};

/// @brief A world of rigid bodies (techstack.md §14): create bodies, step it at a fixed rate, ask it where things
///        are, cast rays and shapes into it, and read what touched what.
///
/// @code
/// auto world = Physics::PhysicsWorld::create({.library = physics, .jobs = jobs, .log = log}, {});
/// const Physics::BodyId floor = *(*world)->createBody(
///     {.shape = Physics::BoxShape{{50.0F, 0.5F, 50.0F}}, .position = {0, -0.5F, 0}, .motion = MotionType::Static});
/// const Physics::BodyId crate = *(*world)->createBody({.shape = Physics::BoxShape{}, .position = {0, 5, 0}});
/// (*world)->step(1.0F / 60.0F);                  // in fixedUpdate
/// const Math::WorldPosition where = (*world)->position(crate);
/// @endcode
///
/// - **The library** is chosen at build time (`MEOW2_PHYSICS_BACKEND`: Jolt, or None for a build without physics),
///   behind this one class (pimpl, no virtuals; techstack.md §3). Every backend passes the same contract tests
///   (`Physics.PhysicsWorldTest.*`).
/// - **Bodies** are named by BodyId. A destroyed body's id is refused afterwards (contains() is false).
/// - **Stepping:** step() advances the simulation by a fixed time, using the JobSystem's threads; call it from
///   fixedUpdate (techstack.md §23). Forces added with addForce() act during the next step only.
/// - **Events:** contactEvents() and sensorEvents() list what started or stopped touching during the last step,
///   one Begin and one End per pair of bodies (however many parts of them touch), sorted by body ids, so the
///   order is the same on every run. Destroying a body ends its touches at once (its End events come with the next
///   step).
/// - **Queries** (castRay(), castShape(), overlap()) see the bodies as of the last step or change.
/// - **Determinism:** the same bodies, created in the same order, stepped with the same times give the same results,
///   whatever the number of threads.
/// @par Thread safety
/// Not thread-safe: one thread at a time (the main thread, in fixedUpdate). step() itself runs jobs on the
/// JobSystem and returns when they're done.
/// @par Lifetime
/// Everything in PhysicsWorldDeps must outlive it.
class PhysicsWorld
{
public:
    /// @brief Creates a world.
    /// @param deps What it uses.
    /// @param desc How it's set up.
    /// @return The world, or ErrorCode::Unsupported in a build without physics, ErrorCode::InvalidArgument for a bad
    ///         desc (no layers, too many).
    [[nodiscard]] static Result<std::unique_ptr<PhysicsWorld>> create(const PhysicsWorldDeps& deps,
                                                                      const PhysicsWorldDesc& desc);

    /// @brief Destroys every body and the world.
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;            ///< Not copyable: owns the simulation.
    PhysicsWorld& operator=(const PhysicsWorld&) = delete; ///< Not copyable.

    /// @brief Creates a body and adds it to the world.
    /// @param desc The body.
    /// @return Its id; ErrorCode::InvalidArgument for a bad shape (a size not above 0, a hull of fewer than 4 points
    ///         or all in a plane, a mesh without triangles or with an index out of range, a mesh on a dynamic body,
    ///         an empty compound), a layer outside the world's, or a negative density or damping;
    ///         ErrorCode::OutOfMemory when the world holds PhysicsWorldDesc::maxBodies.
    [[nodiscard]] Result<BodyId> createBody(const BodyDesc& desc);

    /// @brief Removes a body and destroys it. Its touches end (End events in the next step).
    /// @param body The body.
    /// @return Nothing, or ErrorCode::NotFound if it isn't in this world (never was, or already destroyed).
    Result<void> destroyBody(BodyId body);

    /// @brief Whether a body is in this world.
    /// @param body The id.
    /// @return False for a destroyed body or an empty id. (Ids aren't checked against the world that made them.)
    [[nodiscard]] bool contains(BodyId body) const;

    /// @brief How many bodies it holds.
    /// @return The count.
    [[nodiscard]] std::uint32_t bodyCount() const;

    /// @brief Advances the simulation.
    /// @param seconds The time step, > 0; normally the fixed step (1/60 s). Longer steps are split into collision
    ///                steps of at most 1/60 s.
    void step(float seconds);

    /// @brief Where a body's origin is.
    /// @param body The body.
    /// @return Its position.
    /// @pre contains(body) (asserted).
    [[nodiscard]] Math::WorldPosition position(BodyId body) const;

    /// @brief How a body is turned.
    /// @param body The body.
    /// @return Its rotation.
    /// @pre contains(body) (asserted).
    [[nodiscard]] Math::Quat rotation(BodyId body) const;

    /// @brief Moves a body there at once (a teleport, not a movement: it passes through what's between). Wakes it.
    /// @param body     The body.
    /// @param position Its new position.
    /// @param rotation Its new rotation.
    /// @pre contains(body) (asserted).
    void setTransform(BodyId body, Math::WorldPosition position, Math::Quat rotation);

    /// @brief Moves a kinematic body to a target over the next step, giving it the velocity that gets it there, so
    ///        it pushes what's in the way.
    /// @param body     A kinematic body.
    /// @param position Where it should be after `seconds`.
    /// @param rotation How it should be turned then.
    /// @param seconds  The next step's length, > 0.
    /// @pre contains(body), and it's kinematic (asserted).
    void moveKinematic(BodyId body, Math::WorldPosition position, Math::Quat rotation, float seconds);

    /// @brief A body's velocity.
    /// @param body The body.
    /// @return m/s.
    /// @pre contains(body) (asserted).
    [[nodiscard]] Math::Vec3 linearVelocity(BodyId body) const;

    /// @brief Sets a dynamic or kinematic body's velocity, and wakes it.
    /// @param body     The body.
    /// @param velocity m/s.
    /// @pre contains(body) (asserted).
    void setLinearVelocity(BodyId body, Math::Vec3 velocity);

    /// @brief A body's spin.
    /// @param body The body.
    /// @return rad/s around world axes.
    /// @pre contains(body) (asserted).
    [[nodiscard]] Math::Vec3 angularVelocity(BodyId body) const;

    /// @brief Sets a dynamic or kinematic body's spin, and wakes it.
    /// @param body     The body.
    /// @param velocity rad/s around world axes.
    /// @pre contains(body) (asserted).
    void setAngularVelocity(BodyId body, Math::Vec3 velocity);

    /// @brief Pushes a dynamic body during the next step (at its centre of mass), and wakes it.
    /// @param body  The body.
    /// @param force Newtons.
    /// @pre contains(body) (asserted).
    void addForce(BodyId body, Math::Vec3 force);

    /// @brief Changes a dynamic body's velocity at once, as a hit would (at its centre of mass), and wakes it.
    /// @param body    The body.
    /// @param impulse kg·m/s.
    /// @pre contains(body) (asserted).
    void addImpulse(BodyId body, Math::Vec3 impulse);

    /// @brief Whether a body is being simulated (not asleep). Static bodies never are.
    /// @param body The body.
    /// @return True while awake.
    /// @pre contains(body) (asserted).
    [[nodiscard]] bool isAwake(BodyId body) const;

    /// @brief The world's gravity.
    /// @return m/s².
    [[nodiscard]] Math::Vec3 gravity() const;

    /// @brief Changes the world's gravity. Sleeping bodies stay asleep until something wakes them.
    /// @param gravity m/s².
    void setGravity(Math::Vec3 gravity);

    /// @brief The first body a ray hits.
    /// @param origin      Where the ray starts.
    /// @param translation Its direction times its length: it ends at `origin + translation`.
    /// @param filter      What it can hit.
    /// @return The nearest hit, or nothing.
    [[nodiscard]] std::optional<RayHit> castRay(Math::WorldPosition origin, Math::Vec3 translation,
                                                const QueryFilter& filter = {}) const;

    /// @brief The first body a convex shape touches when swept along a straight line, e.g. a character's capsule.
    /// @param shape       A sphere, box, capsule or convex hull.
    /// @param position    Where its origin starts.
    /// @param rotation    How it's turned (it doesn't turn while it moves).
    /// @param translation The sweep.
    /// @param filter      What it can hit.
    /// @return The first hit, or nothing; ErrorCode::InvalidArgument for a bad or non-convex shape.
    [[nodiscard]] Result<std::optional<ShapeHit>> castShape(const ShapeDesc& shape, Math::WorldPosition position,
                                                            Math::Quat rotation, Math::Vec3 translation,
                                                            const QueryFilter& filter = {}) const;

    /// @brief Every body a shape overlaps where it stands.
    /// @param shape    A sphere, box, capsule or convex hull.
    /// @param position Where its origin is.
    /// @param rotation How it's turned.
    /// @param filter   What it can find.
    /// @return The bodies, each once, sorted by id; ErrorCode::InvalidArgument for a bad or non-convex shape.
    [[nodiscard]] Result<std::vector<BodyId>> overlap(const ShapeDesc& shape, Math::WorldPosition position,
                                                      Math::Quat rotation, const QueryFilter& filter = {}) const;

    /// @brief What started or stopped touching during the last step (non-sensor bodies).
    /// @return The events, valid until the next step().
    [[nodiscard]] std::span<const ContactEvent> contactEvents() const;

    /// @brief What entered or left a sensor during the last step.
    /// @return The events, valid until the next step().
    [[nodiscard]] std::span<const SensorEvent> sensorEvents() const;

    /// @brief The BodyDesc::userData a body was created with.
    /// @param body The body.
    /// @return Its user data.
    /// @pre contains(body) (asserted).
    [[nodiscard]] std::uint64_t userData(BodyId body) const;

    /// @brief The backend's state.
    struct Impl;

    /// @brief Use create(); Impl is the backend's, so nothing else can make one.
    /// @param impl The backend's state.
    explicit PhysicsWorld(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> m_impl;
};
} // namespace Meow2::Physics
