# Physics

Rigid bodies for games: things fall, collide, slide and rest; rays and shapes can be cast into the world; code learns what touched what. The engine talks to one interface, `Meow2::Physics::PhysicsWorld`, and the library behind it is chosen at build time (techstack.md §3, §14).

- Code: `Meow2Engine/include/Meow2/Physics/` (`PhysicsTypes.h`, `PhysicsLibrary.h`, `PhysicsWorld.h`); `src/Physics/PhysicsValidation.*` (checks every backend shares); `src/Physics/Jolt/` (the Jolt backend); `src/Physics/None/` (a build without physics).
- Library: **Jolt Physics v5.7.0** (`external/JoltPhysics`, MIT).
- Status: 🟨 M8. The interface, the Jolt and None backends and the contract tests are done (#236). Components in scenes, the editor and debug drawing come in M8's next PR; 2D physics (Box2D) is its own milestone, M9.

## Using it

```cpp
// Composition root: the library's process-wide state first, then a world (both owned here).
Meow2::Physics::PhysicsLibrary physics{engineLog};
auto world = *Meow2::Physics::PhysicsWorld::create({.library = physics, .jobs = jobs, .log = engineLog}, {});

using namespace Meow2::Physics;
const BodyId floor = *world->createBody({.shape = BoxShape{{50.0F, 0.5F, 50.0F}},
                                         .position = {0.0F, -0.5F, 0.0F},
                                         .motion = MotionType::Static});
const BodyId crate = *world->createBody({.shape = BoxShape{}, .position = {0.0F, 5.0F, 0.0F}, .userData = entityBits});

// In fixedUpdate (techstack.md §23):
world->step(1.0F / 60.0F);
const Meow2::Math::WorldPosition where = world->position(crate);
for (const ContactEvent& event : world->contactEvents()) { /* play a sound on Begin, … */ }
```

| Build option | Values | |
|---|---|---|
| `MEOW2_PHYSICS_BACKEND` | `Jolt` (default), `None` | `None` builds the engine without a physics library: `PhysicsWorld::create()` returns `ErrorCode::Unsupported`, and `physicsAvailable()` is false. |

## The API

- **Bodies** (`createBody(BodyDesc)`, `destroyBody`, `contains`): a shape, a position and rotation, a `MotionType`, a collision layer, friction, restitution, density (mass comes from volume × density), damping, a gravity scale, `sensor`, `allowSleep`, `continuous` (no tunnelling for fast bodies), starting velocities and `userData` (an `EntityId`'s bits, given back in hits and events).
  - `Static`: never moves, cheapest. `Kinematic`: moved by code with `moveKinematic()`, which gives it the velocity to get there in one step, so it pushes what's in the way. `Dynamic`: moved by the simulation.
  - A destroyed body's id stays invalid even when the library reuses its slot.
- **Shapes** (plain data, `ShapeDesc` is a `std::variant`): `SphereShape`, `BoxShape` (half extents), `CapsuleShape` (along local Y: half the cylinder's height and a radius), `ConvexHullShape` (points; at least 4, not flat), `MeshShape` (triangles, clockwise seen from the front like the renderer's; static and kinematic bodies only) and `CompoundShape` (convex parts placed in the body).
- **Moving things**: `setTransform` (a teleport), `moveKinematic`, `setLinearVelocity` / `setAngularVelocity`, `addForce` (during the next step), `addImpulse` (at once). Each wakes the body.
- **Queries**: `castRay(origin, translation)` gives the nearest hit with its point, outward normal and fraction; `castShape` sweeps a convex shape and gives its first hit; `overlap` lists every body a shape overlaps, sorted. A `QueryFilter` limits them to some layers, skips one body, and includes or leaves out sensors.
- **Errors**: bad input (a size not above 0, a flat hull, a mesh index out of range, a mesh on a dynamic body, a layer the world doesn't have, …) is refused with `ErrorCode::InvalidArgument` and a message, never an assert, and the same way for every backend (`PhysicsValidation.cpp`). Using a destroyed body with the other functions is a bug, and asserts.

### Collision layers

A world has up to 32 layers (`PhysicsWorldDesc::layerCount`), and `layerMasks[a]` has bit `b` set when layer `a` collides with layer `b`. Two layers collide only when both masks allow it. Everything collides with everything by default. Queries take a layer mask of their own.

### Events

After each `step()`, `contactEvents()` lists the pairs of bodies that started (`Begin`, with a point and a normal from A to B) or stopped (`End`) touching, and `sensorEvents()` the bodies that entered or left a sensor.

- **One Begin and one End per pair**, however many parts of two bodies touch (a compound, a mesh's triangles): the backend counts the touching parts and reports the pair's first and last.
- **Sorted by body id**, so every run gives the same events in the same order, though the library reports them from several threads.
- **Sleeping bodies still touch.** A box resting on a floor falls asleep after half a second, and Jolt then drops the contact, reporting it as removed. That isn't the box leaving the floor, so the pair is kept as *dormant*: when a body of the pair wakes, either the contact comes back (no new event) or, if Jolt no longer finds them in contact, the pair ends then.
- **Destroying a body** ends its touches at once; the End events come with the next step.

### Determinism

The same bodies, created in the same order and stepped with the same times, give bit-identical results on one platform, whatever the number of threads (`Physics.PhysicsWorldTest.Determinism`). Across platforms (Windows against Linux) Jolt needs its `CROSS_PLATFORM_DETERMINISTIC` mode, which costs speed and isn't on yet.

## How it works

- **The interface** is one concrete class with a private `Impl` (pimpl), defined by whichever backend is compiled. No virtual calls, and no library header in a public engine header (techstack.md §3).
- **`PhysicsLibrary`** owns Jolt's process-wide state: its allocation, trace and assert hooks (plain function pointers, set while it lives) and its type registry (`JPH::Factory`, `RegisterTypes()`). Only one may exist at a time. The composition root makes it before any world and destroys it after the last one.
- **Memory**: in Debug and RelWithDebInfo every Jolt allocation goes to the process-wide tracker under `MemTag::Physics`, including a world's scratch memory for a step (`PhysicsWorldDesc::tempMemoryBytes`, 10 MiB, taken once). `Physics.PhysicsMemoryTest.MemoryTagged` checks it all comes back. Release uses Jolt's default allocator.
- **Messages**: Jolt's trace output becomes debug lines in the log; its failed asserts are reported through ours (`MEOW2_ASSERT`'s handler), so the same break/continue/abort rules apply.
- **Layers in Jolt**: Jolt sorts bodies into *broad-phase layers* (separate bounding-box trees) and *object layers* (what collides with what). Each of our layers becomes two object layers, one for static bodies and one for moving ones (`layer * 2 + moving`), and the broad phase has two trees, static and moving. So static bodies are never tested against each other, moving ones only search the trees they can collide with, and our masks decide the rest.
- **Threads** (`JoltJobSystem`): Jolt splits a step into jobs with dependencies and waits for groups of them at *barriers*. `JoltJobSystem` (a `JPH::JobSystemWithBarrier`) takes jobs from a fixed pool, as Jolt's own thread pool does, and hands each one whose dependencies are done to our `JobSystem::run()`, so physics uses the engine's enkiTS threads instead of starting its own (techstack.md §19). A barrier's waiter runs ready jobs itself, so waiting never idles a core. `run()` returns a handle that waits for its job when destroyed, and Jolt forgets the jobs it queues, so the handles are kept until the step ends (`endStep()`); by then every job has finished. A job can be picked up twice (by our thread and by a barrier's waiter); Jolt's `Execute()` makes the second call do nothing.
- **Shapes** are built from Jolt's shape settings on the stack (`settings.Create()` keeps the shape, not the settings). A box's edges are rounded by a convex radius no larger than its smallest half extent.
- **The broad phase is rebuilt** (`OptimizeBroadPhase()`) at the next step after 256 or more bodies were added, as after a level load, not every step.
- **Steps longer than 1/60 s** are split into collision steps of at most 1/60 s, so fast bodies don't jump further between checks.
- **Building Jolt**: from its own `Jolt/Jolt.cmake`, inside a CMake function because that file sets `CMAKE_CXX_STANDARD` 17 for its scope. The target is SYSTEM, so our warnings don't apply to its headers. Jolt reads its SIMD level (AVX2, FMA, F16C) from the compiler flags `MEOW2_SIMD` already sets, so the library and the engine always agree. The debug renderer, profiler, object streams and GPU compute are off. In the Jolt source files `<Jolt/Jolt.h>` is fenced from clang-format's include sorting, because it must come before every other Jolt header.

## Adding a backend (e.g. Box3D)

1. Add the library (a submodule: needs the owner's approval) and a `MEOW2_PHYSICS_BACKEND` value in `cmake/Meow2External.cmake`.
2. Add `src/Physics/<Backend>/` with `PhysicsLibrary` and `PhysicsWorld` defined over it, and list it in `Meow2Engine/CMakeLists.txt`.
3. Use `PhysicsValidation.h` for input checks, so every backend refuses the same things.
4. Make the contract tests pass (`ctest -R "^Physics\."`) with that option. Box3D's allocator hook (`b3SetAllocator`), assert hook (`b3SetAssertFcn`) and task callbacks (`enqueueTask` / `finishTask`) map onto the same engine pieces as Jolt's.

## Why it's done this way

- **One interface, build-time selection**: games and the editor never see the library, so it can be replaced (Jolt now, Box3D possibly later) without touching them, and there are no virtual calls on the hot path. Runtime switching, as the RHI does between Vulkan and D3D12, isn't worth its cost here: nobody swaps physics engines while a game runs.
- **Jolt first** (owner decision 2026-11-26): mature, used in shipped games and in Godot, MIT, with a job interface that fits our job system and a double-precision mode for large worlds. Box3D is newer (alpha since June 2026) and fits the same interface.
- **2D separately** (owner decision 2026-11-26): a 3D world locked to a plane (Jolt's `EAllowedDOFs::Plane2D`) still pays for 3D collision and lacks 2D tools such as chain shapes and one-way platforms, so 2D games get Box2D in a `Physics2D` module (M9).
- **Events with dormant pairs**: what code wants to know is "did these touch", not "did the library keep a contact"; libraries differ on what they report when bodies sleep, so the interface promises the physical meaning and each backend provides it.

## Tests

The contract (`Meow2Engine/tests/Physics/PhysicsWorldTests.cpp`); every backend must pass it. In a build without physics, each test checks that creating a world reports `Unsupported` and skips the rest.

| Test | What it checks |
|---|---|
| `Physics.PhysicsWorldTest.BodyFallsAndRests` | A box falls under gravity (position and speed after 0.5 s), lands and rests on a floor, falls asleep, and wakes for an impulse. |
| `Physics.PhysicsWorldTest.ShapesAndMotionTypes` | Every shape kind makes a body; static bodies stay put, kinematic ones follow `moveKinematic` with the right velocity, dynamic ones keep a set velocity; teleports; gravity. |
| `Physics.PhysicsWorldTest.LayersFilterCollisions` | A body falls through the floor of a layer it ignores, another lands; queries see only their mask's layers; layer counts are checked. |
| `Physics.PhysicsWorldTest.Queries` | Ray hits with point, normal, fraction and user data; ignored bodies; misses; a swept sphere's first hit; overlaps sorted and unique; non-convex query shapes refused. |
| `Physics.PhysicsWorldTest.ContactAndSensorEvents` | One Begin for a box landing on the floor (none when it falls asleep), a sensor's enter and leave for a ball passing through, an End when a body is destroyed, delivered once. |
| `Physics.PhysicsWorldTest.Determinism` | 64 bodies stepped for 3 s twice, on 4 threads: bit-identical positions and rotations. |
| `Physics.PhysicsWorldTest.BadInput` | Bad shapes and values refused with `InvalidArgument`, nothing half-made left; destroyed ids refused, and not reused. |
| `Physics.PhysicsMemoryTest.MemoryTagged` | Jolt's memory is counted under `MemTag::Physics` and all of it comes back (Debug and RelWithDebInfo). |

See also: techstack.md §3, §14, §19; [Memory](Memory.md), [Jobs](Jobs.md), [GameLoop](GameLoop.md).
