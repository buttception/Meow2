# Shaders

Shaders are written in HLSL and compiled with [DXC](https://github.com/microsoft/DirectXShaderCompiler) to DXIL (D3D12) and SPIR-V (Vulkan).

- Status: 🟨 M4. DXC dependency (#135), `ShaderCompiler` (#137) done.

## DXC

The compiler is a pinned Microsoft release, **v1.9.2609**, not a submodule, and not whatever a machine has installed. Configuring downloads it into `build/_dxc/<version>/`, once per checkout, and checks its SHA-256 (`cmake/Meow2Dxc.cmake`). So every developer and CI job compiles with the same version, and cooked shaders don't depend on the machine.

| Option | Effect |
|---|---|
| `MEOW2_SHADER_TOOLS` (ON) | `OFF`: no download and no way to compile shaders; cooked ones still load |
| `MEOW2_DXC_DIR` | use an already unpacked release, e.g. on a machine without internet |

To change the version, update `meow2DxcVersion`, the two archive names and the two hashes in `cmake/Meow2Dxc.cmake`, in its own PR.

**DXC isn't in the release zip.** It's for development. `Meow2Player` will only ever load cooked shaders. Shipping it next to Renderer and Editor for hot reload waits on a licence question: the release's `LICENSE-MS.txt` allows distributing the files in a "distributables file list", and the release doesn't contain one.

**DXC doesn't optimise for a GPU.** DXIL and SPIR-V are portable; the GPU driver compiles them to machine code for the exact GPU when a pipeline is created, on the player's machine.

## ShaderCompiler

`#include <Meow2/Graphics/Shader/ShaderCompiler.h>`: compiles one entry point of an HLSL file.

```cpp
Result<Shader::ShaderCompiler> compiler = Shader::ShaderCompiler::create(files);
const std::vector<Shader::ShaderDefine> defines{{"SKINNED", "1"}};
Result<Shader::ShaderCompileOutput> output = compiler->compile({
    .path = "assets/shaders/Sky.hlsl",
    .entryPoint = "psMain",
    .stage = Shader::ShaderStage::Pixel,
    .target = Shader::ShaderTarget::Spirv,      // or Dxil
    .optimization = Shader::ShaderOptimization::Debug,
    .defines = defines,
});
```

- **DXC is loaded, not linked.** `create()` opens `dxcompiler` from the executable's folder and returns `NotFound` if it isn't there. An app linked against DXC wouldn't start without it. Player never creates a compiler, so it needs no DXC.
- **Includes go through the FileSystem**, relative to the including file. `output.includes` lists every included file, for dependency tracking.
- **Errors** are `ErrorCode::ParseError` with DXC's text: `shaders/Sky.hlsl:12:5: error: …`.
- **Conventions:** SM 6.6, HLSL 2021, row-major matrices (`-Zpr`) used as `mul(vector, matrix)` like `Meow2/Math`, so a `Mat4` goes to the GPU as it is. SPIR-V uses D3D's constant-buffer layout (`-fvk-use-dx-layout`), so a struct has one layout on both backends.
- **Threads:** `compile()` may be called from several threads at once. On Windows the calls run in parallel. Off Windows a mutex runs them one at a time, because DXC changes the process-wide C locale there.

## Tests

`Build.Dxc.PinnedCompilerRuns` runs the downloaded compiler. `Shader.ShaderCompilerTest.*`: both targets, defines, includes, errors with file and line.

See also: techstack.md §4, §12.
