# Rendering: materials and shading

How `Renderer::GpuScene` draws a scene: glTF's metallic-roughness materials, lit by one sun and a sky/ground ambient, on Vulkan and D3D12 alike. The buffers, culling and indirect drawing underneath are in [Rhi → GPU-driven drawing](Rhi.md#gpu-driven-drawing); how glTF files get here is in [Gltf](Gltf.md).

- **Status:** 🟨 M6. PBR, normal maps, mipmaps, samplers, double-sided, mirrored and blended surfaces (#174). Image-based lighting, cooked `.m2texture` files and texture transforms come next.

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
- **Ambient:** a sky colour above and a ground colour below. It's blended by where the normal points (diffuse), or by where the reflection points (specular, weighted by Karis' fit of the split-sum environment BRDF). That stands in for image-based lighting, which needs an environment map, until the next PR.

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

`addSampler()` returns an existing sampler when an identical one was added before; glTF files repeat the same few. glTF's mirrored repeat maps to `SamplerAddress::Mirror`.

## Not yet

- Image-based lighting (an environment map for reflections and ambient light), punctual lights from `KHR_lights_punctual`.
- Cooked `.m2texture` files with GPU compression (BC7), `KHR_texture_transform`.
- Exact MikkTSpace tangents (needs a library, with approval).
- Shadows.

## Tests

| Test | What it checks |
|---|---|
| `RhiGpu*.GpuSceneTest.MirroredAndDoubleSidedInstancesDraw` | a quad seen from behind is culled one-sided and drawn double-sided; a mirrored quad lands in the mirrored list and shows its front |
| `RhiGpu*.GpuSceneTest.BlendedMaterialMixesWithWhatsBehind` | a half-transparent red quad over a green one gives both colours, and stays out of the indirect lists |
| `RhiGpu*.GpuSceneTest.NormalMapChangesLighting` | a normal map tilted towards the sun brightens the surface |
| `RhiGpu*.RhiDrawTest.CopiesEveryMipLevel` | both levels of a texture are uploaded from one buffer at offsets, and sampled at the right distance |
| `RhiGpu*.RhiStatesTest.FrontCullingSkipsFrontFaces` | `CullMode::Front` removes a clockwise triangle, `Back` keeps it |
| `Assets.Image.MipChainHalvesToOnePixel`, `Assets.Image.SrgbMipsAverageInLinearLight` | the chain's sizes, and colour averaging in linear light |

By hand, on both backends: MetalRoughSpheres, NormalTangentTest, NegativeScaleTest, AlphaBlendModeTest, TextureSettingsTest and DamagedHelmet, against their reference screenshots. Golden-image tests follow in M6 PR 5.
