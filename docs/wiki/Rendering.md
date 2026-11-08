# Rendering: materials and shading

How `Renderer::GpuScene` draws a scene: glTF's metallic-roughness materials, lit by one sun and a physically based sky (image-based lighting), on Vulkan and D3D12 alike. The buffers, culling and indirect drawing underneath are in [Rhi → GPU-driven drawing](Rhi.md#gpu-driven-drawing); how glTF files get here is in [Gltf](Gltf.md).

- **Status:** 🟨 M6. PBR, normal maps, mipmaps, samplers, double-sided, mirrored and blended surfaces (#174); Hillaire's sky and image-based lighting from it (#180). Cooked `.m2texture` files (DirectXTex, BC7) and texture transforms come next.

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

## Not yet

- Aerial perspective, clouds, a night sky; punctual lights from `KHR_lights_punctual`.
- Cooked `.m2texture` files with GPU compression (BC7), `KHR_texture_transform`.
- Exact MikkTSpace tangents (needs a library, with approval).
- Shadows.

## Tests

| Test | What it checks |
|---|---|
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
| `Assets.Image.MipChainHalvesToOnePixel`, `Assets.Image.SrgbMipsAverageInLinearLight` | the chain's sizes, and colour averaging in linear light |

By hand, on both backends: MetalRoughSpheres, NormalTangentTest, NegativeScaleTest, AlphaBlendModeTest, TextureSettingsTest and DamagedHelmet, against their reference screenshots. Golden-image tests follow in M6 PR 5.
