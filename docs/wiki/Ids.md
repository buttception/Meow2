# IDs

Things that are **saved and referenced across runs** (entities, assets, scenes, prefabs) get a 128-bit ID. IDs sort by creation time, say what kind of thing they name, are valid standard UUIDs, and can be made on any machine and any thread without coordination.

- Headers: `#include <Meow2/Core/Id/Uuid.h>` (`Uuid`, `IdKind`, `EntityId`, `AssetId`, `SceneId`, `PrefabId`), `#include <Meow2/Core/Id/UuidGenerator.h>`.
- Namespace: `Meow2`.
- Status: ✅ M2 (#93). The first users are the scene (entities) and the asset registry.
- **Not for runtime-only objects:** entities use `entt::entity` handles while a scene runs (techstack.md §25), GPU resources use RHI handles (§11). IDs are for what's written to disk.

## Using them

```cpp
Meow2::UuidGenerator ids;                                  // one per app, created by the composition root, injected

const Meow2::EntityId player = ids.next<Meow2::IdKind::Entity>();
MEOW2_LOG_INFO(log, "Spawned {}", player.uuid());           // 019a4b1c-7d2e-8001-8040-123456789abc

// Reading one back from a file or a command:
Meow2::Result<Meow2::Uuid> parsed = Meow2::Uuid::parse(text);
Meow2::Result<Meow2::AssetId> asset = Meow2::AssetId::fromUuid(*parsed); // fails if it's not an asset ID
```

| API | What it does |
|---|---|
| `UuidGenerator()` / `UuidGenerator(node)` | random 54-bit node / a fixed one (tests) |
| `next(kind)`, `next<IdKind::K>()`, `next(kind, unixMs)` | a new ID now / typed / at a given time (tests, replays). Thread-safe |
| `Uuid::parse(text)`, `toString()` | text form `xxxxxxxx-xxxx-8xxx-xxxx-xxxxxxxxxxxx`, lowercase out, either case in |
| `kind()`, `unixMilliseconds()`, `isValid()` | what it names, when it was made, whether it's an engine ID |
| `high()`, `low()`, `Uuid::fromParts(high, low)` | store and restore as two integers |
| `EntityId::fromUuid(uuid)` | checks the kind; returns an error for the wrong kind or an invalid ID |
| `std::hash`, `std::format("{}")`, `<`, `==` | unordered containers, logs, sorting |

`EntityId{}` (default) is the invalid ID, e.g. for "no parent".

## The layout

An RFC 9562 **UUIDv8**: v8 is the standard's layout for custom fields, so every tool (databases, parsers, diff viewers) accepts these as UUIDs.

```
 bit 127                                                                                     bit 0
| unix time, ms (48) | version = 8 (4) | sequence (12) | variant = 0b10 (2) | kind (8) | node (54) |
```

- **Time first**, so IDs sort by creation time: scene files list entities in a stable order and git diffs stay small. 48 bits of milliseconds last until the year 10889.
- **Sequence:** 4096 IDs per millisecond per generator; more spill into the next millisecond.
- **Kind:** any ID found in a file or a log says what it refers to, and `TypedId` makes mixing kinds a compile error.
- **Node:** 54 random bits instead of Snowflake's assigned worker ID, so two PCs, two editors or several AI agents can create IDs in the same project without anyone handing out numbers. Two IDs only collide if they share the millisecond, the sequence and all 54 node bits.

## How the generator works

Time and sequence are kept as **one 64-bit number**, `ms << 12 | sequence`, in a `std::atomic`. Each `next()`:

1. computes `now << 12` (this millisecond, sequence 0);
2. takes the larger of that and `last + 1`;
3. stores it with **compare-and-swap**; if another thread got there first, it retries with the new `last`.

That one rule gives every property:
- IDs from one generator are strictly increasing, on every thread.
- If the system clock is set back, `last + 1` wins, so IDs keep increasing (with the old time) instead of repeating.
- Sequence 4095 + 1 carries into the millisecond field, so a burst of more than 4096 IDs runs slightly ahead of the clock instead of failing.
- No lock: millions of IDs per second, far more than entity creation needs.

## Why not …

| Option | Why not |
|---|---|
| UUIDv4 (random) | no order, so files and diffs shuffle; no room for a kind |
| UUIDv7 (time + random) | ordered, but no kind field |
| 64-bit Snowflake | needs a unique worker ID per machine, which someone must hand out; few spare bits |
| Hash of a name or path | changes when the asset is renamed or moved, breaking every reference |
| A counter | not unique across runs or machines |

Imported assets will keep their ID in a `.meta` file next to the source file, so renaming keeps references intact.

**Not secret:** IDs contain their creation time and are predictable. Never use one as a token (the MCP token is separate random data).

## Tests

`Core.Uuid.*` (bit layout, text round trip in both cases, rejected text including a v4 UUID and kind 0, hashing and formatting), `Core.UuidGenerator.*` (more than 4096 IDs in one millisecond, the clock set back, sorting by time, 4 threads × 20000 IDs all unique and increasing per thread, random nodes), `Core.TypedId.KindIsChecked`. The TSan CI job runs the thread test.

See also: techstack.md §24, [RFC 9562](https://www.rfc-editor.org/rfc/rfc9562).
