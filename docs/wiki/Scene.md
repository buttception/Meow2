# Scene

A `Scene` is one world of **entities** and **components**, built on [EnTT](https://github.com/skypjack/entt) v3.16.0. Every entity has a fast runtime handle (`entt::entity`) for systems, and a saved identity (`EntityId`, see [Ids](Ids.md)) for files, the editor, undo and MCP.

- Header: `#include <Meow2/Scene/Scene.h>`. Namespace `Meow2::Scene`.
- EnTT's types (`entt::entity`, the registry, views) are part of the API on purpose: systems iterate EnTT views directly.
- Status: ✅ M2 (#95, command buffers and transient entities #97); transform hierarchy M3 (#129).

## Entities and components in one paragraph

An **entity** is just an ID. A **component** is a plain data struct attached to an entity (`Health{100}`, `Velocity{…}`). A **system** is a function that runs over all entities that have a given set of components (*a view*). EnTT stores each component type in its own densely packed array (a *sparse set*), so a view walks memory in order: fast to iterate, with O(1) add, remove and lookup. There's no `GameObject` class hierarchy and no virtual `update()`: behaviour lives in systems, data in components.

## Using it

```cpp
Meow2::UuidGenerator ids;                                     // composition root
Meow2::Scene::Scene scene{ids, Meow2::Memory::globalTracker()};

scene.registerComponent<Health>();                            // every component type, once, at startup

const entt::entity player = scene.create("Player");          // IdComponent + NameComponent
scene.registry().emplace<Health>(player, 100);

for (auto [entity, health] : scene.registry().view<Health>().each())
{
    health.value -= 1;
}

const Meow2::EntityId saved = scene.idOf(player);             // what a scene file stores
std::optional<entt::entity> again = scene.find(saved);        // back to the handle
scene.destroy(player);
```

| API | What it does |
|---|---|
| `create(name = {})` | new entity with a new `EntityId` (`IdComponent`) and, if named, a `NameComponent` |
| `createWithId(id, name)` | when loading: uses the saved ID; `InvalidArgument` for an invalid one, `AlreadyExists` for a duplicate |
| `destroy(entity)` | removes the entity and its components; the handle may be reused later, the ID never is |
| `find(id)`, `idOf(entity)`, `contains(entity)`, `entityCount()` | lookups |
| `registerComponent<T>()` | creates T's storage now (see below) |
| `registry()` | the EnTT registry for views and `emplace`/`get`/`remove` of components |

**Saved and transient entities.** `create()` gives an entity a saved `EntityId`; `createTransient()` doesn't, for things that are never saved or referenced (bullets, particles, hit effects), which then cost no ID and no map entry. `idOf()` returns an invalid ID for them, `entityCount()` counts both kinds, `persistentCount()` only saved ones.

**The ID map is always right.** EnTT's construct/destroy signals on `IdComponent` keep it in sync, so it doesn't matter whether an entity dies through `destroy()`, `registry().destroy()` or a command, or loses its `IdComponent`. Create entities through the Scene (`registry().create()` would make one without an ID, i.e. transient).

## Changes from jobs: command buffers

Jobs must not create or destroy entities or add/remove components directly: that reshuffles EnTT's packed arrays under the other jobs' iterators. They **record** the change in their thread's buffer instead, and the main thread performs everything at one point in the frame:

```cpp
Meow2::Scene::Scene scene{ids, Meow2::Memory::globalTracker(), jobs.threadCount()}; // one buffer per job thread

// inside a job (parallelFor gives the thread index):
Meow2::Scene::SceneCommands& commands = scene.commands(threadIndex);
commands.destroy(enemy);                                   // when health hits 0
commands.emplace(player, Burning{3.0f});                   // add or replace a component
commands.remove<Shield>(player);
commands.spawnTransient(Bullet{}, Velocity{0, 0, 40});     // appears at apply
const Meow2::EntityId id = commands.spawn("Pickup", Pickup{}); // saved; its ID is known right away

// on the main thread, e.g. after the fixed steps:
scene.applyCommands();
```

- **No locks:** each buffer belongs to one thread (`JobSystem::threadIndex()`; the main thread is 0), so recording is a plain append.
- **Deterministic order:** `applyCommands()` runs buffer 0, then 1, …, each in recording order.
- **Stale handles are skipped:** a command for an entity that's already gone (destroyed by an earlier command, or before) does nothing. EnTT handles carry a version, so a recycled slot isn't mistaken for the old entity.
- **Components must be registered** (`registerComponent<T>()`): the buffers assert `isRegistered<T>()`, so a job never makes EnTT create a pool. They must also be copyable (they're stored in a `std::function`).
- Commands are `std::function`s in a `std::vector` per thread for now; they'll move to the frame arena with M5.

## Transforms and the hierarchy

`#include <Meow2/Scene/TransformHierarchy.h>`: entities with a position get a **local** transform (relative to their parent) and a **world** transform (in the world). A wheel is placed relative to its car; moving the car moves the wheel.

| Component | Holds | Written by |
|---|---|---|
| `LocalTransform` | `Math::Transform` relative to the parent | you: `setLocal()`, or directly + `markDirty()` |
| `WorldTransform` | local → world matrix | `update()` |
| `PreviousWorldTransform` | the world matrix at the previous fixed step | `savePrevious()` |
| `Parent` | the parent entity (absent on roots) | `add()`, `setParent()` |
| `TransformState` | dirty / changed flags and the depth | the hierarchy |

```cpp
Meow2::Scene::TransformHierarchy transforms{scene, jobs};      // composition root, after the Scene
transforms.add(car, Math::Transform{.position = {0.0F, 0.0F, 10.0F}});
transforms.add(wheel, Math::Transform{.position = {1.0F, -0.5F, 1.5F}}, car);

// each fixed step:
transforms.savePrevious();                       // remember where things were
transforms.setLocal(car, movedCar);              // gameplay moves things
transforms.update();                             // world transforms follow, in parallel
// render(alpha):
const Math::Mat4 world = transforms.interpolatedWorld(wheel, alpha);
```

**How `update()` works.**
- **Depth levels:** entities are grouped by depth (roots, their children, grandchildren, …). Each level runs as one `parallelFor` on the job system. A child only reads its parent's world matrix, which the previous level finished, so there are no locks and no data races. The TSan CI job checks this with 100k entities.
- **Dirty flags:** an entity is recomputed only if its local transform changed or its parent's world transform changed this update. A scene where only a few things move costs one flag check per entity.
- **Structure:** the levels are rebuilt only after a structural change (a transform added, a parent set or removed, an entity destroyed). EnTT's signals report those.
- **World matrices, not TRS:** a world matrix is exact even under a non-uniformly scaled parent, where a TRS would need shear (see Transform in [Math](Math.md#transform)).
- **`render(alpha)`:** `interpolatedWorld` splits the previous and current world matrices into position, rotation and scale, lerps and slerps them, and recombines them. Motion is smooth even when the screen refreshes faster than the fixed step.
- **Destroying a parent** turns its children into roots. They keep their local transform, which is now relative to the world. To remove a whole subtree, destroy it child-first.
- **Reparenting** keeps the local transform (the entity moves with its new parent). Making an entity its own ancestor asserts.

## Rules

- **Components are plain data structs** (no behaviour, no invariants), like events.
- **Register every component type at startup.** EnTT creates a type's storage the first time it's used. If that first use happened on a job thread while another thread iterates, it would be a data race. `registerComponent<T>()` creates it up front. `IdComponent` and `NameComponent` are registered by the Scene itself.
- **Threading:** the Scene belongs to the thread that created it (the main thread). `create`, `destroy` and `registerComponent` assert that. Jobs may read and write components in parallel when they touch *different entities* or *different component types* and nothing changes structure meanwhile. Structural changes from jobs go through command buffers (above).

## Memory

All of the scene's memory (the registry, every component pool, the ID map) is counted under **`MemTag::Scene`**:
- `Memory::TaggedResource` forwards to the upstream resource (e.g. `globalTracker()`) inside a `MemTagScope{Scene}`.
- The registry's allocator is `Memory::ResourceAllocator<entt::entity>`. `std::pmr::polymorphic_allocator` doesn't work with EnTT: its `construct()` hands itself to every object it builds that takes an allocator, and EnTT's pools don't accept that extra argument. `ResourceAllocator` only allocates and frees.
- Strings *inside* components (a `NameComponent`'s name) use their own default allocator, counted under the thread's current tag.

The test `Scene.SceneMemory.CountedUnderSceneTag` creates 1000 entities against a `TrackingAllocator` and checks that everything lands on `Scene` and is all returned when the scene is destroyed.

## Tests

`Scene.SceneTest.*`: IDs and names on create, find by ID, destroy, loading with saved IDs (duplicates and invalid IDs rejected), views, pools that exist before first use. `Scene.SceneMemory.CountedUnderSceneTag`. `Core.TaggedResource.CountsUnderTag` checks the memory resource. Part 2: `Scene.SceneTest.TransientHasNoId`, `RegistryDestroyKeepsMapInSync`, `RemovingIdMakesEntityTransient`, `IsRegistered`; `Scene.SceneCommandsTest.*` (spawn appears on apply, destroy/emplace/remove in order, stale handles skipped); `Scene.SceneCommandsJobs.JobsRecordInParallel` (4 job threads record changes for 10,000 entities at once; TSan checks it in CI). Transforms: `Scene.TransformHierarchyTest.*` (world = local × parent world, dirty propagation, reparenting and cycles, orphans of destroyed parents, render interpolation, and 100,000 entities updated on 4 job threads matching a single-threaded reference; TSan in CI).

See also: techstack.md §25, [Ids](Ids.md), [Memory](Memory.md), EnTT's [ECS docs](https://github.com/skypjack/entt/wiki/Entity-Component-System).
