# Scene files (`.m2scene`)

A scene's saved entities and components, as a text file that merges in git, carries versions, and survives being opened by an app that doesn't know every component in it.

- Code: `Meow2/Assets/SceneFile.h` (reading and writing), `Meow2/Scene/ComponentRegistry.h` (what components look like), `Meow2/Scene/Environment.h`.
- Status: 🟨 M7 PR 3, engine half (#218). Meow2Renderer opens and saves them; so does the editor (#222).

## What a file looks like

```toml
# Meow2 scene. Entities are sorted by ID and each is one block, so edits to different entities merge.
format = "meow2.scene"
version = 1

[components]
Environment = 1
LocalTransform = 1
MeshRenderer = 2
Parent = 1

[settings.Environment]
sunElevation = 35.0
sunAzimuth = 30.0
sunColor = [1.0, 1.0, 1.0, 1.0]
sunIntensity = 3.0
exposure = 1.0

[[entity]]
id = "019a7c2e-41d0-8000-8140-3f2a9c11d5e1"
name = "Duck"

[entity.LocalTransform]
position = [0.0, 0.0, 0.0]
rotation = [0.0, 0.0, 0.0, 1.0]
scale = [1.0, 1.0, 1.0]
mobility = "auto"

[entity.MeshRenderer]
mesh = "019a7c2e-41cf-8000-8141-77e0b2a4c3d9"
```

- **`format` and `version`** say it's a scene and which layout of the file it uses. A file from a newer layout is refused ("saved by a newer Meow2") rather than misread. The layout version changes rarely; components carry their own versions.
- **`[components]`** lists each component used in the file once, with its version (scene settings too).
- **`[settings.<Setting>]`**: the scene's settings, one table each (today `Environment`: the sun and the exposure). See [Scene settings](#scene-settings).
- **`[[entity]]`** blocks, one per saved entity: its `id` (an `EntityId`, [Ids](Ids.md)), its `name`, then a table per component, `[entity.<Component>]`, with its fields.
- **References** are IDs as text: a mesh is its mesh `AssetId`, a parent is the parent's `EntityId`. `""` means none.
- **Transient entities** (no `EntityId`: runtime helpers) aren't saved; a reference to one is saved as `""`.

## Scene settings

Some things belong to the scene, not to an entity: today the `Environment` (the sun's angles, colour and strength, and the exposure). The registry describes it like a component, marked `setting` (`ComponentDesc::setting`), so files, the editor's Scene settings dock and agents read and write its fields the same way; it lives in the scene's registry context (`Scene::environment()`, `setEnvironment()`), not on an entity, and is written as `[settings.Environment]` before the entities.

- **Older files** kept it on an `Environment` entity. Reading one folds that component into the setting, and removes the entity if it held nothing else (`SceneReadReport::entities` counts what stays).
- An entity's component that's a setting is never written; `component.add` refuses it.

## Mobility

Whether something moves at runtime is `LocalTransform::mobility` (`auto`, `static`, `movable`): `movable` meshes are drawn into the sun's shadow every frame, the rest into its cache; `auto` counts as static until M14 infers it. `MeshRenderer` had a `movable` flag of its own until version 2; the migration drops it (a migration sees one component, so it can't move the flag to the transform), and Meow2Renderer marks animated nodes `movable` each time it loads them.

## Why it merges

Two people editing one scene in git is the reason the file looks like this (owner requirement).

- **One block per entity, in ID order.** IDs start with their creation time, so the order never changes when entities are edited, and new entities are added at the end. An edit changes lines inside one block only (`Assets.SceneFile.EditingOneEntityChangesOnlyItsLines` checks it), so edits to different entities land in different hunks and git merges them by itself.
- **Deterministic output.** The same scene always gives the same text: components by name, fields in the order their component declares them, floats in their shortest exact form (`0.1`, not `0.10000000149011612`). Saving without a change rewrites nothing.
- **Our own writer.** toml++ reads the files, but writing goes through `writeScene()`, because a general TOML writer decides layout and order itself.
- Two people adding entities at the same time both append at the end: git shows that as a conflict of two added blocks, which is resolved by keeping both.

## Versions and migrations

Each component has a version. When its fields change, the version goes up and a **migration** says how to turn the old fields into the new ones:

```cpp
ComponentDesc speed = component<Speed>("Speed", 2, {field<Speed>("metresPerSecond", [](Speed& c) -> auto& { return c.metresPerSecond; })});
speed.migrations.push_back({.from = 1,
                            .up = [](FieldTable& t) { t["metresPerSecond"] = std::get<double>(t["kmh"]) / 3.6; t.erase("kmh"); },
                            .down = [](FieldTable& t) { t["kmh"] = std::get<double>(t["metresPerSecond"]) * 3.6; t.erase("metresPerSecond"); }});
```

- **Reading an older file:** each component is migrated up, one version at a time, before its fields are set.
- **Writing for an older build:** `SceneWriteOptions::componentVersions` names a component and an older version, and the down migrations produce the old fields. A teammate on last week's build can still open the file.
- **A component from a newer version** than this build knows can't be read safely, so it's kept unchanged (below) with a warning.
- **A renamed component** lists its old names as `aliases`: a file with the old name loads into it, and it's saved under the new name.
- **A field the component doesn't have** (removed, or from a newer build) is dropped with a warning; a field missing from the file keeps its default.

## Unknown components are kept

An app registers only the components it knows. Meow2Renderer knows transforms, cameras, meshes and the environment; the editor will know more (scripts, later). A component the reader doesn't know isn't dropped: it goes into an `UnknownComponents` component on its entity, with its name, version and fields, and is written back exactly as it was. Opening an editor scene in Meow2Renderer and saving it loses nothing (`Assets.SceneFile.UnknownComponentsSurvive`).

## Components describe their fields once

`ComponentRegistry` holds one `ComponentDesc` per component: its name and version in files, aliases, migrations, and its fields. Scene files use it now; the editor's inspector and the MCP schemas will use the same descriptions (M7 PR 4 and 6), so a field added once shows up everywhere.

```cpp
registry.add(component<Camera>("Camera", 1,
    {field<Camera>("projection", [](Camera& c) -> auto& { return c.lens.type; }),
     field<Camera>("verticalFov", [](Camera& c) -> auto& { return c.lens.verticalFov; }),
     field<Camera>("main", [](Camera& c) -> auto& { return c.main; })}));
```

- **A field's type comes from the C++ member** the lambda returns (bool, `std::int32_t`/`std::uint32_t`, float, `std::string`, `Vec3`, `Quat`, `Color`, `AssetId`, `entt::entity`, or an enum with `EnumNames`), so a description can't disagree with the struct. The lambda can reach nested members (`c.lens.type`).
- **Values in a shared form** (`FieldValue`: bool, integer, double, text, number array) are what files hold and what migrations edit.
- **Who registers what:** `registerSceneComponents()` (engine: `LocalTransform`, `Parent`, `Camera`, `Environment`), `Renderer::registerRenderComponents()` (`MeshRenderer`); each app's composition root makes the registry and calls those it needs.
- `registerAll(scene)` creates every registered component's storage up front ([Scene](Scene.md#rules): no pool may be created by a job).

## Reading never asserts

A file is input, not code, so nothing in it can trip an assert:

- **Errors, and nothing is added to the scene:** broken TOML (with line and column), not a scene, a newer format version, an entity without a valid ID, an ID used twice or already in the scene. Everything is read and checked before the first entity is created.
- **Warnings, and the rest loads:** a field with the wrong shape (the component keeps its defaults from there on), a parent that doesn't exist or has no transform, a chain of parents that loops back (the loop is cut), a component table that isn't a table.

## Meshes: stable IDs in the model's `.meta`

A `MeshRenderer` names a mesh by `AssetId`. For that to mean the same mesh next week, a model's mesh IDs must not change between runs:

- The first time a model is imported, `AssetManager::meshIds(model, count)` makes one ID per mesh and writes them into the model's `.meta`, next to its own ID: `meshes = ["…", "…"]`. From then on the same IDs come back, also in a new run after `scan()`.
- `AssetManager::findSubAsset(meshId)` says which model and which mesh an ID names.
- `Renderer::loadSceneModels()` (or its steps: `importSceneModels()`, `sceneInstanceCount()` to size the GPU scene, `uploadSceneModels()`) imports each model a scene uses, once, and uploads it with those IDs.
- Mesh *i* keeps its ID while the model keeps its mesh order. Re-exporting a model with meshes reordered would swap them; the editor's glTF import (editor half) will match meshes by name too.

## In Meow2Renderer

- **Open:** a `.m2scene` path in the Scenes window's ANY FILE field, or `renderer.scene = "path/to/Level.m2scene"`. The scene's folder is scanned for `.meta` files, so the models it uses must be in that folder or below.
- **Save:** SAVE AS .M2SCENE in the Scenes window. A model saves next to its file (`Duck.gltf` → `Duck.m2scene`); a scene file saves over itself. The Stats window's sun and exposure are saved in the scene's `Environment`, and opening a scene sets them back.
- Each folder Meow2Renderer opens from gets a read-write mount of its own, so a model's `.meta` can be written next to it. A folder that can't be written still opens, but can't be saved as a scene (its mesh IDs wouldn't last).
- Not saved yet: glTF animations and cameras (a scene file opens without them).

## In the editor

The editor opens, edits and saves scene files of its project folder (File → New, Open, Save, Save as; the Assets dock), with an unsaved-changes prompt and recent scenes, all through commands an agent can use too: see [Editor → Project and scenes](Editor.md#project-and-scenes). Importing a model there gives its meshes stable IDs the same way, so a scene saved in the editor opens in Meow2Renderer and the other way round.

## Tests

| Test | What it checks |
|---|---|
| `Scene.ComponentRegistryTest.FieldsReadAndWriteMembers` | descriptors read and write members, floats short, enums by name, wrong shapes refused |
| `Scene.ComponentRegistryTest.EntityReferencesUseIds` | references saved as EntityIds; transient ones as ""; unknown IDs refused |
| `Scene.ComponentRegistryTest.FindsByNameAndAlias`, `MigratesUpAndDown` | lookup by old names; migrations both ways, a missing step is an error |
| `Assets.SceneFileTest.RoundTripIsIdentical` | save → load → save gives the same text; IDs, names, hierarchy, fields kept |
| `Assets.SceneFileTest.EntitiesSortedAndOneBlockEach`, `EditingOneEntityChangesOnlyItsLines` | order, layout, merge-friendliness |
| `Assets.SceneFileTest.UnknownComponentsSurvive`, `NewerComponentVersionIsKept` | kept and written back unchanged |
| `Assets.SceneFileTest.OldVersionMigratesUp`, `WritesOlderVersion`, `RenamedComponentReadByAlias` | versions and names |
| `Assets.SceneFileTest.BadInputIsAnError` | errors add nothing; parents that don't resolve or loop are dropped with warnings |
| `Assets.AssetManagerTest.MeshIdsAreStable` | mesh IDs kept in the `.meta`, found again by a new scan |
| `RhiGpu*.GpuSceneTest.SceneFileLoadsTheModelsItUses` | a model saved as a scene opens with the same instance, through the `.meta`, on both backends |
| `RendererApp.SceneFiles.*` | Meow2Renderer's scene paths, folder mounts, sun ↔ Environment |

See also: techstack.md §25, [Scene](Scene.md), [Assets](Assets.md), [Renderer](Renderer.md).
