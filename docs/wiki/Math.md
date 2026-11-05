# Math

The engine's math is built on Microsoft's [DirectXMath](https://github.com/microsoft/DirectXMath) (MIT, header-only, SIMD on x64 and ARM). `Meow2/Math` adds the engine's vocabulary on top: storage types, the bridge to SIMD registers, the coordinate-system constants, tolerant comparisons and readable output.

- Header: `#include <Meow2/Math/Types.h>`. Namespace `Meow2::Math`.
- Library: DirectXMath **`may2026`** (submodule `external/DirectXMath`). Its types may appear in public `Meow2/Math` headers (techstack.md §3).
- Status: 🟨 M3. Types and helpers (#103), the AVX2 baseline, CPU check and batch helpers (#105), scalar helpers (#107), random numbers (#109), colours (#111), quaternions (#113), transforms (#115), easing and splines (#117), projections (#119) done; camera and more are next ([milestones](../../milestones.md)).

## Store compact, compute in registers

DirectXMath separates two kinds of types, and so does the engine:

| | Types | Where |
|---|---|---|
| **Storage** | `Vec2` (8 bytes), `Vec3` (12), `Vec4` (16), `Quat` (16), `Mat4` (64), `Mat3x4` (48) | components, files, arrays, function parameters that are stored |
| **Computation** | `DirectX::XMVECTOR`, `DirectX::XMMATRIX` | local variables inside maths code: they live in SIMD registers (4 floats at once) |

`load()` turns a storage value into a register; `storeVec3()`, `storeQuat()`, `storeMat4()`, … turn the result back:

```cpp
using namespace Meow2::Math;

Vec3 position{1.0F, 2.0F, 3.0F};
const DirectX::XMVECTOR moved = DirectX::XMVectorAdd(load(position), load(c_forward));
position = storeVec3(moved);                     // (1, 2, 4)
MEOW2_LOG_INFO(log, "Player at {}", position);    // "Player at (1, 2, 4)"
```

Why two kinds? A register holds 4 floats and must be 16-byte aligned. Storing every position as 4 aligned floats would waste a third of the memory, and memory bandwidth, not arithmetic, limits code that walks thousands of entities. So data is stored compact and loaded into registers for the work (2–3 instructions for a `Vec3`).

## The types

- **`Vec2`, `Vec3`, `Vec4`** default to **zero**: no uninitialised garbage in components.
- **`Quat`** is a rotation (x, y, z, w with w the real part). It defaults to the **identity** (no rotation) and is **its own type**: a `Vec4` can't be passed where a rotation is expected. It loads into an `XMVECTOR` for DirectXMath's `XMQuaternion*` functions.
- **`Mat4`** defaults to the identity. **Row-major with row vectors**, DirectXMath's convention: a point is transformed as `p' = p × M`, and `A × B` means "first A, then B".
- **`Mat3x4`** is an affine transform without its constant last column (0, 0, 0, 1): 48 bytes instead of 64, for world transforms and GPU data.

Each type derives from its DirectXMath counterpart (`Vec3` from `XMFLOAT3`, …), so every DirectXMath function that takes one accepts ours. The derived types add the safe defaults and keep `Quat` distinct.

## Coordinate system

**Left-handed: +X right, +Y up, +Z forward** (into the screen for an unrotated camera), techstack.md §10:

| Constant | Value |
|---|---|
| `c_right` | (1, 0, 0) |
| `c_up` | (0, 1, 0) |
| `c_forward` | (0, 0, 1) |
| `c_pi`, `c_twoPi`, `c_halfPi` | π, 2π, π/2 as `float` |

In a left-handed system, right × up = forward; `Math.Constants.LeftHanded` checks exactly that, so a convention mistake fails a test rather than mirroring a scene.

## Comparing floats

Floats rarely come out exactly equal after maths, so tests and code compare with `nearlyEqual(a, b, tolerance = 1e-5)`:
- the tolerance is **absolute near zero** and **relative for large values**: `|a − b| ≤ tolerance × max(1, |a|, |b|)`. Near zero relative error is meaningless; far from zero, float steps are larger;
- overloads compare `Vec2/3/4`, `Quat` and `Mat4` component by component;
- **`q` and `−q` are the same rotation** but compare as different. Compare rotations by angle when that matters (quaternion helpers come next).

## Output

`std::format("{}", value)`, and so every log macro, writes `(1, 2.5, -3)`, `quat(0, 0, 0, 1)`, `[(1, 0, 0, 0), (0, 1, 0, 0), …]`. Tests get the same output through GoogleTest printers (`tests/Math/MathPrinters.h`).

## Linux: the `sal.h` shim

DirectXMath includes `sal.h`, Microsoft's source annotations (`_In_`, `_Out_`, …), which only the Windows SDK has. On GCC and Clang, `Meow2Engine/shims/sal.h` defines the ten annotations DirectXMath uses as nothing, and CMake adds that folder to DirectXMath's include path only off Windows. If a DirectXMath update uses a new annotation, the Linux build fails on it: add it to the shim.

## SIMD

**SIMD** (single instruction, multiple data): one instruction works on several numbers at once, e.g. 4 floats with SSE, 8 with AVX2. DirectXMath uses it for everything, so the question is only *which* instructions the build may use.

### The baseline: AVX2

The whole build targets **AVX2 + FMA3 + F16C** on x64 (owner decision, techstack.md §10). That covers Intel CPUs since 2013 and AMD since 2015. FMA3 is fused multiply-add, one instruction for `a × b + c` (all of matrix maths); F16C converts half floats in hardware (vertex packing).

| CMake option | Value | Effect |
|---|---|---|
| `MEOW2_SIMD` | `AVX2` (default) | MSVC `/arch:AVX2`, GCC/Clang `-mavx2 -mfma -mf16c`; DirectXMath turns on its AVX2, FMA3 and F16C paths |
| | `SSE4.2` | for older CPUs (every PC that runs Windows 11 24H2 has it): `-msse4.2`, DirectXMath's SSE4 paths |

On ARM (Android later) the option is ignored: NEON is the default there.

**Every target gets the flags, third-party libraries too.** An inline function (DirectXMath's, or any template) compiled with different instruction sets in different files is still *one* function to the linker, which keeps one copy. If it kept the AVX2 copy, code built for SSE would crash on an old CPU anyway (an ODR violation). So `cmake/Meow2Simd.cmake` sets the flags once, before any library is added.

### The startup check

A program built for AVX2 that starts on an older CPU dies with "illegal instruction" the first time it reaches an AVX2 instruction, with no hint why. So every app's `main` starts with:

```cpp
if (!Meow2::checkCpuFeatures("Meow2Player"))
{
    return 1;   // the user saw: "Meow2Player can't run on this computer: its processor doesn't support AVX2, FMA3. …"
}
```

- It asks the CPU (`CPUID`) **and the OS** (`XGETBV`): a CPU with AVX under an OS that doesn't save the wider registers on a thread switch can't use AVX either.
- The message goes to stderr and, on Windows, a **message box**, because Release builds have no console window.
- **This code is compiled without the AVX2 flags.** It lives in its own small library, `Meow2CpuCheck` (`Meow2CpuCheck/`), whose folder CMake adds before the flags are set. It also avoids C++ standard-library templates (`std::string`…), whose functions would otherwise be shared with the AVX2-compiled rest of the program.
- `detectCpuFeatures()`, `requiredCpuFeatures()` and `describeMissingCpuFeatures()` are public too (`<Meow2/Core/CpuFeatures.h>`), e.g. for a crash report or a system-info screen.

The one remaining gap: C++ code that runs *before* `main` (static initialisers in AVX2-compiled files) could still hit an AVX2 instruction first. Engine code keeps those trivial; it's documented in the header.

### Batch helpers

`#include <Meow2/Math/Batch.h>`: maths over whole arrays, wrapping DirectXMath's *stream* functions, which run tight SIMD loops (AVX2 paths in this build) without copies or allocations.

```cpp
std::vector<Vec3> world(local.size());
transformPoints(local, load(modelMatrix), world);                // with translation and perspective divide
transformDirections(normalsIn, load(modelMatrix), normalsOut);   // rotation and scale only
const std::optional<MinMax> bounds = computeMinMax(local);        // the box around the points
```

- Input and output must not overlap.
- For very large arrays, split the range across jobs (`JobSystem::parallelFor`) and call the helper per range.
- Frustum culling batches come with the culling feature.

## Scalar helpers

`#include <Meow2/Math/Scalar.h>`: small `float` functions used everywhere. For whole vectors, DirectXMath has `XMVectorLerp`, `XMVectorClamp` and `XMVectorSaturate`.

| Function | Does | Edge behaviour |
|---|---|---|
| `toRadians(deg)`, `toDegrees(rad)` | angle units | `constexpr` |
| `lerp(a, b, t)` | `a` at t = 0 to `b` at t = 1 | exact at both ends (`(1−t)·a + t·b`); extrapolates outside 0..1 |
| `inverseLerp(a, b, v)` | where `v` lies between `a` and `b` (0..1) | not clamped; `a == b` asserts |
| `remap(v, inMin, inMax, outMin, outMax)` | one range onto another | not clamped; reversed ranges work |
| `clamp(v, lo, hi)`, `saturate(v)` | limit to a range / to 0..1 | NaN stays NaN |
| `wrapAngle(rad)` | same direction in **[−π, π)** | π becomes −π |
| `smoothstep(e0, e1, v)` | HLSL's S-curve `3t² − 2t³` | 0 below `e0`, 1 above `e1`, zero slope at both |

```cpp
heading = wrapAngle(heading + turnRate * dt);                       // never grows without limit
const float fade = smoothstep(fadeStart, fadeEnd, distanceToCamera); // 0 → 1 without a visible jump
```

Why our own `lerp` rather than `std::lerp`: the standard one guarantees monotonicity, which costs branches in every call; ours is exact at both ends, which is what animation and interpolation need.

**Linux pitfall:** libstdc++ also makes `std::lerp` visible as a global `::lerp`. So after `using namespace Meow2::Math;` at global or function scope, `lerp(1.0F, 2.0F, t)` is ambiguous on GCC and Clang (MSVC compiles it). Write `Math::lerp`, or call it from code inside `namespace Meow2`. The overloads for colours and transforms aren't affected.

## Random numbers

`#include <Meow2/Math/Random.h>`: `Random`, a **PCG32** generator with the engine's own distributions. **One seed gives the same sequence, bit for bit, on every platform and compiler**: replays, tests, procedural levels and bug reports ("seed 1234 crashes") reproduce everywhere.

```cpp
Random rng{levelSeed, threadIndex};              // seed + stream
const int damage = rng.range(5, 10);             // 5..10, both included, no bias
const float delay = rng.range(0.5F, 2.0F);       // [0.5, 2)
const Vec3 direction = rng.unitVector();         // uniform over the sphere
if (rng.chance(0.1F)) { dropRareItem(); }
Random fresh = Random::fromEntropy();            // different every run; log fresh.seed() to reproduce it
```

| Function | Result |
|---|---|
| `nextU32()`, `nextU64()` | raw bits |
| `nextFloat()` | [0, 1), 2²⁴ evenly spaced values, exact |
| `range(int, int)` | inclusive, unbiased (Lemire's method), full `int32` range works |
| `range(float, float)` | [min, max) |
| `chance(p)` | `true` with probability `p` |
| `pointInSphere()`, `unitVector()` | uniform inside / on the unit sphere |

- **Never global.** Each system or job thread owns its generator. A shared one would make the AI's numbers depend on how many particles spawned this frame. The **stream** (the second constructor argument) gives each thread or system its own sequence from one game seed.
- **Why not `std::mt19937` + `std::uniform_int_distribution`?** The engines are specified exactly, but the *distributions* aren't: MSVC and libstdc++ turn the same bits into different numbers. Ours use only integer maths and correctly rounded float operations (no `sin`/`cos`, whose results differ between C libraries).
- **No implicit FMA:** GCC and Clang could fuse `a * b + c` into one instruction with a different rounding than MSVC; `-ffp-contract=off` (in `cmake/Meow2Simd.cmake`) keeps float results identical. `Math.Random.GoldenValues` checks exact bits on Windows and Linux CI.
- `Random` works with `<random>` and `std::shuffle`, but those results are platform-dependent again.
- **Not for security:** a few outputs reveal the state. Tokens come from the OS.

## Colours

`#include <Meow2/Math/Color.h>`. Screens and image files store colours **gamma-encoded (sRGB)**: the 256 steps of a byte are spaced to look even to the eye, so they are *not* even in light intensity. sRGB 128 is only about 21 % of the light of 255. Blending, lighting and averaging are only correct on **linear** values, so the engine has two colour types:

| Type | Encoding | Use |
|---|---|---|
| `Color` | linear float RGBA, opaque black by default; values above 1 are valid (HDR) | maths, lighting, blending, shader constants |
| `Color8` | sRGB-encoded RGB bytes, linear alpha byte (RGBA8 image layout) | colour pickers, UI themes, image data, files |

```cpp
const Color tint = toLinear(fromHex(0xFF8000FFU));    // a designer's orange, decoded to linear
const Color mixed = lerp(tint, Colors::c_white, 0.5F); // blend in linear space
const Color8 bytes = toColor8(mixed);                  // encode back to sRGB bytes
```

- `srgbToLinear` / `linearToSrgb` use the exact sRGB curve (IEC 61966-2-1): a straight segment near black, then a 2.4 power. `toColor8(toLinear(c)) == c` for **every** byte value.
- **Why it matters:** a 50 % blend of red and green in linear space is a bright yellow (sRGB 188, 188, 0). Done on sRGB bytes it is a dark olive (128, 128, 0), the "muddy blend" bug.
- `toHsv` / `fromHsv`: hue in degrees [0, 360), saturation and value 0..1, for colour pickers (apply it to sRGB values, the ones the picker shows).
- `premultiply` / `unpremultiply`: RGB × alpha. Premultiplied colours filter and blend correctly (no dark fringes when a sprite is scaled).
- `Colors::c_white`, `c_black`, `c_grey` (sRGB 128, which *looks* middle grey), `c_red`, `c_green`, `c_blue`, `c_yellow`, `c_cyan`, `c_magenta`, `c_transparent`.
- `load(Color)` / `storeColor()` for DirectXMath's `XMColor*` functions; `std::format` gives `rgba(1, 0.5, 0, 1)` and `#ff8000c0`.

## Quaternions

`#include <Meow2/Math/Quaternion.h>`: a unit quaternion stores a rotation of *angle* about a unit *axis* as `(axis·sin(angle/2), cos(angle/2))`. It has no gimbal lock, interpolates smoothly, takes 16 bytes and stays a pure rotation (just `normalize` it now and then). **`q` and `−q` are the same rotation.**

**Direction of positive angles** (left-handed, DirectXMath's rule): clockwise seen from the axis's positive end. So:
- a positive turn about up turns forward to the **right**;
- a positive pitch tilts forward **down**;
- a positive roll turns right towards **up**.

| Function | Does |
|---|---|
| `fromAxisAngle(axis, angle)`, `toAxisAngle(q)` | axis-angle ↔ quaternion (angle 0..π, the short way) |
| `fromEuler({pitch, yaw, roll})`, `toEuler(q)` | Euler angles, applied **roll (Z) → pitch (X) → yaw (Y)** |
| `lookRotation(forward, up = c_up)` | turn +Z to `forward`, keep +Y up (never rolls) |
| `fromToRotation(a, b)` | the shortest rotation from direction `a` to `b` |
| `combine(first, then)` | apply `first`, then `then` |
| `inverse(q)`, `normalize(q)`, `rotate(q, v)` | undo, re-normalise, apply to a vector |
| `slerp(a, b, t)` | constant angular speed, the short way round |
| `angleBetween(a, b)`, `nearlySameRotation(a, b)` | compare rotations (treats `q` and `−q` as equal) |

```cpp
const Quat aim = lookRotation(target - eye);              // face the target, stay upright
const Quat turned = combine(current, fromAxisAngle(c_up, yawSpeed * dt));
const Vec3 angles = toEuler(rotation);                    // for the inspector: degrees via toDegrees()
```

- **Why that Euler order?** Yaw turns about the world's up, pitch then tilts about the already-turned right axis, and roll banks about the view direction. That is how a camera or a character moves, and it is DirectXMath's `RollPitchYaw` order.
- **Gimbal lock** in `toEuler`: looking straight up or down, yaw and roll turn about the same axis, so roll comes back as 0 and yaw holds the whole turn. The rotation itself is unchanged.
- **`combine`, not `operator*`:** the order of quaternion products is a classic bug source (`XMQuaternionMultiply(a, b)` applies `a` first, while maths notation `b·a` means the same thing). A name that says the order avoids it.

## Transform

`#include <Meow2/Math/Transform.h>`: `Transform` is a position, a rotation and a scale (TRS), the identity by default. It is a plain data struct, so it's ready to be a component.

A point is **scaled, then rotated, then moved**: `world = rotate(rotation, local × scale) + position`, which is the matrix `S × R × T`.

```cpp
Transform door{.position = {2.0F, 0.0F, 5.0F}, .rotation = fromAxisAngle(c_up, toRadians(30.0F))};
const Vec3 handle = transformPoint(door, Vec3{0.9F, 1.0F, 0.0F});   // local → world
const Transform wheelInWorld = combine(wheelLocal, car);              // child, then parent
const Transform shown = lerp(previous, current, alpha);               // render(alpha): lerp + slerp
const DirectX::XMMATRIX world = toMatrix(shown);
```

| Function | Does |
|---|---|
| `toMatrix`, `toMat4`, `toMat3x4`, `fromMatrix` | to and from matrices; `fromMatrix` returns `std::nullopt` for a flattened (zero-scale) matrix |
| `transformPoint`, `inverseTransformPoint` | local ↔ parent space |
| `transformDirection` | rotation only, so unit directions stay unit |
| `combine(child, parent)`, `inverse(t)` | hierarchies; undo a transform |
| `lerp(a, b, t)` | position and scale lerped, rotation slerped |
| `forward`, `right`, `up` | local +Z, +X, +Y in the parent's space |

- **`WorldPosition`** is the type of `position`, a `Vec3` today. Every world position uses this one name, so larger worlds can later move to doubles or a floating origin in one place (techstack.md §10).
- **No shear:** a transform can't represent a rotated child under a parent that scales non-uniformly (e.g. (2, 1, 1)); in reality that child would be sheared. `combine` and `inverse` are exact for uniform parent scales. `inverseTransformPoint` and the matrices are exact in every case.

## Easing and splines

### Easing

`#include <Meow2/Math/Easing.h>`: an easing curve maps progress `t` (0..1, linear in time) to eased progress. A panel then slides in fast and settles gently instead of moving at constant speed.

```cpp
const float t = saturate(elapsed / duration);
panel.x = lerp(hiddenX, shownX, ease(EaseCurve::Cubic, EaseMode::Out, t));
```

- **Curves:** `Linear`, `Quad`, `Cubic`, `Quart`, `Sine`, `Expo`, `Circ`, `Back` (pulls back first), `Elastic` (springy), `Bounce`. The formulas are the ones shown on [easings.net](https://easings.net).
- **Modes:** `In` (slow start), `Out` (slow end), `InOut` (both). Each curve is defined once in its *In* form. *Out* is its mirror `1 − in(1 − t)`, and *InOut* uses *In* for the first half and *Out* for the second.
- Two small enums instead of 30 functions, so a component, a config file or the editor can store the choice.
- Every curve gives 0 at t = 0 and 1 at t = 1; `Back` and `Elastic` leave 0..1 in between. `t` outside 0..1 is clamped.

### Splines

`#include <Meow2/Math/Spline.h>`:

| Curve | Passes through | Shaped by | Use |
|---|---|---|---|
| `catmullRom(p0, p1, p2, p3, t)` | every point (`p1` → `p2`) | its neighbours | paths through waypoints, camera rails |
| `bezier(p0, p1, p2, p3, t)`, `bezierTangent` | first and last | two handles | hand-shaped curves, editor handles |
| `hermite(p0, m0, p1, m1, t)` | both ends | explicit end tangents | animation curves, joining pieces smoothly |
| `catmullRom(q0, q1, q2, q3, t)` on `Quat` | every key | neighbours (squad) | smooth rotation through keys |

**Moving at constant speed: `SplinePath`.** The `t` of a curve is not distance: the curve moves faster where its points are far apart, so advancing `t` evenly looks jerky. `SplinePath` builds a Catmull-Rom path through any number of points and an **arc-length table** (the length so far at 32 samples per segment). `pointAtDistance(metres)` then looks up `t` by binary search and refines it with one Newton step. Equal distance steps come out equally spaced within 0.2 %.

```cpp
const SplinePath rail{waypoints};
distance = std::min(distance + speed * dt, rail.length());
camera.position = rail.pointAtDistance(distance);
camera.rotation = lookRotation(rail.tangentAtDistance(distance));
```

## Projections

`#include <Meow2/Math/Projection.h>`. The conventions (techstack.md §10):
- left-handed view space (+Z into the screen);
- depth 0..1 with **reversed Z**: the near plane maps to 1, the far plane or infinity to 0;
- a `D32_FLOAT` depth buffer, cleared to 0 and compared with `GREATER_EQUAL`.

| Function | Depth |
|---|---|
| `perspectiveReversedInfiniteLH(fovY, aspect, near)` | `near / z`: 1 at near, 0.5 at twice the distance, → 0 at infinity. **The default.** |
| `perspectiveReversedLH(fovY, aspect, near, far)` | 1 at near, 0 at far (shadow cascades, probes) |
| `orthographicReversedLH(width, height, near, far)` | linear, 1 at near, 0 at far |
| `viewDistanceFromDepth(depth, near)` | back from a depth value to a distance (fog, SSAO) |

**Why reversed Z?**
- A float has most of its precision near 0.
- Perspective division crowds depth values near the near plane.
- Storing `near / z` puts the float's fine steps exactly where the projection's coarse ones are, so the two errors cancel.
- The result: nearly uniform precision out to infinity, so there's **no far plane** to tune and no z-fighting on distant mountains.

**Vulkan and the Y axis.** In NDC, +Y is up; framebuffer rows count down from the top on both APIs. D3D12's viewport rule flips Y (`screenY = y + (1 − ndcY)·h/2`) and Vulkan's doesn't (`y + (1 + ndcY)·h/2`). With the same matrix, Vulkan's image would be upside down. `flipForVulkan(viewport)` returns the viewport with its origin on the bottom edge and a **negative height** (core since Vulkan 1.1). Every point then lands on the same pixel as on D3D12, and both backends share every matrix. `Math.Projection.VulkanYFlip` checks it.

## Tests

`Math.Types.Layout` (sizes, defaults, `Quat` ≠ `Vec4`), `Math.LoadStore.RoundTrip`, `Math.Constants.LeftHanded`, `Math.NearlyEqual.AbsoluteAndRelative`, `Math.Format.ReadableOutput`, `Math.Simd.IntrinsicsOn`, `Math.Scalar.*` (angle conversion and wrapping, lerp and its inverse, remap, clamp, smoothstep), `Math.Random.*` (PCG reference sequence, golden values, bounds, bias, sphere), `Math.Color.*` (sRGB curve, exact byte round trip, hex, HSV, premultiplied alpha, blending), `Math.Quaternion.*` (axis-angle, Euler order and round trip, look and from-to rotations, combine order, slerp, angle between), `Math.Transform.*` (identity, matrix round trip, scale-rotate-translate order, combine vs matrices, inverse, interpolation, directions), `Math.Easing.*` (endpoints, known values, mirrored modes), `Math.Spline.*` (curves hit their points, rotation squad, constant-speed path), `Math.Projection.*` (hand-checked infinite perspective, finite vs DirectXMath, orthographic, depth linearisation, Vulkan Y-flip).

See also: techstack.md §10, [DirectXMath programming guide](https://learn.microsoft.com/windows/win32/dxmath/directxmath-portal).
