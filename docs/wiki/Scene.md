# Scene

A `Scene` is one world of **entities** and **components**, built on [EnTT](https://github.com/skypjack/entt) v3.16.0. Every entity has a fast runtime handle (`entt::entity`) for systems, and a saved identity (`EntityId`, see [Ids](Ids.md)) for files, the editor, undo and MCP.

- Header: `#include <Meow2/Scene/Scene.h>`. Namespace `Meow2::Scene`.
- EnTT's types (`entt::entity`, the registry, views) are part of the API on purpose: systems iterate EnTT views directly.
- Status: 🟨 M2 (#95), part 1. Next: **command buffers** so jobs can create and destroy entities. Transforms come with M3's math.

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

**Create and destroy through the Scene**, never with `registry().create()`/`destroy()`, which would bypass the ID map.

## Rules

- **Components are plain data structs** (no behaviour, no invariants), like events.
- **Register every component type at startup.** EnTT creates a type's storage the first time it's used. If that first use happened on a job thread while another thread iterates, it would be a data race. `registerComponent<T>()` creates it up front. `IdComponent` and `NameComponent` are registered by the Scene itself.
- **Threading:** the Scene belongs to the thread that created it (the main thread). `create`, `destroy` and `registerComponent` assert that. Jobs may read and write components in parallel when they touch *different entities* or *different component types* and nothing changes structure meanwhile. Structural changes from jobs will go through command buffers (part 2).

## Memory

All of the scene's memory (the registry, every component pool, the ID map) is counted under **`MemTag::Scene`**:
- `Memory::TaggedResource` forwards to the upstream resource (e.g. `globalTracker()`) inside a `MemTagScope{Scene}`.
- The registry's allocator is `Memory::ResourceAllocator<entt::entity>`. `std::pmr::polymorphic_allocator` doesn't work with EnTT: its `construct()` hands itself to every object it builds that takes an allocator, and EnTT's pools don't accept that extra argument. `ResourceAllocator` only allocates and frees.
- Strings *inside* components (a `NameComponent`'s name) use their own default allocator, counted under the thread's current tag.

The test `Scene.SceneMemory.CountedUnderSceneTag` creates 1000 entities against a `TrackingAllocator` and checks that everything lands on `Scene` and is all returned when the scene is destroyed.

## Tests

`Scene.SceneTest.*`: IDs and names on create, find by ID, destroy, loading with saved IDs (duplicates and invalid IDs rejected), views, pools that exist before first use. `Scene.SceneMemory.CountedUnderSceneTag`. `Core.TaggedResource.CountsUnderTag` checks the memory resource.

See also: techstack.md §25, [Ids](Ids.md), [Memory](Memory.md), EnTT's [ECS docs](https://github.com/skypjack/entt/wiki/Entity-Component-System).
