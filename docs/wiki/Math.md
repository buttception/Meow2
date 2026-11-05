# Math

The engine's math is built on Microsoft's [DirectXMath](https://github.com/microsoft/DirectXMath) (MIT, header-only, SIMD on x64 and ARM). `Meow2/Math` adds the engine's vocabulary on top: storage types, the bridge to SIMD registers, the coordinate-system constants, tolerant comparisons and readable output.

- Header: `#include <Meow2/Math/Types.h>`. Namespace `Meow2::Math`.
- Library: DirectXMath **`may2026`** (submodule `external/DirectXMath`). Its types may appear in public `Meow2/Math` headers (techstack.md §3).
- Status: 🟨 M3. Types and helpers done (#103); SIMD batches, random numbers, colours, quaternions, transforms, camera and more are next ([milestones](../../milestones.md)).

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

On x64 DirectXMath uses SSE intrinsics (checked by `Math.Simd.IntrinsicsOn`), and NEON on ARM. The **AVX2** baseline (owner decision) with FMA3 and F16C, the CPU check at startup and the batch helpers for thousands of items are the next M3 feature.

## Tests

`Math.Types.Layout` (sizes, defaults, `Quat` ≠ `Vec4`), `Math.LoadStore.RoundTrip`, `Math.Constants.LeftHanded`, `Math.NearlyEqual.AbsoluteAndRelative`, `Math.Format.ReadableOutput`, `Math.Simd.IntrinsicsOn`.

See also: techstack.md §10, [DirectXMath programming guide](https://learn.microsoft.com/windows/win32/dxmath/directxmath-portal).
