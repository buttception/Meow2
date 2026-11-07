# Shaders

Shaders are written in HLSL and compiled with [DXC](https://github.com/microsoft/DirectXShaderCompiler) to DXIL (D3D12) and SPIR-V (Vulkan).

- Status: ✅ M4 (0.5.0). DXC dependency (#135), `ShaderCompiler` (#137), shader settings and permutations (#141), cooking (#143), the build step (#145), the bindless contract (#147), hot reload (#149) done.

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

## Shader settings and permutations

`#include <Meow2/Assets/Shader/ShaderMeta.h>`: all of a shader's compile settings live in its `.meta`, next to the source.

```toml
id = "01900000-0000-8000-8100-000000000001"

[[entry]]
name = "vsMain"
stage = "vertex"        # vertex, pixel, compute, amplification or mesh

[defines]               # macros set in every variant
USE_FOG = 1

[options]               # one variant per combination: here 2 × 3 = 6
SKINNED = [0, 1]
QUALITY = ["LOW", "MID", "HIGH"]
```

To change how a shader compiles, you edit this file: add a macro under `[defines]`, or an axis under `[options]`. Values may be numbers, booleans (0/1) or strings.

Code picks a variant by naming values, and options it doesn't name take their first value:

```cpp
const std::vector<Assets::ShaderOptionChoice> choices{{"SKINNED", "1"}};
Result<Assets::ShaderVariantKey> key = Assets::variantKey(meta, choices);
std::vector<Shader::ShaderDefine> defines = Assets::variantDefines(meta, *key);
```

- **Typos are errors.** An option or value the `.meta` doesn't declare returns `InvalidArgument`.
- **A key is a number:** the variant's position among all combinations (a mixed-radix index), with options sorted by name, so their order in the file doesn't matter. Key 0 has every option at its first value.
- **Options multiply.** Ten two-value axes are 1,024 variants, each compiled for two targets. Over `c_shaderVariantWarning` (256) the cooker warns, and over `c_shaderVariantLimit` (4,096) the `.meta` is rejected.
- **GPU capabilities are ordinary options:** `WAVE_OPS = [0, 1]`, chosen at runtime by what the GPU supports.

## Cooking

The pipeline goes from source to compiled object:

```
shaders/Sky.hlsl + Sky.hlsl.meta  →  ShaderCooker (DXC)  →  cooked/shaders/<AssetId>.m2shader  →  ShaderAsset
```

```cpp
Assets::ShaderCooker cooker{files, compiler, jobs, log};
Result<Assets::ShaderCookResult> cooked = cooker.cook(*assets.find(id), {});   // skipped if up to date

Result<Assets::AssetHandle<Assets::ShaderAsset>> shader = assets.load<Assets::ShaderAsset>(id);
std::shared_ptr<const Assets::ShaderAsset> loaded = shader->get();
const std::vector<Assets::ShaderOptionChoice> choices{{"SKINNED", "1"}};
Result<Assets::ShaderVariantKey> key = Assets::variantKey(loaded->meta(), choices);
Result<std::span<const std::byte>> code = loaded->bytecode(*key, "psMain", Shader::ShaderTarget::Spirv);
```

- **A `.m2shader` holds everything:** the bytecode of every variant × entry point × target, the `.meta` settings, and the list of included files. It needs no source and no DXC to load, which `Assets.ShaderAsset.LoadsFromAFolderWithoutDxc` proves by running from an empty folder.
- **The cache key** hashes the source, the `.meta`, every included file, the options, the target list, the file format version and the DXC version. If it matches the cooked file's key, nothing is compiled. Editing an included file recooks only the shaders that include it.
- **Compiles run as jobs**, one per combination.
- **A failed cook writes nothing**, so the last good file stays.
- **Targets:** DXIL and SPIR-V on Windows, SPIR-V elsewhere (`defaultShaderTargets()`), because D3D12 is Windows-only. A cooked file holds only what it was cooked for.
- **The file format** is versioned (`c_shaderFileVersion`), and every length is checked when reading. A corrupt file is an error, and an old one is simply recooked.
- **The `cooked` mount** is set up by the composition root: read-write for the cooker, read-only for a game.

## The build step

Shaders live in `shaders/`, each with its `.meta`. A normal build cooks them all into `<build>/cooked/shaders/`: Debug builds cook with debug info, other configurations optimised.

**To add a shader:**
1. Write `shaders/Sky.hlsl`.
2. Create its `.meta` with a new ID:
   ```bash
   Meow2ShaderCompiler --source shaders --import Sky.hlsl
   ```
3. Add its `[[entry]]` blocks to `Sky.hlsl.meta` and commit both files.

The build runs `Meow2ShaderCompiler --source shaders --output <build>/cooked` every time. It's the tool, not CMake, that knows what changed, because only it can see `#include`s through the cache keys. With nothing to do it costs a few milliseconds per shader. A shader that doesn't compile fails the build, with `shaders/Sky.hlsl:12:5: error: …`.

`meow2_add_shaders(<target> SOURCE_DIR <folder> OUTPUT_DIR <folder>)` (`cmake/Meow2Shaders.cmake`) adds the same step for another folder. Other tool options are `--debug` and `--force`.

## The bindless contract

The engine has one binding layout: 16 bytes of **push constants**, and one global **descriptor heap**. Nothing else is bound per draw. Shaders get indices through the push constants and fetch what they need from the heap. That's what lets a compute shader drive thousands of draws, because no CPU code binds anything per object.

Every shader includes `Common.hlsli`:

```hlsl
#include "Common.hlsli"

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    const FrameConstants frame = frameConstants();      // from the heap, by g_push.frameBuffer
    const InstanceData instance = instanceData();       // by g_push.instanceBuffer and g_push.instance
    ...
}

float4 psMain(VertexOutput input) : SV_Target
{
    Texture2D<float4> albedo = ResourceDescriptorHeap[g_push.userData];
    ...
}
```

- **`shaders/Meow2Shared.hlsli` is compiled by both languages.** C++ gets it through `<Meow2/Graphics/Shader/BindlessLayout.h>`, so a struct can't drift apart between HLSL and C++. Only `uint`, `float4` and `float4x4` are allowed in it, padded by hand to 16 bytes. Sizes are checked on both sides at compile time.
- **Buffers are `ByteAddressBuffer`s:** one kind of heap slot serves every struct, through `heapBuffer()` and `loadElement<T>()`.
- **D3D12** needs SM 6.6. **Vulkan** works through DXC's default emulation, with descriptor indexing and `VK_EXT_mutable_descriptor_type`, so the same HLSL compiles for both. The M5 RHI creates the matching heap and root signature.
- `shaders/BindlessProbe.hlsl` exercises all of it for both targets, and CI runs `spirv-val` over the SPIR-V.

## Hot reload

`#include <Meow2/Assets/Shader/ShaderHotReload.h>`: edit a shader while an app runs, and it recompiles on a job.

```cpp
Assets::ShaderHotReload hotReload{{.assets = assets, .cooker = cooker, .jobs = jobs, .events = events, .log = log}};
hotReload.watchAll();                       // cooks what's stale, learns each shader's includes

hotReload.poll();                           // a couple of times per second, from the game loop

auto onReload = events.subscribe<Assets::ShaderReloaded>([&](const auto& e) { rebuildPipelines(e.id); });
```

- **What's watched:** each shader's source, its `.meta` and every file it includes. New includes are picked up after each reload.
- **It polls file times.** That's the same code on every platform, and it copes with editors that save by replacing a file. It reads one timestamp per file, so poll a couple of times a second, not every frame.
- **Compiles run on a job**, so the main thread never stalls. Handles see the new shader through `AssetManager::reload`, and `ShaderReloaded` arrives at the next `dispatchQueued()`.
- **A broken edit keeps the old shader running.** The compiler's messages go to the log and to `ShaderReloadFailed`, with file and line. Saving a fix reloads it, with no restart.
- **Needs DXC:** Renderer and Editor, not Player. It's wired into an app with the Renderer (M6), since nothing can draw a shader yet.

## Tests

`Build.Dxc.PinnedCompilerRuns` runs the downloaded compiler. `Shader.ShaderCompilerTest.*`: both targets, defines, includes, errors with file and line. `Assets.ShaderMeta.*`: parsing, bad files, keys and defaults. `Assets.ShaderFile.*`, `Assets.ShaderAsset.*`, `Shader.ShaderCookerTest.*`: the file format, loading without DXC, the cache. `Build.Shaders.CookStep`: the tool end to end. `Shader.BindlessTest.*`: the repository's shaders on both targets, and `spirv-val`. `Shader.ShaderHotReloadTest.*`: edits reach handles, broken edits keep the old shader.

See also: techstack.md §4, §12.

## Implementation summary

- `ShaderCompiler` (`Meow2/Graphics/Shader/`): DXC loaded at runtime through `SharedLibrary`, SM 6.6, HLSL 2021, `-Zpr`, `-fvk-use-dx-layout`, includes through the VFS.
- `Meow2ShaderCompiler` and `meow2_add_shaders()`: cooks `shaders/` into `<build>/cooked/` on every build (the cache keys make that cheap); `--import` writes a new `.meta`.
- `shaders/Common.hlsli` and `Meow2Shared.hlsli` (with `Meow2/Graphics/Shader/BindlessLayout.h`): 16 bytes of push constants, `ResourceDescriptorHeap`, structs compiled by C++ and HLSL with size checks on both sides; `spirv-val` in CI.
