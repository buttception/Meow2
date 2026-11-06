# Shaders

Shaders are written in HLSL and compiled with [DXC](https://github.com/microsoft/DirectXShaderCompiler) to DXIL (D3D12) and SPIR-V (Vulkan).

- Status: 🟨 M4. DXC dependency (#135) done.

## DXC

The compiler is a pinned Microsoft release, **v1.9.2609**, not a submodule, and not whatever a machine has installed. Configuring downloads it into `build/_dxc/<version>/`, once per checkout, and checks its SHA-256 (`cmake/Meow2Dxc.cmake`). So every developer and CI job compiles with the same version, and cooked shaders don't depend on the machine.

| Option | Effect |
|---|---|
| `MEOW2_SHADER_TOOLS` (ON) | `OFF`: no download and no way to compile shaders; cooked ones still load |
| `MEOW2_DXC_DIR` | use an already unpacked release, e.g. on a machine without internet |

To change the version, update `meow2DxcVersion`, the two archive names and the two hashes in `cmake/Meow2Dxc.cmake`, in its own PR.

**DXC isn't in the release zip.** It's for development. `Meow2Player` will only ever load cooked shaders. Shipping it next to Renderer and Editor for hot reload waits on a licence question: the release's `LICENSE-MS.txt` allows distributing the files in a "distributables file list", and the release doesn't contain one.

**DXC doesn't optimise for a GPU.** DXIL and SPIR-V are portable; the GPU driver compiles them to machine code for the exact GPU when a pipeline is created, on the player's machine.

## Tests

`Build.Dxc.PinnedCompilerRuns` runs the downloaded compiler.

See also: techstack.md §4, §12.
