/// @file
/// @brief Registering the physics components (PhysicsComponents.h).

#include <Meow2/Physics/PhysicsComponents.h>

namespace Meow2::Physics
{
void registerPhysicsComponents(Scene::ComponentRegistry& registry)
{
    using Scene::component;
    using Scene::field;
    registry.add(component<RigidBody>(
        "RigidBody", 1,
        {field<RigidBody>("motion", [](RigidBody& c) -> auto& { return c.motion; }),
         field<RigidBody>("mass", [](RigidBody& c) -> auto& { return c.mass; }),
         field<RigidBody>("linearDamping", [](RigidBody& c) -> auto& { return c.linearDamping; }),
         field<RigidBody>("angularDamping", [](RigidBody& c) -> auto& { return c.angularDamping; }),
         field<RigidBody>("gravityScale", [](RigidBody& c) -> auto& { return c.gravityScale; }),
         field<RigidBody>("continuous", [](RigidBody& c) -> auto& { return c.continuous; }),
         field<RigidBody>("allowSleep", [](RigidBody& c) -> auto& { return c.allowSleep; })}));
    registry.add(
        component<Collider>("Collider", 1,
                            {field<Collider>("shape", [](Collider& c) -> auto& { return c.shape; }),
                             field<Collider>("center", [](Collider& c) -> auto& { return c.center; }),
                             field<Collider>("size", [](Collider& c) -> auto& { return c.size; }),
                             field<Collider>("radius", [](Collider& c) -> auto& { return c.radius; }),
                             field<Collider>("height", [](Collider& c) -> auto& { return c.height; }),
                             field<Collider>("friction", [](Collider& c) -> auto& { return c.friction; }),
                             field<Collider>("restitution", [](Collider& c) -> auto& { return c.restitution; }),
                             field<Collider>("sensor", [](Collider& c) -> auto& { return c.sensor; })}));
}
} // namespace Meow2::Physics
