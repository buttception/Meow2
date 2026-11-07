# Assets

`#include <Meow2/Assets/AssetManager.h>`: finds assets by a stable ID and keeps one loaded copy of each.

- Status: ✅ M4 (0.5.0). Asset core (#139) done. Shaders are the first asset type; meshes and textures come with M7.

## Source, `.meta`, cooked file

An asset has up to three files:

| File | Example | Holds |
|---|---|---|
| Source | `shaders/Sky.hlsl` | what you edit |
| `.meta` sidecar | `shaders/Sky.hlsl.meta` | the `AssetId` and import settings, committed next to the source |
| Cooked file | `<AssetId>.m2shader` | what the game loads, named by the ID |

The ID lives in the `.meta`, so moving or renaming a source keeps every reference to it working, as long as the `.meta` moves with it. A packaged game has only the cooked files.

```toml
id = "01900000-0000-8000-8100-000000000001"
```

## Using it

```cpp
Assets::AssetManager assets{files, ids, log};                 // composition root
assets.scan("assets");                                        // read every .meta below a folder
Result<AssetId> id = assets.importSource("assets/Sky.hlsl");  // creates the .meta if it has none

Result<Assets::AssetHandle<Texture>> texture = assets.load<Texture>(*id);
std::shared_ptr<const Texture> pixels = texture->get();
assets.reload<Texture>(*id);                                  // every handle sees the new value
```

- A loadable type provides `static Result<T> loadAsset(AssetManager& assets, AssetId id)`. There's no asset base class and no virtual call.
- **One copy:** loading the same ID again returns the same value.
- **Reload:** `get()` returns the new value. Values already handed out stay valid until released. A failed reload keeps the old value, and `version()` tells you a reload happened.
- **Copied `.meta` files are errors:** two files with one ID would make references ambiguous, so `scan()` returns `AlreadyExists`.
- **Threads:** any thread may load. Nothing is locked during file reads. Two threads loading one asset may both build it, and the first to finish wins.
- **Memory:** loads run under `MemTag::Assets`.

## Tests

`Assets.AssetManagerTest.*`: `.meta` creation, nested scans, moves keep IDs, bad and duplicate `.meta` files, loading once, reloads, and jobs loading at once.

See also: techstack.md §13a, §24.

## Implementation summary

- `Meow2/Assets/AssetManager.h`: `scan`, `importSource`, `load<T>`, `reload<T>`, `AssetHandle<T>`; `T` provides a static `loadAsset()`.
