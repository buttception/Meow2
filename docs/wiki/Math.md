# Math

The engine's math is built on Microsoft's [DirectXMath](https://github.com/microsoft/DirectXMath) (MIT, header-only, SIMD on x64 and ARM). `Meow2/Math` adds the engine's vocabulary on top: storage types, the bridge to SIMD registers, the coordinate-system constants, tolerant comparisons and readable output.

- Header: `#include <Meow2/Math/Types.h>`. Namespace `Meow2::Math`.
- Library: DirectXMath **`may2026`** (submodule `external/DirectXMath`). Its types may appear in public `Meow2/Math` headers (techstack.md §3).
- Status: 🟨 M3. Types and helpers (#103), the AVX2 baseline, CPU check and batch helpers (#105), scalar helpers (#107) done; random numbers, colours, quaternions, transforms, camera and more are next ([milestones](../../milestones.md)).

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

## Tests

`Math.Types.Layout` (sizes, defaults, `Quat` ≠ `Vec4`), `Math.LoadStore.RoundTrip`, `Math.Constants.LeftHanded`, `Math.NearlyEqual.AbsoluteAndRelative`, `Math.Format.ReadableOutput`, `Math.Simd.IntrinsicsOn`, `Math.Scalar.*` (angle conversion and wrapping, lerp and its inverse, remap, clamp, smoothstep).

See also: techstack.md §10, [DirectXMath programming guide](https://learn.microsoft.com/windows/win32/dxmath/directxmath-portal).
