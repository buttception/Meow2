# Rendering: materials and shading

How `Renderer::GpuScene` draws a scene: glTF's metallic-roughness materials, lit by one sun and a physically based sky (image-based lighting), on Vulkan and D3D12 alike. The buffers, culling and indirect drawing underneath are in [Rhi → GPU-driven drawing](Rhi.md#gpu-driven-drawing); how glTF files get here is in [Gltf](Gltf.md).

- **Status:** ✅ M6. PBR, normal maps, mipmaps, samplers, double-sided, mirrored and blended surfaces (#174); Hillaire's sky and image-based lighting from it (#180); cooked `.m2texture` files with BC7 and BC5 (DirectXTex), a texture cache, and texture transforms (#183); meshes optimised at import and drawn as meshlets through mesh shaders (#192); rotated texture transforms fixed and golden images of 25 samples (#194).

## Using it

```cpp
#include <Meow2/Graphics/Renderer/GpuScene.h>

const std::uint32_t albedo = *scene.addTexture(image, /*srgb=*/true, "Albedo"); // mips made here
const std::uint32_t pixelArt = *scene.addSampler({.filter = Rhi::SamplerFilter::Nearest});
const std::uint32_t material = *scene.addMaterial({.baseColor = {.texture = static_cast<std::int32_t>(albedo),
                                                                 .sampler = static_cast<std::int32_t>(pixelArt)},
                                                   .metallic = 0.0F,
                                                   .roughness = 0.6F,
                                                   .doubleSided = true});
scene.setLighting({.sunDirection = {0.4F, 0.8F, 0.6F}, .sunIntensity = 3.0F, .exposure = 1.0F});
```

`uploadModel()` (`SceneModel.h`) does all of this for an imported glTF model.

| `MaterialDesc` | Meaning |
|---|---|
| `baseColorFactor`, `baseColor` | Linear RGBA × an sRGB texture × the vertex colour. Alpha is used by `alphaMask` and `blend` |
| `metallic`, `roughness`, `metallicRoughness` | 0..1 factors × a linear texture: roughness in G, metallic in B (glTF's packing) |
| `normal`, `normalScale` | Linear tangent-space normal map, +Y up (glTF's convention); the scale multiplies X and Y |
| `occlusion`, `occlusionStrength` | Ambient occlusion in R; it darkens the ambient light only |
| `emissiveFactor`, `emissive` | Linear RGB × an sRGB texture, added after lighting |
| `alphaMask`, `alphaCutoff` | Pixels below the cutoff are discarded |
| `blend` | Mixed over what's behind; drawn sorted, after everything else |
| `doubleSided` | Both sides drawn, each lit from its own side |

Each texture slot names its own sampler (`-1`: linear, repeating, mipmapped).

## How it works

### The frame

The scene isn't drawn into the swapchain any more but into **Scene colour**, an RGBA16F texture in linear light with the exposure applied: the opaque surfaces (**G-buffer** then **Lighting**, deferred, or one **Opaque** pass, forward; see [Deferred shading](#deferred-shading)), **Sky** (behind everything, where depth is still far) and **Blended** (sorted, over both). **Tone map** (`ToneMapPass`, `ToneMap.hlsl`) then applies the Khronos PBR Neutral curve while writing the swapchain, whose sRGB view encodes the result.

Why: every step's output is now a texture of the render graph, which the Renderer's inspector can show ([Renderer](Renderer.md#frame-and-debugging)), and post-processing (bloom, exposure control) has an HDR image to work on. It costs one full-screen pass.


### Deferred shading

Opaque surfaces are drawn **deferred** by default (`GpuScene::setDeferred()`, Meow2Renderer's DEFERRED setting); forward stays available, and both give the same image (the golden images run on both paths against the same goldens).

- **Forward** (the **Opaque** pass) shades every pixel as it rasterises it. A pixel covered by five overlapping surfaces is shaded five times, though only the nearest is seen, and every light would have to be evaluated in every draw.
- **Deferred** splits that in two:
  1. **G-buffer**: the same draw lists, culling and pipelines per list (the `GBUFFER = 1` variant of `GpuDriven.hlsl`), but instead of shading, `psMain` writes what shading needs into four targets at once (multiple render targets, [Rhi](Rhi.md#several-colour-targets)), plus depth:

     | Target | Format | Holds |
     |---|---|---|
     | G-buffer albedo | RGBA8 sRGB | base colour (8-bit sRGB is enough for a colour) |
     | G-buffer normal | RGBA16F | the shading normal (8 bits per axis would band smooth highlights) |
     | G-buffer material | RGBA8 | metallic, roughness, occlusion |
     | G-buffer emissive | RGBA16F | emitted light (goes above 1) |
     | Depth | D32F | the scene's depth, as before |

  2. **Lighting** (`Deferred.hlsl`'s `psLighting`): one full-screen triangle reads the G-buffer and shades each pixel exactly once. Pixels nothing covered (depth 0, the far end of reversed Z) are skipped, and the **Sky** pass fills them afterwards. **Blended** surfaces are always forward: a G-buffer holds one surface per pixel, and blending needs the one behind too.
- **One lighting function for both paths:** `shaders/Lighting.hlsli` holds `lightSurface()` / `shadeSurface()` (the BRDF, sun, sky light) on a `Surface` struct. Forward's `psMain` fills a `Surface` from the material and calls it; deferred fills it from the G-buffer. So the two paths can't drift apart.
- **The position isn't stored.** A G-buffer could hold each pixel's position, but that's 8–16 more bytes per pixel to write and read every frame, and depth already says where the pixel is. `positionFromDepth()` (in `Lighting.hlsli`) takes the pixel's centre in clip space (`x`, `y` from its coordinates, `z` its depth) back through `FrameConstants::inverseViewProjection` and divides by `w`. With reversed infinite Z this is exact up to float precision.
- **Debug views** (other than Position) still draw forward: they're a variant of the forward shader. The Position view is the exception; [Renderer](Renderer.md#frame-and-debugging) explains it.
- **Why deferred now, with one light?** The sun alone doesn't need it: deferred pays off with many lights (each touches only the pixels it reaches, once) and with screen-space effects that read normals and depth (SSAO, SSR). The G-buffer is the base for both (M13).
- **Pitfalls avoided:** the G-buffer is cleared to zero each frame, so background pixels hold nothing a later pass could mistake for a surface; its state is carried between frames (where Lighting left it), so the next frame's writes wait for the last frame's reads; it's remade at the scene's size and freed on a backend switch (GpuScene's device user).
### Meshes for the GPU: optimisation and meshlets

`MeshProcessing.h` runs a mesh through [meshoptimizer](https://github.com/zeux/meshoptimizer) between import and upload:

```cpp
#include <Meow2/Graphics/Renderer/MeshProcessing.h>

MeshData mesh = makeMesh(arrays, bounds);
optimizeMesh(mesh);   // same triangles, an order the GPU's caches like
buildMeshlets(mesh);  // clusters of ≤ 64 vertices and ≤ 124 triangles, with bounds
scene.addMesh(mesh);  // builds the meshlets itself if they're missing
```

**`optimizeMesh()`** keeps every triangle and its winding, and changes only the order:

1. **Merge identical vertices.** glTF files often repeat a vertex once per triangle around it. Merged, the GPU sees that it's the same vertex and can reuse its result.
2. **Vertex cache order.** A GPU keeps the last few vertex shader results; a triangle whose corners were just shaded costs almost nothing. Triangles are reordered so neighbours come together (ACMR, vertices shaded per triangle, falls from up to 3 to well under 1 on a regular grid).
3. **Overdraw order.** Within what the cache order allows (at most 5 % worse), triangles likely to be in front come first, so the depth test rejects more of what's behind before it's shaded.
4. **Vertex fetch order.** Vertices are renumbered by first use, so reading them walks memory forwards.

It runs once per primitive when a glTF model is uploaded (`uploadModel()`), at import rather than per frame or per backend switch.

**A meshlet** is a small cluster of neighbouring triangles: at most 64 vertices and 124 triangles (sizes that suit NVIDIA's and AMD's hardware). Each has a **bounding sphere** and a **normal cone**: the average direction its triangles face, and how far they spread. The clusterizer favours tight cones over full clusters a little (weight 0.25), so more clusters can be culled for facing away. Triangles are stored per meshlet as three 8-bit indices into its own vertex list, one `uint` each.

### The meshlet path

On a GPU with mesh shaders (`RhiAdapterInfo::meshShaders`), `GpuScene` draws the three opaque lists through task and mesh shaders instead of indirect draws. Both paths draw the same image; the meshlet path draws less of what can't be seen.

| Pass | Indirect path | Meshlet path |
|---|---|---|
| Cull instances (`csCull`) | frustum test per instance; one `IndirectDraw` per survivor | the same test; one `TaskItem` (an instance and up to 32 of its meshlets) per 32 meshlets of each survivor |
| Task arguments (`csTaskArgs`) | — | each list's item count → the group counts of one mesh dispatch |
| Opaque | `drawIndirectCount()` per list; `vsMain` shades every vertex of the mesh | `dispatchMeshIndirect()` per list: `asMain` (one group per item, one thread per meshlet) culls each meshlet; `msMain` (one group per survivor) shades its vertices and emits its triangles |

**Cluster culling** in the task shader:

- **Frustum:** the meshlet's sphere against the six planes, as for instances. A big mesh that's half in view draws only its meshlets that are.
- **Normal cone** (back faces): if the direction from the camera to the cluster lies inside the cone of directions from which every triangle in it faces away, none can be seen. This is meshoptimizer's test: `dot(centre − camera, axis) ≥ cutoff · |centre − camera| + radius`. It's skipped for double-sided materials, for clusters whose normals spread too far (cutoff 1), for instances scaled differently along their axes (the spread would change), and for orthographic views, which have no camera point. The camera point is where the frustum's side planes meet, so frozen culling freezes it too.
- The survivors are packed in their original order, not by an atomic, so the same frame draws the same way every time.

The task shader counts meshlets and triangles drawn; `GpuScene::meshletStats()` reads them back a frame or two later without waiting. With the default view of the whole model, DragonAttenuation draws 2178 of its 2315 meshlets and Sponza 3112 of 3474. Looking at a part of a scene, much more is culled.

**Why task shaders, not a second compute pass:** culling meshlets in compute would need a list of survivors in memory, a pass to count them and an indirect dispatch, with a barrier between each. The task shader culls and launches the mesh groups directly, and the survivors' list stays on chip (the payload).

**Why a list of 32-meshlet items, not one task dispatch per instance:** an indirect dispatch per instance would need the instance index in the shader. Vulkan's mesh dispatches have no "first instance", and DXC doesn't expose the draw index to task shaders, so each API would need its own trick. A flat list of items is the same on both, and every task group has a full set of work.

**Limits:** a dispatch may have 2^22 groups in all and 65535 per dimension. Past 65535 items, the dispatch goes 2D, 32768 wide; the lists hold at most 2^22 items each.

**What stays on the indirect path:** blended surfaces (drawn sorted, one draw each), GPUs without mesh shaders, the `MESHLETS` setting off, and the triangle debug view: a colour per triangle needs a per-triangle value in the pixel shader, mesh shaders share vertices between triangles, and DXC can't mark a pixel shader input per-primitive for Vulkan.

### Shading

`psMain` in `shaders/GpuDriven.hlsl` is the standard real-time PBR model, the one glTF's specification describes:

- **Diffuse** is Lambert: `baseColor × (1 − metallic) / π`. Metals have no diffuse light: light that enters a metal is absorbed.
- **Specular** is Cook-Torrance with three parts:
  - **GGX distribution:** how many microscopic facets face exactly between the light and the eye. Rough surfaces spread the highlight.
  - **Height-correlated Smith visibility:** how many of those facets are hidden by others.
  - **Schlick's Fresnel:** reflectance rises towards 100 % at grazing angles.

  The reflectance straight on (`F0`) is 4 % for non-metals and the base colour for metals, which is why gold reflects gold.
- **Roughness** is squared before use (`alpha = roughness²`), so the 0..1 slider feels even. It's clamped to at least 0.045, because a perfect mirror would turn the sun into a single pixel.

Lights:
- **The sun:** one directional light (`SceneLighting::sunDirection`, colour and intensity).
- **The sky** ([below](#sky-and-image-based-lighting)): the sun's light reaches the ground through the atmosphere's transmittance, so it reddens as the sun sets, and the whole sky lights the scene. Without a `SkyAtmosphere`, a flat sky colour above and a ground colour below stand in for it.

The result is scaled by `exposure`, then goes through **Khronos PBR Neutral** tone mapping. That curve keeps colours as authored up to about 0.8 and rolls brighter light off to white, so a red material stays the red its author picked; ACES and other filmic curves shift hues. The swapchain's sRGB view does the final encoding.

### Normal maps and tangents

A normal map stores directions in *tangent space*: X along the texture's U, Y along the bitangent, Z out of the surface. To use one, every vertex needs a tangent (and the bitangent's sign, ±1: whether the UVs are mirrored).

- Tangents come from the file, or are computed from the UVs (Lengyel's method) when it has none. That's not bit-exact with MikkTSpace, which glTF recommends, but it gives the same directions.
- The bitangent is `cross(normal, tangent) × sign`, and points towards **decreasing V**: glTF images start at the top, and its normal maps are "+Y up".
- The packed vertex (24 bytes) keeps the tangent as an octahedral direction, with its sign in z's spare top bit.
- After interpolation, the pixel shader makes the tangent perpendicular to the normal again (Gram-Schmidt) before building the frame.

### Mirrored and double-sided surfaces: three draw lists

A negative scale (a mirrored node) reverses the triangles' winding on screen, so culling back faces would remove the *front* of the mesh. The cull pass sorts every visible instance into one of three indirect draw lists. Each has its own pipeline, and they differ only in culling:

| List | Culls | For |
|---|---|---|
| `MEOW2_DRAW_LIST_BACK` | back faces | most meshes |
| `MEOW2_DRAW_LIST_MIRRORED` | front faces | instances whose world matrix has a negative determinant (`MEOW2_INSTANCE_MIRRORED`, set on the CPU) |
| `MEOW2_DRAW_LIST_DOUBLE_SIDED` | nothing | double-sided materials |

Each list has room for every instance, because which list an instance lands in isn't known in advance. They're drawn by three `drawIndirectCount()` calls, with offsets into one draw buffer and one count buffer.

In the shaders:
- **Normals** go through the inverse transpose of the world matrix, built from cross products of its rows, so non-uniform scale doesn't tilt them. The determinant's sign keeps a mirrored normal pointing outwards.
- **Mirroring** flips the bitangent's sign.
- **Back faces** of double-sided materials flip the normal, tangent and bitangent, so they're lit from their own side. A mirrored triangle's front face looks like a back face to the rasterizer, so `SV_IsFrontFace` is combined with the instance's flag.

The alternative, one list with culling off for everything, would draw every hidden back face of every mesh: about twice the pixel work.

### Transparency

Blended materials are left out of the indirect lists. After the three opaque draws, the CPU draws them one by one, **sorted back to front** by the distance to their bounding box's centre (positions are camera-relative, so that's the length of the centre). Each draw:
- tests depth, so opaque things in front hide it;
- doesn't write depth, so blended surfaces behind each other all show;
- uses `vsBlend`, which reads its instance from the push constants instead of the indirect draw.

Sorting whole instances is the usual compromise: overlapping triangles inside one mesh can still come out in the wrong order. Order-independent transparency (weighted blended OIT, per-pixel lists) would fix that at a cost, and can come later if a scene needs it. Blended instances aren't frustum-culled on the GPU; there are rarely many of them.

### Textures, mips and samplers

`addTexture()` builds the full mip chain on the CPU right away (`Assets::makeMipChain`: a 2 × 2 box filter per level). Colour textures are averaged in **linear light** and encoded back to sRGB; averaging the sRGB bytes directly would darken distant textures. The chain is kept for a backend switch, and the "Upload textures" pass copies every level from one staging buffer.

Without mips, a texture seen from far away skips texels between pixels, which shimmers and aliases. Mips are pre-shrunk copies the GPU picks by distance. Linear samplers from glTF files get 8× anisotropic filtering, which keeps floors and walls seen at a grazing angle sharp.

### Cooked textures: BC7, BC5 and the cache

A GPU reads compressed textures directly, without unpacking them first. *Block compression* (BC) stores each 4 × 4 pixel block in 16 bytes, so a block of RGBA8 (64 bytes) takes a quarter of the memory and bandwidth. The GPU decodes a block in the texture unit as it samples, so it costs nothing at draw time.

- **BC7** (colour and data textures): two endpoint colours per block (or per partition of it) and an index per pixel saying where between them it lies. Close to the original for photos and painted textures. Encoding is a search over modes and partitions, so it's slow even in BC7's quick mode: DamagedHelmet's five 2048-pixel textures take about 15 s on 16 threads (RelWithDebInfo), and 0.4 s to read back from the cache.
- **BC5** (normal maps): two channels, X and Y, each with its own endpoints, the same 16 bytes per block. A normal map's three channels aren't independent (a unit vector), so the shader rebuilds Z = √(1 − X² − Y²), and X and Y get all the precision. BC7 on a normal map would spend bits on Z and blur its directions.

`Assets::cookTexture(image, kind, compress, jobs)` (`<Meow2/Assets/Texture/CookedTexture.h>`) makes the mip chain, then compresses each level with **DirectXTex** (Microsoft's MIT-licensed texture library), in strips of 64 rows spread over the job system. Strips are independent because blocks are: the result is identical to compressing on one thread (`CookedTexture.JobsGiveTheSameBlocks`). Mips smaller than a block (2 × 2, 1 × 1) are padded by repeating their edge and take one whole block, which is what both APIs expect. A top level whose sides aren't multiples of 4 stays RGBA8: D3D12 refuses BC textures of that size.

DirectXTex is called from one file, `src/Assets/Texture/DirectXTex/`, behind `BlockCompression.h`: on Windows its header brings in the D3D11 and Windows headers, which stay out of the rest of the engine. It's built from its sources as `meow2_directxtex`, without its CMake project, which adds install rules that would put the library in the release zip.

A cooked texture is saved as a **`.m2texture`** file: `M2TX`, a version, the format, then each level's size and bytes, little-endian. `decodeM2Texture` checks every size against the format, so a damaged file is a `ParseError`, never a bad upload.

`Assets::TextureCache` keeps them in a folder (`cache/textures` in Meow2Renderer) named by `textureCacheKey()`: a 64-bit FNV-1a hash of the pixels, the size, the kind, compression and the cooker's version. The first load of a model cooks; later loads read the files, which takes milliseconds. Changing the cooker bumps its version, so old entries stop matching instead of being used. Deleting the folder is always safe.

`uploadModel(scene, model, ids, meshes, log, {.compress, .cache, .jobs})` cooks each image once per kind it's used as. `GpuScene::addTexture(CookedTexture)` uploads every level as it is (`addTexture(Image, srgb)` still works: it cooks RGBA8 with mips). `renderer.compressTextures` turns compression on (the default except in Debug, where the unoptimised encoder takes minutes over a big model).

### Texture transforms

`KHR_texture_transform` gives a texture slot an offset, a rotation and a scale, so a texture atlas or a tiled floor can reuse one image. The extension defines uv′ = T × R × S × (u, v, 1); `GpuScene` multiplies that out once per material into two rows of a 2 × 3 matrix per slot (`MaterialData::baseColorUv0/1`, …), so the shader does two dot products per sample. Without a transform the rows are the identity.

The rotation's direction is the one Khronos' TextureTransformTest checks: rows (cos θ, sin θ) and (−sin θ, cos θ), times the scale. Until 0.7.0 the sign was the other way, which turned rotated textures backwards; its golden image now guards it.

`addSampler()` returns an existing sampler when an identical one was added before; glTF files repeat the same few. glTF's mirrored repeat maps to `SamplerAddress::Mirror`.

### Sky and image-based lighting

`Renderer::SkyAtmosphere` is a physically based sky built with Sébastien Hillaire's method ("A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020; Unreal Engine's sky atmosphere). The atmosphere is an Earth-like shell 100 km thick around a 6360 km planet, with three things in it:

| Medium | Does | Look |
|---|---|---|
| Air molecules (Rayleigh) | scatter blue far more than red, almost evenly in all directions | the blue sky; the red sun at the horizon, after the blue is scattered away |
| Aerosols (Mie) | scatter all colours alike, mostly forwards | the bright haze around the sun |
| Ozone | absorbs orange-red | keeps the zenith blue at sunset |

The trick is a few small lookup textures (LUTs), so the expensive integrals aren't done per pixel:

| LUT | Size | Holds | Built |
|---|---|---|---|
| Transmittance | 256 × 64 | how much light gets through from any height, at any angle, to the top of the atmosphere | once |
| Multiple scattering | 32 × 32 | light that has bounced many times, for each height and sun angle (Hillaire's ψ_ms: second-order scattering made infinite-order by a geometric series) | once |
| Sky-view | 192 × 108 | the whole sky seen from the camera, by zenith and azimuth from the sun; rows squeezed towards the horizon, where the sky changes fastest; the lower half is the lit ground | every frame |

All three are compute shaders in `shaders/Sky.hlsl`, with the physics in `shaders/Atmosphere.hlsli`. Each sky-view texel marches 30 steps along its ray: the sun's light (from the transmittance LUT, unless the planet shadows it) scattered by the Rayleigh and Mie phase functions, plus the multiple-scattering LUT, integrated per step the energy-conserving way (Hillaire's equation 7).

**The sky behind the scene** is one full-screen triangle at depth 0, which is infinitely far with reversed Z. It's drawn after the opaque geometry with depth tested but not written, so it only fills pixels nothing else covered. Each pixel looks up the sky-view LUT in its view direction and adds the sun's disc where the ground isn't in the way. The direction comes from two points along the pixel's ray, so it's right whether or not the view matrix moves the camera away from the origin.

**Image-based lighting**, recomputed each frame after the sky-view LUT:

1. **Environment map:** the sky (without the sun's disc, which the shading adds as a light) in a 128-pixel octahedral map with every mip. Octahedral mapping folds the sphere onto a square with nearly even texel areas and no poles; it needs one 2D texture instead of a cube map. Every level samples the sky-view LUT directly, so no level reads another (the sky is smooth enough not to need a box filter).
2. **Prefiltered map:** 6 levels, level i blurred by a GGX lobe of roughness i / 5. That's the first half of Karis' split-sum approximation, with the view direction taken as the normal. It uses 64 importance samples with filtered importance sampling, which reads a blurrier mip for samples that stand for more of the sphere, so it doesn't sparkle.
3. **Diffuse light:** the environment's 16 × 16 level projected onto 9 spherical-harmonic coefficients (bands 0 to 2, Ramamoorthi and Hanrahan 2001), each texel weighted by its solid angle, then convolved with the cosine lobe. Evaluating them in a normal gives the irradiance there: smooth enough that 9 numbers capture it to within a few percent.

The PBR shader then uses `skyIrradiance(normal) / π` for diffuse ambient light, and the prefiltered map at `roughness × 5` along the reflection, times the environment BRDF (Karis' analytic fit), for specular.

**Sizes and units:** the atmosphere is in kilometres and the scene in metres. The camera's altitude is its height above y = 0, plus 50 m. A 32-bit float resolves only about 0.5 m at 6360 km, so lower altitudes would turn the horizon maths into noise, and differences of squares near the ground are computed as `(r - R)(r + R)`. The horizon drops by only 0.2° at 50 m.

**Aerial perspective** (haze over distant objects) is Hillaire's fourth LUT, a 3D texture; it comes when the RHI has 3D textures.

### Shadows from the sun

The sun casts **cascaded shadow maps** (`GpuScene::setShadows()`, `shaders/Shadows.hlsl`, the shadow lookup in `Lighting.hlsli`). A shadow map is a depth image seen from the light: a point is in shadow when something nearer the sun covers it in that image.

- **Cascades.** One map can't be sharp near the camera and still reach far away, so the camera's view is cut into **4 slices** by distance, each with its own 2048 × 2048 map (`renderer.shadowResolution`). The splits blend even spacing with logarithmic spacing (λ = 0.8, `cascadeSplits()`), so near slices are short and sharp. How far shadows reach is worked out from the scene's bounds and the camera, rounded up to a power of two, at most 200 m (`renderer.shadowDistance`).
- **Stable boxes.** Each cascade is an orthographic box the sun looks through (`ShadowCascades`, `ShadowCascades.h`):
  - it's fitted to a sphere around the slice whose size doesn't depend on where the camera looks (`sliceRadius()`), so turning the camera doesn't resize texels;
  - its position is snapped to whole texels, so shadow edges don't crawl as the camera moves;
  - it's **25 % larger** than the slice and only moves when the slice leaves it, which keeps the cached map below valid while the camera moves around inside;
  - its depth covers every caster, so things between the sun and the slice cast into it.
- **Static casters cached, movable ones every frame.** Per cascade:
  1. **Static shadow N** draws the static casters into a cached map, only when it's stale: the cascade moved, the sun turned, a static instance appeared, went or moved (a fingerprint of the static instances, kept by `setInstances()`), or the settings changed. On other frames its passes stay in the render graph marked `skipped` (`GraphPass::skipped`: not recorded, no barriers), so the graph has the same passes every frame.
  2. **Shadow map N** copies the cached map texel for texel (`psCopyDepth`, a full-screen pass that writes depth).
  3. **Movable shadow N** draws the movable casters over the copy, every frame.

  Lighting reads the result: static and movable shadows combined. An instance is movable when its `InstanceData::flags` has `MEOW2_INSTANCE_MOVABLE`, which `gatherInstances()` sets from `MeshRenderer::movable`. Until the editor infers mobility from components (M13), Meow2Renderer marks what the file's animations move, and their children.
- **Drawing casters.** Each set is culled against the cascade's box by the same `csCull` (its record picked by `g_push.instance`, and `CullConstants::mobility` keeping only static or only movable instances), then drawn through the indirect path with depth-only pipelines (no colour target, [Rhi](Rhi.md#depth-only-passes)). `psShadow` cuts out alpha-masked materials, so leaves and fences cast their shape. Meshlets aren't used for casters yet.
- **Reading the shadow.** In `lightSurface()`, forward and deferred alike, sunlight is multiplied by `sunShadow()`:
  - the cascade is picked by the point's view distance;
  - in the last 10 % of a cascade it blends into the next, and after the last the shadow fades out;
  - the point is first pushed out along its normal by 1.5 texels (normal-offset bias), which stops surfaces from shadowing themselves in stripes (shadow acne), plus a tiny constant depth bias;
  - a 3 × 3 tent filter over 4 × 4 texels softens the edge and moves it smoothly.

  Reversed Z, as everywhere: nearer the sun is the larger depth, and the maps clear to 0.
- **Seeing it.** The Cascades debug view colours each pixel by its cascade (red, green, blue, yellow; grey past the last), darker in shadow. The maps are textures of the render graph ("Shadow map N", "Shadow static N"), shown in the Pass view. Meow2Renderer's Stats window counts how often static maps were drawn again, which should rarely rise.

## Not yet

- Aerial perspective, clouds, a night sky; punctual lights from `KHR_lights_punctual`.
- Cooking at import time (with M7's asset files) rather than on first load; BC6H for HDR images.
- Rotated texture transforms don't rotate the tangent frame, so a rotated normal map is lit slightly wrong (rare in practice).
- Exact MikkTSpace tangents (needs a library, with approval).
- Shadows from other lights (point and spot lights come with the editor); contact shadows; soft shadows that widen with distance (PCSS) or virtual shadow maps; meshlets for shadow casters.
- Occlusion culling of meshlets against a depth pyramid (HZB, M13); levels of detail; meshlets cooked into files with the mesh at import (M7) rather than built on load.

## Tests

| Test | What it checks |
|---|---|
| `Visual.Backends/DeferredTest.GBufferHoldsTheSurface/*` | on the Box sample, the G-buffer holds its red albedo, a unit normal, metallic 0 and roughness 1, and stays empty in the background |
| `Visual.Backends/DeferredTest.PositionFromDepthLiesOnTheSurface/*` | every position rebuilt from depth lies on the Box's surface (within 5 mm) |
| `Visual.Samples/GoldenImageTest.MatchesGolden/*`, `*_forward` | the 25 samples match the same goldens deferred and forward |
| `Renderer.ShadowCascades.*` | splits rise from the near plane to the shadow distance; every corner of every slice lies in its cascade's box, wherever the camera looks; small camera moves keep a cascade, large ones move it by whole texels, a new sun moves all |
| `RhiGpu*.GpuSceneTest.BoxShadowsTheGround` | a box over a slab, sun nearly overhead: the slab under the box is darker than beside it, forward and deferred |
| `RhiGpu*.GpuSceneTest.StaticShadowsAreCached` | static maps drawn once; again only for a new sun or a moved static caster; a movable caster moves its shadow without redrawing them |
| `RhiGpu*.GpuSceneTest.ShadowGraphKeepsItsShape` | the same passes whether the static cache is drawn or not; skipped while it's valid |
| `Renderer.RenderGraph.SkippedPassesAreKeptButNotRun` | a skipped pass stays in the graph, places no barriers, changes no state, and shares the list before it |
| `RhiGpu*.SkyAtmosphereTest.TransmittanceFallsTowardsTheHorizon` | the transmittance LUT: more light gets through straight up than along the horizon, and red more than blue |
| `RhiGpu*.SkyAtmosphereTest.SkyIsBlueAtNoonAndRedAtSunset` | the sky-view LUT: a blue zenith with the sun overhead; red-orange towards a setting sun |
| `RhiGpu*.SkyAtmosphereTest.SurvivesABackendSwitch` | after the device is re-created, the LUTs are rebuilt with the same values |
| `RhiGpu*.GpuSceneTest.SmoothMetalReflectsTheSky` | a mirror-like metal quad reflects the blue sky when tilted up and the warm ground when tilted down; the sky shows behind |
| `RhiGpu*.RhiDrawTest.WritesEachMipThroughItsStorageView` | compute writes each mip level through its own storage slot |
| `RhiGpu*.GpuSceneTest.MirroredAndDoubleSidedInstancesDraw` | a quad seen from behind is culled one-sided and drawn double-sided; a mirrored quad lands in the mirrored list and shows its front |
| `RhiGpu*.GpuSceneTest.BlendedMaterialMixesWithWhatsBehind` | a half-transparent red quad over a green one gives both colours, and stays out of the indirect lists |
| `RhiGpu*.GpuSceneTest.NormalMapChangesLighting` | a normal map tilted towards the sun brightens the surface |
| `RhiGpu*.RhiDrawTest.CopiesEveryMipLevel` | both levels of a texture are uploaded from one buffer at offsets, and sampled at the right distance |
| `RhiGpu*.RhiStatesTest.FrontCullingSkipsFrontFaces` | `CullMode::Front` removes a clockwise triangle, `Back` keeps it |
| `Assets.CookedTexture.*` | `.m2texture` round trip and bad files; BC7 and BC5 within 2/255 on every level, small mips included; non-block sizes stay RGBA8; jobs give the same blocks; the cache key |
| `RhiGpu*.RhiDrawTest.SamplesBlockCompressedMips` | BC7 levels uploaded from one buffer, sampled at the right distance, a sub-block level included |
| `RhiGpu*.GpuSceneTest.TextureTransformMovesTheTexture` | an offset of half the texture shows its other half |
| `Assets.Image.MipChainHalvesToOnePixel`, `Assets.Image.SrgbMipsAverageInLinearLight` | the chain's sizes, and colour averaging in linear light |
| `Renderer.MeshProcessing.OptimizeKeepsTriangles` | a scrambled grid keeps the same triangles, each wound the same way; repeated vertices are merged |
| `Renderer.MeshProcessing.OptimizeImprovesVertexCache` | vertices shaded per triangle fall by more than a quarter, below 1 |
| `Renderer.MeshProcessing.MeshletsCoverEveryTriangleOnce` | every triangle is in exactly one meshlet, wound the same way |
| `Renderer.MeshProcessing.MeshletsRespectLimits` | ≤ 64 vertices and ≤ 124 triangles each; the sphere holds them; a flat grid's cone faces its way |
| `RhiGpu*.GpuSceneTest.MeshletPathMatchesIndirect` | spheres (one turned, one mirrored), a double-sided quad from behind and a cube: the same image through both paths |
| `RhiGpu*.GpuSceneTest.MeshletCullingSkipsHiddenClusters` | a grid mostly outside the view draws under half its meshlets; a sphere's far side is culled by the cones; a double-sided sphere isn't; the image matches the indirect path each time |
| `RhiGpu*.GpuSceneTest.MeshletViewColoursClusters` | the Meshlets view gives neighbouring clusters different colours, and is grey on the indirect path |
| `RhiGpu*.RenderDeviceTest.MeshletsSurviveBackendSwitch` | the meshlet path draws the same meshlets and image after the device is re-created |

By hand, on both backends: MetalRoughSpheres, NormalTangentTest, NegativeScaleTest, AlphaBlendModeTest, TextureSettingsTest and DamagedHelmet, against their reference screenshots. DragonAttenuation and Sponza through both paths, a backend switch and the Meshlets view, validation clean. All 25 suggested samples have golden images on both backends ([Testing](Testing.md#golden-images)).
