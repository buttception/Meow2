# Scene

A `Scene` is one world of **entities** and **components**, built on [EnTT](https://github.com/skypjack/entt) v3.16.0. Every entity has a fast runtime handle (`entt::entity`) for systems, and a saved identity (`EntityId`, see [Ids](Ids.md)) for files, the editor, undo and MCP.

- Header: `#include <Meow2/Scene/Scene.h>`. Namespace `Meow2::Scene`.
- EnTT's types (`entt::entity`, the registry, views) are part of the API on purpose: systems iterate EnTT views directly.
- Status: ✅ M2 (#95, command buffers and transient entities #97). Transforms come with M3's math.

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

`Scene.SceneTest.*`: IDs and names on create, find by ID, destroy, loading with saved IDs (duplicates and invalid IDs rejected), views, pools that exist before first use. `Scene.SceneMemory.CountedUnderSceneTag`. `Core.TaggedResource.CountsUnderTag` checks the memory resource. Part 2: `Scene.SceneTest.TransientHasNoId`, `RegistryDestroyKeepsMapInSync`, `RemovingIdMakesEntityTransient`, `IsRegistered`; `Scene.SceneCommandsTest.*` (spawn appears on apply, destroy/emplace/remove in order, stale handles skipped); `Scene.SceneCommandsJobs.JobsRecordInParallel` (4 job threads record changes for 10,000 entities at once; TSan checks it in CI).

See also: techstack.md §25, [Ids](Ids.md), [Memory](Memory.md), EnTT's [ECS docs](https://github.com/skypjack/entt/wiki/Entity-Component-System).
