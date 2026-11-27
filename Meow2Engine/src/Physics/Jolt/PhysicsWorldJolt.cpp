/// @file
/// @brief PhysicsWorld over Jolt: bodies, shapes, layers, stepping on our job system, queries and contact events.

#include "Physics/Jolt/JoltJobSystem.h"
#include "Physics/PhysicsValidation.h"

#include <Meow2/Core/Assert.h>
#include <Meow2/Physics/PhysicsWorld.h>

// Jolt.h comes before every other Jolt header (it sets up Jolt's configuration), so it's kept out of the sorted
// block below.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Meow2::Physics
{
namespace
{
// ---- Conversions ---------------------------------------------------------------------------------------------------
// Jolt's math and ours agree on the axes (Jolt doesn't fix a handedness) and on triangle fronts: our clockwise-front
// triangles in a left-handed space have the face normal (v1 - v0) × (v2 - v0) that Jolt calls the front. So values
// pass through unchanged.

JPH::Vec3 toJolt(const Math::Vec3& value)
{
    return {value.x, value.y, value.z};
}

JPH::Quat toJolt(const Math::Quat& value)
{
    return JPH::Quat(value.x, value.y, value.z, value.w).Normalized();
}

Math::Vec3 fromJolt(JPH::Vec3Arg value)
{
    return {value.GetX(), value.GetY(), value.GetZ()};
}

Math::Quat fromJolt(JPH::QuatArg value)
{
    return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
}

/// @brief Our id for a Jolt body: its index-and-sequence number plus one, so 0 stays "no body".
BodyId toBodyId(const JPH::BodyID& id)
{
    return BodyId{static_cast<std::uint64_t>(id.GetIndexAndSequenceNumber()) + 1};
}

/// @brief Jolt's id for ours; an invalid one for an empty or out-of-range id.
JPH::BodyID toJolt(BodyId id)
{
    if (id.value == 0 || id.value > JPH::BodyID::cInvalidBodyID)
    {
        return {};
    }
    return JPH::BodyID(static_cast<JPH::uint32>(id.value - 1));
}

// ---- Layers --------------------------------------------------------------------------------------------------------
// Jolt sorts bodies into broad-phase layers (separate trees) and object layers (what collides with what). Our
// collision layers become object layers twice over: once for bodies that never move and once for those that do
// (objectLayer = layer * 2 + moving). The broad phase then has two trees, static and moving, so static bodies are
// never tested against each other, while our masks decide the rest.

constexpr JPH::BroadPhaseLayer c_staticTree{0};
constexpr JPH::BroadPhaseLayer c_movingTree{1};

JPH::ObjectLayer objectLayer(CollisionLayer layer, MotionType motion)
{
    return static_cast<JPH::ObjectLayer>(layer * 2U + (motion == MotionType::Static ? 0U : 1U));
}

std::uint32_t collisionLayer(JPH::ObjectLayer layer)
{
    return layer / 2U;
}

bool moves(JPH::ObjectLayer layer)
{
    return (layer & 1U) != 0;
}

/// @brief Which broad-phase tree an object layer's bodies live in.
class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return 2;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return moves(layer) ? c_movingTree : c_staticTree;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == c_movingTree ? "Moving" : "Static";
    }
#endif
};

/// @brief Whether a body can meet a broad-phase tree at all: static bodies only look in the moving tree.
class LayerVsTree final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer tree) const override
    {
        return moves(layer) || tree == c_movingTree;
    }
};

/// @brief Whether two object layers collide: not both static, and allowed by the world's masks.
class LayerPairs final : public JPH::ObjectLayerPairFilter
{
public:
    explicit LayerPairs(const PhysicsWorldDesc& desc)
        : m_desc(desc)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return (moves(a) || moves(b)) && layersCollide(m_desc, collisionLayer(a), collisionLayer(b));
    }

private:
    const PhysicsWorldDesc& m_desc;
};

/// @brief A query's layer mask.
class QueryLayers final : public JPH::ObjectLayerFilter
{
public:
    explicit QueryLayers(std::uint32_t mask)
        : m_mask(mask)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return ((m_mask >> collisionLayer(layer)) & 1U) != 0;
    }

private:
    std::uint32_t m_mask;
};

/// @brief A query's ignored body and whether it sees sensors.
class QueryBodies final : public JPH::BodyFilter
{
public:
    explicit QueryBodies(const QueryFilter& filter)
        : m_ignore(toJolt(filter.ignore))
        , m_sensors(filter.sensors)
    {
    }

    bool ShouldCollide(const JPH::BodyID& body) const override
    {
        return body != m_ignore;
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override
    {
        return m_sensors || !body.IsSensor();
    }

private:
    JPH::BodyID m_ignore;
    bool m_sensors;
};

// ---- Shapes --------------------------------------------------------------------------------------------------------

Unexpected<Error> shapeError(const JPH::Shape::ShapeResult& result)
{
    return fail(ErrorCode::InvalidArgument, std::format("the shape can't be built: {}", result.GetError().c_str()));
}

/// @brief Builds a convex shape. The settings live on the stack: Create() keeps the shape, not them.
Result<JPH::RefConst<JPH::Shape>> makeConvex(const CompoundPartShape& shape, float density)
{
    return std::visit(
        [density](const auto& convex) -> Result<JPH::RefConst<JPH::Shape>>
        {
            using Shape = std::decay_t<decltype(convex)>;
            JPH::Shape::ShapeResult result;
            if constexpr (std::is_same_v<Shape, SphereShape>)
            {
                JPH::SphereShapeSettings settings(convex.radius);
                settings.mDensity = density;
                result = settings.Create();
            }
            else if constexpr (std::is_same_v<Shape, BoxShape>)
            {
                // Jolt rounds a box's edges by a convex radius, which may not exceed its smallest half extent.
                const float smallest = std::min({convex.halfExtents.x, convex.halfExtents.y, convex.halfExtents.z});
                JPH::BoxShapeSettings settings(toJolt(convex.halfExtents),
                                               std::min(JPH::cDefaultConvexRadius, smallest));
                settings.mDensity = density;
                result = settings.Create();
            }
            else if constexpr (std::is_same_v<Shape, CapsuleShape>)
            {
                JPH::CapsuleShapeSettings settings(convex.halfHeight, convex.radius);
                settings.mDensity = density;
                result = settings.Create();
            }
            else
            {
                JPH::Array<JPH::Vec3> points;
                points.reserve(convex.points.size());
                for (const Math::Vec3& point : convex.points)
                {
                    points.push_back(toJolt(point));
                }
                JPH::ConvexHullShapeSettings settings(points);
                settings.mDensity = density;
                result = settings.Create();
            }
            if (result.HasError())
            {
                return shapeError(result);
            }
            return JPH::RefConst<JPH::Shape>(result.Get());
        },
        shape);
}

/// @brief Builds any shape (checked by validateShape() first).
Result<JPH::RefConst<JPH::Shape>> makeShape(const ShapeDesc& shape, float density)
{
    if (const auto* mesh = std::get_if<MeshShape>(&shape))
    {
        JPH::VertexList vertices;
        vertices.reserve(mesh->vertices.size());
        for (const Math::Vec3& vertex : mesh->vertices)
        {
            vertices.push_back(JPH::Float3(vertex.x, vertex.y, vertex.z));
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(mesh->indices.size() / 3);
        for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3)
        {
            triangles.push_back(JPH::IndexedTriangle(mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2]));
        }
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        const JPH::Shape::ShapeResult result = settings.Create();
        if (result.HasError())
        {
            return shapeError(result);
        }
        return JPH::RefConst<JPH::Shape>(result.Get());
    }
    if (const auto* compound = std::get_if<CompoundShape>(&shape))
    {
        JPH::StaticCompoundShapeSettings settings;
        for (const CompoundPart& part : compound->parts)
        {
            Result<JPH::RefConst<JPH::Shape>> child = makeConvex(part.shape, density);
            if (!child)
            {
                return Unexpected<Error>{child.error()};
            }
            settings.AddShape(toJolt(part.position), toJolt(part.rotation), *child);
        }
        const JPH::Shape::ShapeResult result = settings.Create();
        if (result.HasError())
        {
            return shapeError(result);
        }
        return JPH::RefConst<JPH::Shape>(result.Get());
    }
    return std::visit(
        [density](const auto& convex) -> Result<JPH::RefConst<JPH::Shape>>
        {
            using Shape = std::decay_t<decltype(convex)>;
            if constexpr (std::is_same_v<Shape, MeshShape> || std::is_same_v<Shape, CompoundShape>)
            {
                return fail(ErrorCode::InvalidArgument); // handled above
            }
            else
            {
                return makeConvex(CompoundPartShape{convex}, density);
            }
        },
        shape);
}

// ---- Contact events ------------------------------------------------------------------------------------------------

/// @brief One callback from Jolt's contact listener: a pair of sub-shapes started or stopped touching. Plain data.
struct RawContact
{
    bool added = false;    ///< Started (OnContactAdded) or stopped (OnContactRemoved).
    JPH::BodyID body1;     ///< Jolt's first body.
    JPH::BodyID body2;     ///< Its second.
    JPH::uint32 part1 = 0; ///< Sub-shape of the first (a compound's part, a mesh's triangle).
    JPH::uint32 part2 = 0; ///< Sub-shape of the second.
    bool sensor1 = false;  ///< Added: the first body is a sensor.
    bool sensor2 = false;  ///< Added: the second is.
    JPH::Vec3 point;       ///< Added: a contact point.
    JPH::Vec3 normal;      ///< Added: the normal from body 1 towards body 2.
};

/// @brief Collects Jolt's contact callbacks. They come from job threads during the step, so behind a mutex; the
///        world turns them into events after the step, on the stepping thread.
class ContactCollector final : public JPH::ContactListener
{
public:
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& /*settings*/) override
    {
        RawContact contact{.added = true,
                           .body1 = body1.GetID(),
                           .body2 = body2.GetID(),
                           .part1 = manifold.mSubShapeID1.GetValue(),
                           .part2 = manifold.mSubShapeID2.GetValue(),
                           .sensor1 = body1.IsSensor(),
                           .sensor2 = body2.IsSensor(),
                           .point = manifold.mRelativeContactPointsOn1.empty()
                                        ? JPH::Vec3(manifold.mBaseOffset)
                                        : JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(0)),
                           .normal = manifold.mWorldSpaceNormal};
        const std::scoped_lock lock(m_mutex);
        m_contacts.push_back(contact);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        const RawContact contact{.added = false,
                                 .body1 = pair.GetBody1ID(),
                                 .body2 = pair.GetBody2ID(),
                                 .part1 = pair.GetSubShapeID1().GetValue(),
                                 .part2 = pair.GetSubShapeID2().GetValue(),
                                 .sensor1 = false,
                                 .sensor2 = false,
                                 .point = JPH::Vec3::sZero(),
                                 .normal = JPH::Vec3::sZero()};
        const std::scoped_lock lock(m_mutex);
        m_contacts.push_back(contact);
    }

    /// @brief This step's callbacks; the collector starts over.
    std::vector<RawContact> take()
    {
        std::vector<RawContact> taken;
        const std::scoped_lock lock(m_mutex);
        taken.swap(m_contacts);
        return taken;
    }

private:
    std::mutex m_mutex;
    std::vector<RawContact> m_contacts;
};

/// @brief A pair of bodies that touch: how many of their sub-shape pairs touch, and whether one is a sensor. Plain
///        data.
struct Touch
{
    std::set<std::uint64_t> parts{}; ///< Sub-shape pairs touching (first part << 32 | second); ends when empty.
    BodyId sensor{};                 ///< The sensor of the pair, if one is.
    /// Jolt dropped the contact because the bodies fell asleep, not because they parted: the pair still touches,
    /// until a body wakes and Jolt no longer finds them in contact.
    bool dormant = false;
};

/// @brief A body pair's key: the smaller Jolt id first.
using PairKey = std::pair<JPH::uint32, JPH::uint32>;

PairKey pairKey(const JPH::BodyID& a, const JPH::BodyID& b)
{
    const JPH::uint32 first = a.GetIndexAndSequenceNumber();
    const JPH::uint32 second = b.GetIndexAndSequenceNumber();
    return first < second ? PairKey{first, second} : PairKey{second, first};
}
} // namespace

/// @brief Everything the Jolt world holds. Members are declared in dependency order: Jolt's PhysicsSystem keeps
///        pointers to the layer filters, the job system and the listener, so it's declared last and destroyed first.
struct PhysicsWorld::Impl
{
    Impl(const PhysicsWorldDeps& worldDeps, const PhysicsWorldDesc& worldDesc)
        : deps(worldDeps)
        , desc(worldDesc)
        , layerPairs(desc)
        , temp(worldDesc.tempMemoryBytes)
        , jobs(worldDeps.jobs)
    {
    }

    /// @brief Turns this step's contact callbacks into events (and the ends of destroyed bodies' touches).
    void collectEvents();

    /// @brief Adds the event for a pair that started or stopped touching.
    void addEvent(EventType type, const PairKey& key, const Touch& touch, const RawContact* contact);

    PhysicsWorldDeps deps;
    PhysicsWorldDesc desc;
    BroadPhaseLayers broadPhaseLayers;
    LayerVsTree layerVsTree;
    LayerPairs layerPairs;
    JPH::TempAllocatorImpl temp; ///< The step's scratch memory, taken once (under MemTag::Physics).
    JoltJobSystem jobs;
    ContactCollector contacts;
    std::map<PairKey, Touch> touching;            ///< Pairs that touch now.
    std::vector<ContactEvent> contactEvents;      ///< The last step's.
    std::vector<SensorEvent> sensorEvents;        ///< The last step's.
    std::vector<ContactEvent> pendingContactEnds; ///< Ends from destroyBody(), delivered with the next step.
    std::vector<SensorEvent> pendingSensorEnds;
    std::uint32_t addedSinceOptimize = 0; ///< Bodies added since the broad phase was last rebuilt.
    bool warnedFull = false;              ///< A full pair or contact buffer was logged (once).
    JPH::PhysicsSystem system;
};

void PhysicsWorld::Impl::addEvent(EventType type, const PairKey& key, const Touch& touch, const RawContact* contact)
{
    const BodyId first = toBodyId(JPH::BodyID(key.first));
    const BodyId second = toBodyId(JPH::BodyID(key.second));
    if (isSet(touch.sensor))
    {
        sensorEvents.push_back(
            SensorEvent{.type = type, .sensor = touch.sensor, .other = touch.sensor == first ? second : first});
        return;
    }
    ContactEvent event{.type = type, .bodyA = first, .bodyB = second, .point = {}, .normal = {}};
    if (contact != nullptr)
    {
        // Jolt's normal points from its body 1 to body 2; ours from A (the smaller id) to B.
        const bool flipped = contact->body1.GetIndexAndSequenceNumber() != key.first;
        event.point = fromJolt(contact->point);
        event.normal = fromJolt(flipped ? -contact->normal : contact->normal);
    }
    contactEvents.push_back(event);
}

void PhysicsWorld::Impl::collectEvents()
{
    contactEvents.clear();
    sensorEvents.clear();
    contactEvents.swap(pendingContactEnds);
    sensorEvents.swap(pendingSensorEnds);

    // Callbacks arrive from several threads in no fixed order: sorted first, so the events (and which contact point a
    // Begin reports) are the same on every run. Additions before removals: a pair that swaps which parts touch
    // within one step keeps touching instead of ending and beginning again.
    std::vector<RawContact> raw = contacts.take();
    std::ranges::sort(raw,
                      [](const RawContact& a, const RawContact& b)
                      {
                          return std::tuple(!a.added, a.body1.GetIndexAndSequenceNumber(),
                                            a.body2.GetIndexAndSequenceNumber(), a.part1, a.part2) <
                                 std::tuple(!b.added, b.body1.GetIndexAndSequenceNumber(),
                                            b.body2.GetIndexAndSequenceNumber(), b.part1, b.part2);
                      });
    for (const RawContact& contact : raw)
    {
        const PairKey key = pairKey(contact.body1, contact.body2);
        const std::uint64_t part = (std::uint64_t{contact.part1} << 32U) | contact.part2;
        if (contact.added)
        {
            const auto [it, isNew] = touching.try_emplace(key);
            Touch& touch = it->second;
            if (touch.dormant)
            {
                // Awake again and touching: the parts are found afresh (the old ones may not come back).
                touch.parts.clear();
                touch.dormant = false;
            }
            touch.parts.insert(part);
            if (isNew)
            {
                if (contact.sensor1 || contact.sensor2)
                {
                    touch.sensor = toBodyId(contact.sensor1 ? contact.body1 : contact.body2);
                }
                addEvent(EventType::Begin, key, touch, &contact);
            }
            continue;
        }
        const auto found = touching.find(key);
        if (found == touching.end())
        {
            continue; // a destroyed body's, already ended by destroyBody()
        }
        // Jolt removes the contacts of bodies that fall asleep, though they still touch. Asleep means neither body is
        // active now (a static one never is): the pair stays, dormant.
        JPH::BodyInterface& bodyInterface = system.GetBodyInterface();
        if (!bodyInterface.IsActive(contact.body1) && !bodyInterface.IsActive(contact.body2))
        {
            found->second.dormant = true;
            continue;
        }
        found->second.parts.erase(part);
        if (found->second.parts.empty())
        {
            addEvent(EventType::End, key, found->second, nullptr);
            touching.erase(found);
        }
    }
    // A dormant pair whose body woke without Jolt finding the contact again has parted (it was moved, or the other
    // body left): it ends now.
    for (auto it = touching.begin(); it != touching.end();)
    {
        const JPH::BodyID first(it->first.first);
        const JPH::BodyID second(it->first.second);
        JPH::BodyInterface& bodyInterface = system.GetBodyInterface();
        if (it->second.dormant && (bodyInterface.IsActive(first) || bodyInterface.IsActive(second)) &&
            !system.WereBodiesInContact(first, second))
        {
            addEvent(EventType::End, it->first, it->second, nullptr);
            it = touching.erase(it);
            continue;
        }
        ++it;
    }
    std::ranges::sort(contactEvents,
                      [](const ContactEvent& a, const ContactEvent& b)
                      {
                          return std::tuple(a.bodyA.value, a.bodyB.value, a.type) <
                                 std::tuple(b.bodyA.value, b.bodyB.value, b.type);
                      });
    std::ranges::sort(sensorEvents,
                      [](const SensorEvent& a, const SensorEvent& b)
                      {
                          return std::tuple(a.sensor.value, a.other.value, a.type) <
                                 std::tuple(b.sensor.value, b.other.value, b.type);
                      });
}

Result<std::unique_ptr<PhysicsWorld>> PhysicsWorld::create(const PhysicsWorldDeps& deps, const PhysicsWorldDesc& desc)
{
    if (Result<void> valid = validateWorldDesc(desc); !valid)
    {
        return Unexpected<Error>{valid.error()};
    }
    auto impl = std::make_unique<Impl>(deps, desc);
    impl->system.Init(desc.maxBodies, 0, desc.maxBodyPairs, desc.maxContacts, impl->broadPhaseLayers, impl->layerVsTree,
                      impl->layerPairs);
    impl->system.SetGravity(toJolt(desc.gravity));
    impl->system.SetContactListener(&impl->contacts);
    return std::make_unique<PhysicsWorld>(std::move(impl));
}

PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

PhysicsWorld::~PhysicsWorld()
{
    if (!m_impl)
    {
        return;
    }
    // Remove and destroy every body before the system goes (Jolt asserts that none are left in the broad phase).
    JPH::BodyIDVector bodies;
    m_impl->system.GetBodies(bodies);
    JPH::BodyInterface& bodyInterface = m_impl->system.GetBodyInterface();
    if (!bodies.empty())
    {
        bodyInterface.RemoveBodies(bodies.data(), static_cast<int>(bodies.size()));
        bodyInterface.DestroyBodies(bodies.data(), static_cast<int>(bodies.size()));
    }
}

Result<BodyId> PhysicsWorld::createBody(const BodyDesc& desc)
{
    if (Result<void> valid = validateBodyDesc(desc, m_impl->desc.layerCount); !valid)
    {
        return Unexpected<Error>{valid.error()};
    }
    Result<JPH::RefConst<JPH::Shape>> shape = makeShape(desc.shape, desc.density);
    if (!shape)
    {
        return Unexpected<Error>{shape.error()};
    }
    const JPH::EMotionType motion = desc.motion == MotionType::Static      ? JPH::EMotionType::Static
                                    : desc.motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                                           : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings settings(*shape, toJolt(desc.position), toJolt(desc.rotation), motion,
                                       objectLayer(desc.layer, desc.motion));
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mLinearDamping = desc.linearDamping;
    settings.mAngularDamping = desc.angularDamping;
    settings.mGravityFactor = desc.gravityScale;
    settings.mIsSensor = desc.sensor;
    settings.mAllowSleeping = desc.allowSleep;
    settings.mMotionQuality = desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    if (desc.motion == MotionType::Dynamic && desc.mass > 0.0F)
    {
        // The given mass, with the inertia the shape would have at that mass (its density only sets the proportions).
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }
    settings.mUserData = desc.userData;
    if (desc.motion != MotionType::Static)
    {
        settings.mLinearVelocity = toJolt(desc.linearVelocity);
        settings.mAngularVelocity = toJolt(desc.angularVelocity);
    }
    const JPH::BodyID id = m_impl->system.GetBodyInterface().CreateAndAddBody(
        settings, desc.motion == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (id.IsInvalid())
    {
        return fail(ErrorCode::OutOfMemory,
                    std::format("the world is full ({} bodies, PhysicsWorldDesc::maxBodies)", m_impl->desc.maxBodies));
    }
    ++m_impl->addedSinceOptimize;
    return toBodyId(id);
}

Result<void> PhysicsWorld::destroyBody(BodyId body)
{
    if (!contains(body))
    {
        return fail(ErrorCode::NotFound, "no such body in this world");
    }
    const JPH::BodyID id = toJolt(body);
    // Its touches end now: Jolt would report them only after the next step, and only by id.
    const JPH::uint32 raw = id.GetIndexAndSequenceNumber();
    for (auto it = m_impl->touching.begin(); it != m_impl->touching.end();)
    {
        if (it->first.first != raw && it->first.second != raw)
        {
            ++it;
            continue;
        }
        const BodyId first = toBodyId(JPH::BodyID(it->first.first));
        const BodyId second = toBodyId(JPH::BodyID(it->first.second));
        if (isSet(it->second.sensor))
        {
            m_impl->pendingSensorEnds.push_back(SensorEvent{.type = EventType::End,
                                                            .sensor = it->second.sensor,
                                                            .other = it->second.sensor == first ? second : first});
        }
        else
        {
            m_impl->pendingContactEnds.push_back(
                ContactEvent{.type = EventType::End, .bodyA = first, .bodyB = second, .point = {}, .normal = {}});
        }
        it = m_impl->touching.erase(it);
    }
    JPH::BodyInterface& bodyInterface = m_impl->system.GetBodyInterface();
    bodyInterface.RemoveBody(id);
    bodyInterface.DestroyBody(id);
    return {};
}

bool PhysicsWorld::contains(BodyId body) const
{
    const JPH::BodyID id = toJolt(body);
    return !id.IsInvalid() && m_impl->system.GetBodyInterface().IsAdded(id);
}

std::uint32_t PhysicsWorld::bodyCount() const
{
    return m_impl->system.GetNumBodies();
}

void PhysicsWorld::step(float seconds)
{
    MEOW2_ASSERT(seconds > 0.0F, "a step needs a time above 0");
    // Jolt's broad phase answers best when rebuilt after many bodies were added (a level load); not every step.
    if (m_impl->addedSinceOptimize >= 256)
    {
        m_impl->system.OptimizeBroadPhase();
        m_impl->addedSinceOptimize = 0;
    }
    // One collision step per 1/60 s: longer steps would let fast bodies pass further between checks.
    const int collisionSteps = std::max(1, static_cast<int>(std::ceil(seconds * 60.0F - 0.001F)));
    const JPH::EPhysicsUpdateError error = m_impl->system.Update(seconds, collisionSteps, &m_impl->temp, &m_impl->jobs);
    m_impl->jobs.endStep();
    if (error != JPH::EPhysicsUpdateError::None && !m_impl->warnedFull)
    {
        m_impl->warnedFull = true;
        MEOW2_LOG_WARN(m_impl->deps.log,
                       "Physics step ran out of room for body pairs or contacts (raise PhysicsWorldDesc::maxBodyPairs "
                       "or maxContacts); some collisions were skipped");
    }
    m_impl->collectEvents();
}

Math::WorldPosition PhysicsWorld::position(BodyId body) const
{
    MEOW2_ASSERT(contains(body));
    return fromJolt(JPH::Vec3(m_impl->system.GetBodyInterface().GetPosition(toJolt(body))));
}

Math::Quat PhysicsWorld::rotation(BodyId body) const
{
    MEOW2_ASSERT(contains(body));
    return fromJolt(m_impl->system.GetBodyInterface().GetRotation(toJolt(body)));
}

void PhysicsWorld::setTransform(BodyId body, Math::WorldPosition position, Math::Quat rotation)
{
    MEOW2_ASSERT(contains(body));
    JPH::BodyInterface& bodyInterface = m_impl->system.GetBodyInterface();
    const JPH::BodyID id = toJolt(body);
    const bool isStatic = bodyInterface.GetMotionType(id) == JPH::EMotionType::Static;
    bodyInterface.SetPositionAndRotation(id, toJolt(position), toJolt(rotation),
                                         isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
}

void PhysicsWorld::moveKinematic(BodyId body, Math::WorldPosition position, Math::Quat rotation, float seconds)
{
    MEOW2_ASSERT(contains(body));
    MEOW2_ASSERT(seconds > 0.0F);
    JPH::BodyInterface& bodyInterface = m_impl->system.GetBodyInterface();
    MEOW2_ASSERT(bodyInterface.GetMotionType(toJolt(body)) == JPH::EMotionType::Kinematic,
                 "moveKinematic() moves kinematic bodies");
    bodyInterface.MoveKinematic(toJolt(body), toJolt(position), toJolt(rotation), seconds);
}

Math::Vec3 PhysicsWorld::linearVelocity(BodyId body) const
{
    MEOW2_ASSERT(contains(body));
    return fromJolt(m_impl->system.GetBodyInterface().GetLinearVelocity(toJolt(body)));
}

void PhysicsWorld::setLinearVelocity(BodyId body, Math::Vec3 velocity)
{
    MEOW2_ASSERT(contains(body));
    JPH::BodyInterface& bodyInterface = m_impl->system.GetBodyInterface();
    bodyInterface.SetLinearVelocity(toJolt(body), toJolt(velocity));
    if (bodyInterface.GetMotionType(toJolt(body)) != JPH::EMotionType::Static)
    {
        bodyInterface.ActivateBody(toJolt(body));
    }
}

Math::Vec3 PhysicsWorld::angularVelocity(BodyId body) const
{
    MEOW2_ASSERT(contains(body));
    return fromJolt(m_impl->system.GetBodyInterface().GetAngularVelocity(toJolt(body)));
}

void PhysicsWorld::setAngularVelocity(BodyId body, Math::Vec3 velocity)
{
    MEOW2_ASSERT(contains(body));
    JPH::BodyInterface& bodyInterface = m_impl->system.GetBodyInterface();
    bodyInterface.SetAngularVelocity(toJolt(body), toJolt(velocity));
    if (bodyInterface.GetMotionType(toJolt(body)) != JPH::EMotionType::Static)
    {
        bodyInterface.ActivateBody(toJolt(body));
    }
}

void PhysicsWorld::addForce(BodyId body, Math::Vec3 force)
{
    MEOW2_ASSERT(contains(body));
    m_impl->system.GetBodyInterface().AddForce(toJolt(body), toJolt(force), JPH::EActivation::Activate);
}

void PhysicsWorld::addImpulse(BodyId body, Math::Vec3 impulse)
{
    MEOW2_ASSERT(contains(body));
    m_impl->system.GetBodyInterface().AddImpulse(toJolt(body), toJolt(impulse));
}

bool PhysicsWorld::isAwake(BodyId body) const
{
    MEOW2_ASSERT(contains(body));
    return m_impl->system.GetBodyInterface().IsActive(toJolt(body));
}

Math::Vec3 PhysicsWorld::gravity() const
{
    return fromJolt(m_impl->system.GetGravity());
}

void PhysicsWorld::setGravity(Math::Vec3 gravity)
{
    m_impl->system.SetGravity(toJolt(gravity));
}

std::optional<RayHit> PhysicsWorld::castRay(Math::WorldPosition origin, Math::Vec3 translation,
                                            const QueryFilter& filter) const
{
    const JPH::RRayCast ray{toJolt(origin), toJolt(translation)};
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    const QueryLayers layers(filter.layerMask);
    const QueryBodies bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings{}, collector, {}, layers, bodies);
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const JPH::RayCastResult& hit = collector.mHit;
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    RayHit result{.body = toBodyId(hit.mBodyID),
                  .fraction = hit.mFraction,
                  .point = fromJolt(JPH::Vec3(point)),
                  .normal = {},
                  .userData = 0};
    const JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded())
    {
        const JPH::Body& body = lock.GetBody();
        result.normal = fromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
        result.userData = body.GetUserData();
    }
    return result;
}

Result<std::optional<ShapeHit>> PhysicsWorld::castShape(const ShapeDesc& shape, Math::WorldPosition position,
                                                        Math::Quat rotation, Math::Vec3 translation,
                                                        const QueryFilter& filter) const
{
    if (Result<void> valid = validateQueryShape(shape); !valid)
    {
        return Unexpected<Error>{valid.error()};
    }
    Result<JPH::RefConst<JPH::Shape>> made = makeShape(shape, 1000.0F);
    if (!made)
    {
        return Unexpected<Error>{made.error()};
    }
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        *made, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(toJolt(rotation), toJolt(position)),
        toJolt(translation));
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const QueryLayers layers(filter.layerMask);
    const QueryBodies bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastShape(cast, JPH::ShapeCastSettings{}, JPH::RVec3::sZero(), collector, {},
                                                   layers, bodies);
    if (!collector.HadHit())
    {
        return std::optional<ShapeHit>{};
    }
    const JPH::ShapeCastResult& hit = collector.mHit;
    // The penetration axis points into the body that was hit; its surface faces the other way.
    const JPH::Vec3 axis = hit.mPenetrationAxis;
    const JPH::Vec3 normal = axis.IsNearZero() ? JPH::Vec3::sZero() : -axis.Normalized();
    ShapeHit result{.body = toBodyId(hit.mBodyID2),
                    .fraction = hit.mFraction,
                    .point = fromJolt(JPH::Vec3(hit.mContactPointOn2)),
                    .normal = fromJolt(normal),
                    .userData = 0};
    const JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), hit.mBodyID2);
    if (lock.Succeeded())
    {
        result.userData = lock.GetBody().GetUserData();
    }
    return std::optional<ShapeHit>{result};
}

Result<std::vector<BodyId>> PhysicsWorld::overlap(const ShapeDesc& shape, Math::WorldPosition position,
                                                  Math::Quat rotation, const QueryFilter& filter) const
{
    if (Result<void> valid = validateQueryShape(shape); !valid)
    {
        return Unexpected<Error>{valid.error()};
    }
    Result<JPH::RefConst<JPH::Shape>> made = makeShape(shape, 1000.0F);
    if (!made)
    {
        return Unexpected<Error>{made.error()};
    }
    // Jolt places shapes by their centre of mass, which a hull's origin needn't be.
    const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(toJolt(rotation), toJolt(position)) *
                                  JPH::Mat44::sTranslation((*made)->GetCenterOfMass());
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const QueryLayers layers(filter.layerMask);
    const QueryBodies bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CollideShape(*made, JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings{},
                                                      JPH::RVec3::sZero(), collector, {}, layers, bodies);
    std::vector<BodyId> found;
    found.reserve(collector.mHits.size());
    for (const JPH::CollideShapeResult& hit : collector.mHits)
    {
        found.push_back(toBodyId(hit.mBodyID2));
    }
    std::ranges::sort(found, [](BodyId a, BodyId b) { return a.value < b.value; });
    const auto [first, last] = std::ranges::unique(found);
    found.erase(first, last);
    return found;
}

std::span<const ContactEvent> PhysicsWorld::contactEvents() const
{
    return m_impl->contactEvents;
}

std::span<const SensorEvent> PhysicsWorld::sensorEvents() const
{
    return m_impl->sensorEvents;
}

std::uint64_t PhysicsWorld::userData(BodyId body) const
{
    MEOW2_ASSERT(contains(body));
    return m_impl->system.GetBodyInterface().GetUserData(toJolt(body));
}
} // namespace Meow2::Physics
