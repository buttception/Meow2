# glTF import

`#include <Meow2/Assets/Gltf/GltfImporter.h>`: reads a glTF 2.0 file (`.gltf` with its buffers and images, or one `.glb`) into meshes, materials, images and a node tree on the CPU, in the engine's left-handed space. `#include <Meow2/Graphics/Renderer/SceneModel.h>` puts the result in a `Scene` and on the GPU.

- **Status:** ✅ M6. Import, textures and a lit draw on both backends (#169 / PR #170); PBR and the rest in [Rendering](Rendering.md); every suggested sample checked by golden images (#194). Scene files are M7.
- **Libraries:** [cgltf](https://github.com/jkuhlmann/cgltf) v1.15 (submodule) parses the file. [stb_image](https://github.com/nothings/stb) v2.30 (one header, copied into `external/stb/`) decodes PNG and JPEG. Both are compiled in their own libraries, outside the engine's warnings, and allocate through the engine's hooks under `MemTag::Assets`.

## Seeing a sample

```bash
cmake -P cmake/FetchGltfSample.cmake DamagedHelmet
```

```bash
build\windows-msvc-debug\bin\Meow2Renderer.exe --set renderer.scene=DamagedHelmet
```

Or pick it in Renderer's Scenes window, which fetches samples itself ([Renderer](Renderer.md)). `renderer.scene` takes a sample name, or a path to any `.gltf` or `.glb`. The camera circles the model, far enough to see all of it. Empty, Renderer draws its grid of cubes.

The fetch script makes a blobless, sparse clone of [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) at a pinned commit into `assets/gltf-sample-assets/` (git-ignored), and downloads only the models you name. Running it again for a model that's there does nothing. Renderer looks for samples there by default; `renderer.sampleAssets` points it elsewhere.

| Key | Default | Meaning |
|---|---|---|
| `renderer.scene` | empty | A sample name (`Duck`) or a path to a `.gltf`/`.glb`; empty for the cube grid |
| `renderer.sampleAssets` | `<repo>/assets/gltf-sample-assets` | Where the fetched samples are |

## Using it

```cpp
Result<Assets::ImportedModel> model = Assets::importGltf(files, "models/Duck.gltf", log); // virtual path

Renderer::RenderMeshes meshes;                                      // AssetId → GPU meshes
Result<std::vector<AssetId>> meshIds = Renderer::uploadModel(gpuScene, *model, ids, meshes, log);
Renderer::instantiateModel(scene, hierarchy, *model, *meshIds);     // one entity per node
hierarchy.update();
std::vector<Shader::InstanceData> instances;
Renderer::gatherInstances(scene, meshes, instances);               // MeshRenderer + WorldTransform → instances
gpuScene.setInstances(instances);
```

Entities get a `LocalTransform`, a `Parent` and, for nodes with a mesh, a `MeshRenderer` naming the mesh by `AssetId`. Nothing in the `Scene` refers to GPU indices, so the same entities mean the same thing in every app, and after a backend switch.

## How it works

### Files through the VFS

Every file is read through the [virtual file system](FileSystem.md): the `.gltf`, its `.bin` buffers (cgltf's file callbacks are routed to `FileSystem::readFile`) and its images. Paths in the file are relative to it and may be percent-encoded (`tri%20data.bin`). Base64 data URIs and `.glb` binary chunks work too.

### Right-handed to left-handed

glTF is right-handed: +Y up, +Z forward (the front of a model faces +Z), +X to the model's left. The engine is left-handed with +X right ([techstack §10](../../techstack.md#10-math--coordinates)). Mirroring X converts one into the other:

- **Positions and normals:** `x = -x`.
- **Triangles:** mirroring the model and the camera together puts the same picture on screen, so a front face still looks counter-clockwise there, which is glTF's rule. The engine's rule is clockwise (D3D style), so every triangle's last two indices are swapped.
- **Tangents:** X is negated like normals, and the bitangent's sign flips, because mirroring reverses the handedness of `cross(normal, tangent)`.
- **Node transforms:** with `S` mirroring X, the left-handed matrix is `S × M × S`, decomposed back into scale, rotation and translation. A rotation about X stays the same, rotations about Y and Z turn the other way, and translations lose the sign of X.

A model seen from a camera at `(-x, y, z)` in the engine looks exactly like the glTF viewer's picture from `(x, y, z)`. The Duck, OrientationTest and DamagedHelmet were checked against their reference screenshots this way.

### What's imported

| glTF | Engine | Notes |
|---|---|---|
| Default scene's nodes | `ImportedNode`, parents first | The first scene if there's no default; every root if there's none |
| Triangle primitives | `ImportedPrimitive`: positions, normals, tangents, UV set 0, vertex colours (`COLOR_0`), 32-bit indices | Points, lines and strips are skipped with a warning. Missing normals are computed from the triangles, missing tangents from the UVs (in glTF's space, before mirroring), missing UVs are zeros, missing colours white |
| Metallic-roughness materials | `ImportedMaterial` | Every core parameter and texture slot, alpha mode, double-sided |
| Cameras | `ImportedCamera`, `ImportedNode::camera` | Perspective (vertical field of view, near, far) and orthographic (`ymag`); they still look along the node's −Z, see [Renderer → Cameras](Renderer.md#cameras) |
| Animations | `ImportedAnimation` with `ImportedChannel`s | Translation, rotation and scale of the imported nodes, with step, linear and cubic-spline keys, converted to left-handed space like node transforms (positions negate x; quaternions negate y and z). `animatedTransform()` samples one node at a time. Morph-target weights are skipped with a warning |
| Samplers | `ImportedSampler` | Linear or nearest, wrap per axis (repeat, clamp, mirrored repeat) |
| `KHR_texture_transform` | `ImportedTexture::offset`, `rotation`, `scale` | Per texture slot; a transform that picks another UV set is a warning |
| PNG and JPEG images | `Image`, RGBA8 | A missing or broken image is a warning: the model draws without it |

Accessors are read with `cgltf_accessor_unpack_floats`, so normalised integers (`KHR_mesh_quantization`) and sparse accessors work.

**Refused:** files that *require* an extension whose data can't be read without it: Draco, meshopt, Basis Universal and WebP textures (`ErrorCode::Unsupported`). Other required extensions are a warning. glTF 1.0 is refused.

### On the GPU

`uploadModel` optimises each primitive for the GPU's caches and splits it into meshlets ([Rendering → Meshes for the GPU](Rendering.md#meshes-for-the-gpu-optimisation-and-meshlets)), then adds it to the `GpuScene` as a mesh, each material as a `MaterialData` record, each sampler once, and each image a material uses as a texture, cooked by kind: colour (base colour, emissive) as sRGB BC7, data (metallic-roughness, occlusion) as linear BC7, normal maps as BC5; uncompressed RGBA8 when compression is off. An image used two ways is cooked twice. Cooked textures are cached ([Rendering → Cooked textures](Rendering.md#cooked-textures-bc7-bc5-and-the-cache)). Vertices are 24 bytes: positions quantised to 16 bits within the primitive's box, octahedral normal and tangent, the bitangent's sign, UVs as two half floats, and an RGBA8 colour.

New textures are copied at the start of the next frame, by an "Upload textures" pass in the render graph. The pixels stay on the CPU, so the textures can be made again after a backend switch, like everything else in the `GpuScene` ([Rhi → Switching backends](Rhi.md#switching-backends-while-running)).

How they're drawn (PBR, normal maps, mips, double-sided, mirrored and blended surfaces) is in [Rendering](Rendering.md). Instances without a material (the cube grid) keep their colour from their index.

## Not yet

- UV sets other than 0, and the `KHR_materials_*` extensions.
- glTF's sampler `minFilter` is reduced to "mipmaps or not": every texture has mips, and nearest filtering applies to them too.
- Scene files: models are imported each run (M7).
- Skins, morph targets and an animation system (blending, events): M12. Animations are only sampled per node.
- Images are decoded one after another on one thread; big scenes such as Sponza take a few seconds in Debug.

## Tests

`Assets.Image.*` and `Assets.GltfImporterTest.*` write small files in code (a tiny PNG encoder and a `.glb` writer, `tests/Assets/TestFiles.h`), so they need no samples and run in CI. They check the mirroring and winding, computed normals, tangents (from the file and generated), vertex colours, samplers, node transforms, buffers next to the file, `.glb` with an embedded texture, materials, cameras and animations (`ReadsCamerasAndAnimations`, `GltfAnimation.CubicSplineFollowsItsTangents`), and each failure. `RhiGpu*.GpuSceneTest.ModelDrawsWithItsTexture` draws a textured model through `SceneModel` on both backends.
