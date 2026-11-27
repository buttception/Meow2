/// @file
/// @brief The `None` physics backend (`MEOW2_PHYSICS_BACKEND=None`): the engine without a physics library.
///
/// PhysicsLibrary does nothing and PhysicsWorld::create() reports ErrorCode::Unsupported, so no PhysicsWorld ever
/// exists in this build; its other member functions are only defined so the program links, and assert if reached.

#include <Meow2/Core/Assert.h>
#include <Meow2/Physics/PhysicsLibrary.h>
#include <Meow2/Physics/PhysicsWorld.h>

namespace Meow2::Physics
{
std::string_view physicsBackendName() noexcept
{
    return "None";
}

bool physicsAvailable() noexcept
{
    return false;
}

/// @brief Nothing to own without a library.
struct PhysicsLibrary::Impl
{
};

PhysicsLibrary::PhysicsLibrary(Log::Logger& /*log*/) {}

PhysicsLibrary::~PhysicsLibrary() = default;

/// @brief Never made: create() fails.
struct PhysicsWorld::Impl
{
};

Result<std::unique_ptr<PhysicsWorld>> PhysicsWorld::create(const PhysicsWorldDeps& /*deps*/,
                                                           const PhysicsWorldDesc& /*desc*/)
{
    return fail(ErrorCode::Unsupported, "this build has no physics (MEOW2_PHYSICS_BACKEND=None)");
}

PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

PhysicsWorld::~PhysicsWorld() = default;

namespace
{
/// @brief Every member below is unreachable: no world can be created.
void unreachable()
{
    MEOW2_ASSERT(false, "no PhysicsWorld exists in a build without physics");
}
} // namespace

Result<BodyId> PhysicsWorld::createBody(const BodyDesc& /*desc*/)
{
    unreachable();
    return fail(ErrorCode::Unsupported);
}

Result<void> PhysicsWorld::destroyBody(BodyId /*body*/)
{
    unreachable();
    return fail(ErrorCode::Unsupported);
}

bool PhysicsWorld::contains(BodyId /*body*/) const
{
    return false;
}

std::uint32_t PhysicsWorld::bodyCount() const
{
    return 0;
}

void PhysicsWorld::step(float /*seconds*/)
{
    unreachable();
}

Math::WorldPosition PhysicsWorld::position(BodyId /*body*/) const
{
    unreachable();
    return {};
}

Math::Quat PhysicsWorld::rotation(BodyId /*body*/) const
{
    unreachable();
    return {};
}

void PhysicsWorld::setTransform(BodyId /*body*/, Math::WorldPosition /*position*/, Math::Quat /*rotation*/)
{
    unreachable();
}

void PhysicsWorld::moveKinematic(BodyId /*body*/, Math::WorldPosition /*position*/, Math::Quat /*rotation*/,
                                 float /*seconds*/)
{
    unreachable();
}

Math::Vec3 PhysicsWorld::linearVelocity(BodyId /*body*/) const
{
    unreachable();
    return {};
}

void PhysicsWorld::setLinearVelocity(BodyId /*body*/, Math::Vec3 /*velocity*/)
{
    unreachable();
}

Math::Vec3 PhysicsWorld::angularVelocity(BodyId /*body*/) const
{
    unreachable();
    return {};
}

void PhysicsWorld::setAngularVelocity(BodyId /*body*/, Math::Vec3 /*velocity*/)
{
    unreachable();
}

void PhysicsWorld::addForce(BodyId /*body*/, Math::Vec3 /*force*/)
{
    unreachable();
}

void PhysicsWorld::addImpulse(BodyId /*body*/, Math::Vec3 /*impulse*/)
{
    unreachable();
}

bool PhysicsWorld::isAwake(BodyId /*body*/) const
{
    unreachable();
    return false;
}

Math::Vec3 PhysicsWorld::gravity() const
{
    unreachable();
    return {};
}

void PhysicsWorld::setGravity(Math::Vec3 /*gravity*/)
{
    unreachable();
}

std::optional<RayHit> PhysicsWorld::castRay(Math::WorldPosition /*origin*/, Math::Vec3 /*translation*/,
                                            const QueryFilter& /*filter*/) const
{
    unreachable();
    return std::nullopt;
}

Result<std::optional<ShapeHit>> PhysicsWorld::castShape(const ShapeDesc& /*shape*/, Math::WorldPosition /*position*/,
                                                        Math::Quat /*rotation*/, Math::Vec3 /*translation*/,
                                                        const QueryFilter& /*filter*/) const
{
    unreachable();
    return fail(ErrorCode::Unsupported);
}

Result<std::vector<BodyId>> PhysicsWorld::overlap(const ShapeDesc& /*shape*/, Math::WorldPosition /*position*/,
                                                  Math::Quat /*rotation*/, const QueryFilter& /*filter*/) const
{
    unreachable();
    return fail(ErrorCode::Unsupported);
}

std::span<const ContactEvent> PhysicsWorld::contactEvents() const
{
    return {};
}

std::span<const SensorEvent> PhysicsWorld::sensorEvents() const
{
    return {};
}

std::uint64_t PhysicsWorld::userData(BodyId /*body*/) const
{
    unreachable();
    return 0;
}
} // namespace Meow2::Physics
