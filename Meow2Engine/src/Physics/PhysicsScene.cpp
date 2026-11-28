/// @file
/// @brief PhysicsScene (PhysicsScene.h): building bodies from components, and the fixed step.

#include <Meow2/Math/Transform.h>
#include <Meow2/Physics/PhysicsScene.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <type_traits>
#include <utility>

namespace Meow2::Physics
{
namespace
{
using DirectX::XMVECTOR;

/// @brief Below this, a rotation or an offset counts as none (a compound of one part is avoided).
constexpr float c_epsilon = 1e-5F;

XMVECTOR loadVec3(const Math::Vec3& value)
{
    return DirectX::XMLoadFloat3(&value);
}

XMVECTOR loadQuat(const Math::Quat& value)
{
    return DirectX::XMLoadFloat4(&value);
}

Math::Vec3 storeVec3(XMVECTOR value)
{
    Math::Vec3 out;
    DirectX::XMStoreFloat3(&out, value);
    return out;
}

Math::Quat storeQuat(XMVECTOR value)
{
    Math::Quat out;
    DirectX::XMStoreFloat4(&out, value);
    return out;
}

Math::Vec3 multiply(const Math::Vec3& a, const Math::Vec3& b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

float largest(const Math::Vec3& value)
{
    return std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
}

/// @brief An entity's world position, rotation and scale (its WorldTransform split up); nothing if it has none or
///        the matrix can't be split (a zero scale).
std::optional<Math::Transform> worldOf(const Scene::Scene& scene, entt::entity entity)
{
    const auto* world = scene.registry().try_get<Scene::WorldTransform>(entity);
    if (world == nullptr)
    {
        return std::nullopt;
    }
    return Math::fromMatrix(DirectX::XMLoadFloat4x4(&world->matrix));
}

/// @brief The nearest entity at or above `entity` with a RigidBody, if any.
std::optional<entt::entity> bodyOwner(const Scene::Scene& scene, entt::entity entity)
{
    const auto& registry = scene.registry();
    for (entt::entity at = entity; at != entt::null && registry.valid(at);)
    {
        if (registry.all_of<RigidBody>(at))
        {
            return at;
        }
        const auto* parent = registry.try_get<Scene::Parent>(at);
        at = parent != nullptr ? parent->entity : entt::null;
    }
    return std::nullopt;
}

/// @brief A Collider as a shape in its entity's space: the shape (sized by the entity's world scale) and where its
///        origin sits (the centre offset, scaled; mesh points already include it, so their offset is zero). Plain data.
struct Piece
{
    ShapeDesc shape;
    Math::Vec3 offset{};
};

/// @brief A Collider's shape at its entity's world scale.
/// @param convex A mesh becomes a convex hull of its vertices (a moving body's, or one part of several).
Result<Piece> pieceFor(const Collider& collider, const Math::Vec3& scale, bool convex, const CollisionMesh* mesh)
{
    const Math::Vec3 offset = multiply(collider.center, scale);
    switch (collider.shape)
    {
        case ColliderShape::Box:
        {
            const Math::Vec3 size = multiply(collider.size, scale);
            return Piece{.shape = BoxShape{Math::Vec3{std::abs(size.x) * 0.5F, std::abs(size.y) * 0.5F,
                                                      std::abs(size.z) * 0.5F}},
                         .offset = offset};
        }
        case ColliderShape::Sphere:
            return Piece{.shape = SphereShape{collider.radius * largest(scale)}, .offset = offset};
        case ColliderShape::Capsule:
        {
            const float radius = collider.radius * std::max(std::abs(scale.x), std::abs(scale.z));
            const float half = collider.height * std::abs(scale.y) * 0.5F - radius;
            if (half <= c_epsilon)
            {
                return Piece{.shape = SphereShape{radius}, .offset = offset}; // no room for a cylinder: all cap
            }
            return Piece{.shape = CapsuleShape{.halfHeight = half, .radius = radius}, .offset = offset};
        }
        case ColliderShape::Mesh:
        {
            if (mesh == nullptr)
            {
                return fail(ErrorCode::NotFound,
                            "its mesh isn't known (a mesh Collider uses the entity's MeshRenderer)");
            }
            std::vector<Math::Vec3> points;
            points.reserve(mesh->positions.size());
            for (const Math::Vec3& position : mesh->positions)
            {
                const Math::Vec3 scaled = multiply(position, scale);
                points.push_back(Math::Vec3{scaled.x + offset.x, scaled.y + offset.y, scaled.z + offset.z});
            }
            if (convex)
            {
                return Piece{.shape = ConvexHullShape{std::move(points)}, .offset = {}};
            }
            return Piece{.shape = MeshShape{.vertices = std::move(points), .indices = mesh->indices}, .offset = {}};
        }
    }
    return fail(ErrorCode::InvalidArgument, "unknown collider shape");
}

/// @brief The convex alternatives of a ShapeDesc as a compound part (a mesh or a compound can't be one).
std::optional<CompoundPartShape> asPart(ShapeDesc shape)
{
    return std::visit(
        [](auto&& value) -> std::optional<CompoundPartShape>
        {
            using Shape = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Shape, MeshShape> || std::is_same_v<Shape, CompoundShape>)
            {
                return std::nullopt;
            }
            else
            {
                return CompoundPartShape{std::forward<decltype(value)>(value)};
            }
        },
        std::move(shape));
}

/// @brief Moves a mesh's points by a rotation and an offset (a mesh can't be a compound part, so where it sits in its
///        body is put into its points instead).
void placeMesh(MeshShape& mesh, const Math::Vec3& position, const Math::Quat& rotation)
{
    const XMVECTOR q = loadQuat(rotation);
    const XMVECTOR p = loadVec3(position);
    for (Math::Vec3& vertex : mesh.vertices)
    {
        vertex = storeVec3(DirectX::XMVectorAdd(DirectX::XMVector3Rotate(loadVec3(vertex), q), p));
    }
}

bool isIdentity(const Math::Vec3& position, const Math::Quat& rotation)
{
    return std::abs(position.x) < c_epsilon && std::abs(position.y) < c_epsilon && std::abs(position.z) < c_epsilon &&
           std::abs(std::abs(rotation.w) - 1.0F) < c_epsilon;
}

/// @brief A Collider of a body: its piece, placed relative to the body's position and rotation. Plain data.
struct Part
{
    entt::entity entity = entt::null;
    Piece piece;
    Math::Vec3 position{}; ///< Where the piece's origin is, in the body's frame.
    Math::Quat rotation{}; ///< How it's turned, in the body's frame.
};

/// @brief One shape from a body's parts: the piece alone when there's one at the body's origin, a compound otherwise
///        (meshes have their placement put into their points; they can only be alone).
Result<ShapeDesc> combine(std::vector<Part>& parts)
{
    if (parts.size() == 1)
    {
        Part& part = parts.front();
        if (auto* mesh = std::get_if<MeshShape>(&part.piece.shape))
        {
            placeMesh(*mesh, part.position, part.rotation);
            return part.piece.shape;
        }
        if (isIdentity(part.position, part.rotation))
        {
            return part.piece.shape;
        }
    }
    CompoundShape compound;
    for (Part& part : parts)
    {
        std::optional<CompoundPartShape> shape = asPart(std::move(part.piece.shape));
        if (!shape)
        {
            return fail(ErrorCode::InvalidArgument, "a mesh Collider can't share a body with other Colliders");
        }
        compound.parts.push_back(
            CompoundPart{.shape = std::move(*shape), .position = part.position, .rotation = part.rotation});
    }
    return ShapeDesc{std::move(compound)};
}
} // namespace

PhysicsScene::PhysicsScene(PhysicsSceneDeps deps)
    : m_deps(std::move(deps))
{
}

PhysicsScene::~PhysicsScene()
{
    clear();
}

void PhysicsScene::clear()
{
    for (const Body& body : m_bodies)
    {
        static_cast<void>(m_deps.world.destroyBody(body.id));
    }
    m_bodies.clear();
}

std::vector<std::string> PhysicsScene::build()
{
    clear();
    m_deps.transforms.update(); // world transforms as they are now
    Scene::Scene& scene = m_deps.scene;
    const auto& registry = scene.registry();
    std::vector<std::string> warnings;
    const auto nameOf = [&scene](entt::entity entity)
    {
        const auto* name = scene.registry().try_get<Scene::NameComponent>(entity);
        return name != nullptr && !name->name.empty() ? name->name : std::string{"an entity"};
    };
    const auto meshFor = [this, &scene](entt::entity entity) -> const CollisionMesh*
    {
        if (!m_deps.meshOf)
        {
            return nullptr;
        }
        const std::optional<AssetId> mesh = m_deps.meshOf(scene, entity);
        return mesh ? m_deps.meshes.find(*mesh) : nullptr;
    };

    // Which Colliders make each RigidBody's body, and which stand alone as static scenery. The collider entities are
    // sorted by EntityId first, so bodies (and their ids) come out in the same order on every run: the world's
    // results depend on the order bodies are made.
    std::vector<std::pair<Uuid, entt::entity>> colliders;
    for (const entt::entity entity : registry.view<Collider>())
    {
        colliders.emplace_back(scene.idOf(entity).uuid(), entity);
    }
    std::ranges::sort(colliders, [](const auto& a, const auto& b) { return a.first < b.first; });
    std::map<Uuid, std::pair<entt::entity, std::vector<entt::entity>>> bodies; // RigidBody entity → its colliders
    std::vector<entt::entity> statics;
    for (const auto& [id, entity] : colliders)
    {
        if (const std::optional<entt::entity> owner = bodyOwner(scene, entity))
        {
            auto& [ownerEntity, parts] = bodies[scene.idOf(*owner).uuid()];
            ownerEntity = *owner;
            parts.push_back(entity);
        }
        else
        {
            statics.push_back(entity);
        }
    }
    for (const entt::entity entity : registry.view<RigidBody>())
    {
        if (!bodies.contains(scene.idOf(entity).uuid()))
        {
            warnings.push_back(
                std::format("{}: its RigidBody has no Collider (on it or a child); skipped", nameOf(entity)));
        }
    }

    const auto addBody = [&](entt::entity entity, const BodyDesc& desc, MotionType motion)
    {
        Result<BodyId> made = m_deps.world.createBody(desc);
        if (!made)
        {
            warnings.push_back(std::format("{}: no body: {}", nameOf(entity), made.error().message));
            return;
        }
        m_bodies.push_back(Body{.entity = entity, .id = *made, .motion = motion});
    };

    // Static scenery: one body per Collider, where its entity is.
    for (const entt::entity entity : statics)
    {
        const std::optional<Math::Transform> world = worldOf(scene, entity);
        if (!world)
        {
            warnings.push_back(std::format("{}: its transform can't be used (a scale of 0?); skipped", nameOf(entity)));
            continue;
        }
        const Collider& collider = registry.get<Collider>(entity);
        Result<Piece> piece = pieceFor(collider, world->scale, false, meshFor(entity));
        if (!piece)
        {
            warnings.push_back(std::format("{}: {}; skipped", nameOf(entity), piece.error().message));
            continue;
        }
        const Math::Vec3 offset = piece->offset; // the body sits at the entity's origin; the shape at its centre
        std::vector<Part> parts;
        parts.push_back(Part{.entity = entity, .piece = std::move(*piece), .position = offset, .rotation = {}});
        Result<ShapeDesc> shape = combine(parts);
        if (!shape)
        {
            warnings.push_back(std::format("{}: {}; skipped", nameOf(entity), shape.error().message));
            continue;
        }
        addBody(entity,
                BodyDesc{.shape = std::move(*shape),
                         .position = world->position,
                         .rotation = world->rotation,
                         .motion = MotionType::Static,
                         .friction = collider.friction,
                         .restitution = collider.restitution,
                         .sensor = collider.sensor,
                         .userData = static_cast<std::uint64_t>(entt::to_integral(entity))},
                MotionType::Static);
    }

    // RigidBodies: their Colliders as one shape, each placed relative to the body.
    for (auto& [id, owner] : bodies)
    {
        const auto& [entity, colliderEntities] = owner;
        const RigidBody& rigidBody = registry.get<RigidBody>(entity);
        const std::optional<Math::Transform> world = worldOf(scene, entity);
        if (!world)
        {
            warnings.push_back(std::format("{}: its transform can't be used (a scale of 0?); skipped", nameOf(entity)));
            continue;
        }
        // A mesh stays a mesh only alone on a body that doesn't move by itself; otherwise it becomes a hull.
        const bool convex = rigidBody.motion == MotionType::Dynamic || colliderEntities.size() > 1;
        const XMVECTOR bodyPosition = loadVec3(world->position);
        const XMVECTOR bodyRotation = loadQuat(world->rotation);
        std::vector<Part> parts;
        for (const entt::entity colliderEntity : colliderEntities)
        {
            const std::optional<Math::Transform> colliderWorld = worldOf(scene, colliderEntity);
            if (!colliderWorld)
            {
                warnings.push_back(
                    std::format("{}: its transform can't be used (a scale of 0?); skipped", nameOf(colliderEntity)));
                continue;
            }
            Result<Piece> piece =
                pieceFor(registry.get<Collider>(colliderEntity), colliderWorld->scale, convex, meshFor(colliderEntity));
            if (!piece)
            {
                // That part is left out; the body keeps the parts that work (as Unity ignores a broken collider).
                warnings.push_back(std::format("{}: {}", nameOf(colliderEntity), piece.error().message));
                continue;
            }
            // The piece's origin in the world, then in the body's frame.
            const XMVECTOR colliderRotation = loadQuat(colliderWorld->rotation);
            const XMVECTOR origin = DirectX::XMVectorAdd(
                loadVec3(colliderWorld->position), DirectX::XMVector3Rotate(loadVec3(piece->offset), colliderRotation));
            const Math::Vec3 position = storeVec3(
                DirectX::XMVector3InverseRotate(DirectX::XMVectorSubtract(origin, bodyPosition), bodyRotation));
            // XMQuaternionMultiply(a, b) is "a, then b": the collider's rotation, then the body's undone.
            const Math::Quat rotation = storeQuat(DirectX::XMQuaternionNormalize(
                DirectX::XMQuaternionMultiply(colliderRotation, DirectX::XMQuaternionInverse(bodyRotation))));
            parts.push_back(
                Part{.entity = colliderEntity, .piece = std::move(*piece), .position = position, .rotation = rotation});
        }
        if (parts.empty())
        {
            warnings.push_back(std::format("{}: its RigidBody has no usable Collider; skipped", nameOf(entity)));
            continue;
        }
        Result<ShapeDesc> shape = combine(parts);
        if (!shape)
        {
            warnings.push_back(std::format("{}: {}; skipped", nameOf(entity), shape.error().message));
            continue;
        }
        // The body's surface: its own Collider's, or its first child's.
        const Collider* surface = registry.try_get<Collider>(entity);
        if (surface == nullptr)
        {
            surface = &registry.get<Collider>(colliderEntities.front());
        }
        addBody(entity,
                BodyDesc{.shape = std::move(*shape),
                         .position = world->position,
                         .rotation = world->rotation,
                         .motion = rigidBody.motion,
                         .friction = surface->friction,
                         .restitution = surface->restitution,
                         .mass = rigidBody.mass,
                         .linearDamping = rigidBody.linearDamping,
                         .angularDamping = rigidBody.angularDamping,
                         .gravityScale = rigidBody.gravityScale,
                         .sensor = surface->sensor,
                         .allowSleep = rigidBody.allowSleep,
                         .continuous = rigidBody.continuous,
                         .userData = static_cast<std::uint64_t>(entt::to_integral(entity))},
                rigidBody.motion);
    }
    for (const std::string& warning : warnings)
    {
        MEOW2_LOG_WARN(m_deps.log, "Physics: {}", warning);
    }
    return warnings;
}

void PhysicsScene::step(float seconds)
{
    Scene::Scene& scene = m_deps.scene;
    auto& registry = scene.registry();
    m_deps.transforms.update();
    // Kinematic bodies go where their entities are now, over this step (so they push what's in the way).
    for (const Body& body : m_bodies)
    {
        if (body.motion != MotionType::Kinematic || !registry.valid(body.entity))
        {
            continue;
        }
        if (const std::optional<Math::Transform> world = worldOf(scene, body.entity))
        {
            m_deps.world.moveKinematic(body.id, world->position, world->rotation, seconds);
        }
    }
    m_deps.world.step(seconds);
    // Dynamic bodies that moved write back to their entities: the new world pose, made local to the parent.
    for (const Body& body : m_bodies)
    {
        if (body.motion != MotionType::Dynamic || !registry.valid(body.entity) || !m_deps.world.isAwake(body.id))
        {
            continue;
        }
        const auto* local = registry.try_get<Scene::LocalTransform>(body.entity);
        const std::optional<Math::Transform> world = worldOf(scene, body.entity);
        if (local == nullptr || !world)
        {
            continue;
        }
        const Math::Transform moved{.position = m_deps.world.position(body.id),
                                    .rotation = m_deps.world.rotation(body.id),
                                    .scale = world->scale};
        Math::Transform next = moved;
        if (const auto* parent = registry.try_get<Scene::Parent>(body.entity); parent != nullptr)
        {
            const auto* parentWorld = registry.try_get<Scene::WorldTransform>(parent->entity);
            if (parentWorld != nullptr)
            {
                // Row vectors: world = local × parentWorld, so local = world × parentWorld⁻¹.
                const DirectX::XMMATRIX parentMatrix = DirectX::XMLoadFloat4x4(&parentWorld->matrix);
                const DirectX::XMMATRIX localMatrix =
                    DirectX::XMMatrixMultiply(Math::toMatrix(moved), DirectX::XMMatrixInverse(nullptr, parentMatrix));
                if (const std::optional<Math::Transform> split = Math::fromMatrix(localMatrix))
                {
                    next = *split;
                }
            }
        }
        next.scale = local->value.scale; // physics never scales; keep exactly what was there
        m_deps.transforms.setLocal(body.entity, next);
    }
    m_deps.transforms.update();
}

std::optional<BodyId> PhysicsScene::bodyOf(entt::entity entity) const
{
    const auto found = std::ranges::find_if(m_bodies, [entity](const Body& body) { return body.entity == entity; });
    return found != m_bodies.end() ? std::optional<BodyId>{found->id} : std::nullopt;
}
} // namespace Meow2::Physics
