// Metallic-roughness shading, shared by the forward path (GpuDriven.hlsl's psMain) and the deferred one (Deferred.hlsl's
// psLighting), so both light a surface with exactly the same maths: Cook-Torrance with GGX, the sun, and the sky's
// image-based light (or a flat sky and ground colour without a sky). Also the one function that turns a pixel's depth
// back into a position, used by deferred lighting and by Meow2Renderer's Position debug view.
#ifndef MEOW2_LIGHTING_HLSLI
#define MEOW2_LIGHTING_HLSLI

#include "Common.hlsli"
#include "Atmosphere.hlsli" // c_pi, the sky lookups

/// What shading needs to know about a surface at a pixel: what a G-buffer stores, plus the position (which deferred
/// rebuilds from depth instead of storing).
struct Surface
{
    float3 baseColor; ///< Linear base colour.
    float metallic;   ///< 0..1.
    float roughness;  ///< Perceptual roughness, already clamped to 0.045..1.
    float occlusion;  ///< Ambient occlusion, 0..1.
    float3 emissive;  ///< Emitted light, linear.
    float3 normal;    ///< Unit shading normal, facing the viewer's side.
    float3 position;  ///< In the instances' space (camera relative).
};

/// The parts of a pixel's light, before exposure; the sum (plus emissive) is its colour. Debug views show them apart.
struct LightTerms
{
    float3 sunDiffuse;     ///< Sunlight entering the surface.
    float3 sunSpecular;    ///< Sunlight reflected off it.
    float3 ambientDiffuse; ///< Light from the surroundings, entering.
    float3 ambientSpecular; ///< Light from the surroundings, reflected.
    float3 sun;            ///< The sun's light reaching the ground (after the atmosphere).
    float3 skyDiffuse;     ///< The surroundings' diffuse light in the normal's direction, before the surface's colour.
    float3 view;           ///< Unit vector towards the camera.
    float nDotL;           ///< Normal · sun.
    float nDotV;           ///< Normal · view.
    float nDotH;           ///< Normal · half vector.
    float vDotH;           ///< View · half vector.
    float alpha;           ///< roughness².
};

/// GGX (Trowbridge-Reitz) normal distribution: how many microfacets face along the half vector.
float distributionGgx(float nDotH, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (c_pi * d * d);
}

/// Height-correlated Smith visibility for GGX, with the 1 / (4 n·l n·v) of the specular term folded in.
float visibilitySmithGgx(float nDotL, float nDotV, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float lightTerm = nDotV * sqrt(nDotL * nDotL * (1.0 - alpha2) + alpha2);
    const float viewTerm = nDotL * sqrt(nDotV * nDotV * (1.0 - alpha2) + alpha2);
    return 0.5 / max(lightTerm + viewTerm, 1e-5);
}

/// Schlick's Fresnel: reflectance rises towards 1 at grazing angles.
float3 fresnelSchlick(float3 f0, float vDotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

/// Karis' analytic fit of the split-sum environment BRDF, for the ambient specular without a lookup texture.
float3 environmentBrdf(float3 f0, float nDotV, float roughness)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * scaleBias.x + scaleBias.y;
}

/// Irradiance from the sky in a normal's direction: its 9 spherical-harmonic coefficients (already convolved with
/// the cosine lobe by Sky.hlsl's csIrradiance) evaluated there.
float3 skyIrradiance(uint buffer, float3 n)
{
    const ByteAddressBuffer coefficients = heapBuffer(buffer);
    const float basis[9] = {0.282095,
                            0.488603 * n.y,
                            0.488603 * n.z,
                            0.488603 * n.x,
                            1.092548 * n.x * n.y,
                            1.092548 * n.y * n.z,
                            0.315392 * (3.0 * n.z * n.z - 1.0),
                            1.092548 * n.x * n.z,
                            0.546274 * (n.x * n.x - n.y * n.y)};
    float3 irradiance = 0.0;
    for (uint k = 0; k < 9; ++k)
    {
        irradiance += coefficients.Load<float4>(k * 16).rgb * basis[k];
    }
    return max(irradiance, 0.0);
}

/// How much of a cascade's map at a point is lit (1) or in shadow (0), filtered over the texels around it.
///
/// The point is pushed out along its normal by about a texel first (normal-offset bias): a surface facing the sun at
/// a grazing angle would otherwise shadow itself in stripes (shadow acne), since its own depth is stored at the
/// map's coarser resolution. Then a 3 × 3 tent filter over 4 × 4 texels: each texel is compared with the point's
/// depth (reversed Z: a caster nearer the sun has the larger depth), and the comparisons are weighted so the edge is
/// smooth and moves smoothly as the point does.
float sampleShadowCascade(ShadowConstants shadows, uint index, float3 position, float3 normal)
{
    const ShadowCascadeData cascade = shadows.cascades[index];
    const float3 offset = position + normal * cascade.texelSize * 1.5;
    const float4 clip = mul(float4(offset, 1.0), MEOW2_ROWS(cascade, viewProjection));
    const float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0))
    {
        return 1.0; // outside the cascade's box: nothing known, lit
    }
    const float depth = clip.z + shadows.depthBias;
    const Texture2D<float> map = ResourceDescriptorHeap[NonUniformResourceIndex(cascade.depthTexture)];
    const float size = float(shadows.resolution);
    const float2 texel = uv * size - 0.5;
    const int2 base = int2(floor(texel)) - 1;
    const float2 f = frac(texel);
    // Tent weights along each axis for the 4 texels: (1 − f, 1, 1, f) / 3.
    const float4 wx = float4(1.0 - f.x, 1.0, 1.0, f.x) / 3.0;
    const float4 wy = float4(1.0 - f.y, 1.0, 1.0, f.y) / 3.0;
    float lit = 0.0;
    [unroll]
    for (int y = 0; y < 4; ++y)
    {
        [unroll]
        for (int x = 0; x < 4; ++x)
        {
            const int2 at = clamp(base + int2(x, y), 0, int(shadows.resolution) - 1);
            const float stored = map.Load(int3(at, 0));
            lit += (depth >= stored ? 1.0 : 0.0) * wx[x] * wy[y];
        }
    }
    return lit;
}

/// Which cascade covers a point: the first whose far split is beyond the point's view distance; count when none does.
uint shadowCascadeIndex(FrameConstants frame, ShadowConstants shadows, float3 position)
{
    const float viewZ = mul(float4(position, 1.0), MEOW2_ROWS(frame, view)).z;
    for (uint i = 0; i < shadows.count; ++i)
    {
        if (viewZ < shadows.cascades[i].splitFar)
        {
            return i;
        }
    }
    return shadows.count;
}

/// How much sunlight reaches a point: 1 lit, 0 in the sun's shadow. The cascade is picked by the point's distance from
/// the camera; near a cascade's far end it blends into the next, so the change of resolution doesn't show as a line;
/// past the last, the shadow fades out.
float sunShadow(FrameConstants frame, float3 position, float3 normal)
{
    if (frame.shadowBuffer == 0)
    {
        return 1.0;
    }
    const ShadowConstants shadows = loadElement<ShadowConstants>(frame.shadowBuffer, 0);
    const uint index = shadowCascadeIndex(frame, shadows, position);
    if (index >= shadows.count)
    {
        return 1.0;
    }
    const ShadowCascadeData cascade = shadows.cascades[index];
    const float viewZ = mul(float4(position, 1.0), MEOW2_ROWS(frame, view)).z;
    const float band = (cascade.splitFar - cascade.splitNear) * shadows.blend;
    const float toEnd = (cascade.splitFar - viewZ) / max(band, 1e-4);
    float shadow = sampleShadowCascade(shadows, index, position, normal);
    if (toEnd < 1.0)
    {
        // The last few percent of the cascade: mix in the next one, or, after the last, no shadow at all.
        const float next = index + 1 < shadows.count ? sampleShadowCascade(shadows, index + 1, position, normal) : 1.0;
        shadow = lerp(next, shadow, saturate(toEnd));
    }
    return shadow;
}

/// Lights a surface: Cook-Torrance, Lambert diffuse for the light that enters the surface, GGX specular for what
/// reflects off it. Metals have no diffuse and tint their reflection; dielectrics reflect 4 % at normal incidence.
LightTerms lightSurface(FrameConstants frame, Surface surface)
{
    LightTerms terms;
    terms.alpha = surface.roughness * surface.roughness;
    const float3 diffuseColor = surface.baseColor * (1.0 - surface.metallic);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), surface.baseColor, surface.metallic);
    const float3 normal = surface.normal;

    terms.view = normalize(frame.cameraPosition.xyz - surface.position);
    const float3 light = frame.sunDirection.xyz;
    const float3 halfway = normalize(terms.view + light);
    terms.nDotV = max(dot(normal, terms.view), 1e-4);
    terms.nDotL = saturate(dot(normal, light));
    terms.nDotH = saturate(dot(normal, halfway));
    terms.vDotH = saturate(dot(terms.view, halfway));

    const float3 fresnel = fresnelSchlick(f0, terms.vDotH);
    const float3 specular =
        fresnel * distributionGgx(terms.nDotH, terms.alpha) * visibilitySmithGgx(terms.nDotL, terms.nDotV, terms.alpha);
    const float3 diffuse = (1.0 - fresnel) * diffuseColor / c_pi;
    terms.sun = frame.sunColor.rgb * frame.sunDirection.w;
    const bool sky = frame.environmentMap != 0;
    if (sky)
    {
        // Sunlight reaching the ground has crossed the atmosphere: reddened and dimmed when the sun is low.
        terms.sun *= sampleTransmittance(frame.transmittanceLut, frame.environmentSampler,
                                         viewHeightFromAltitude(frame.groundColor.w), light.y);
    }
    terms.sun *= sunShadow(frame, surface.position, normal); // the sun's cascaded shadow maps
    terms.sunDiffuse = diffuse * terms.sun * terms.nDotL;
    terms.sunSpecular = specular * terms.sun * terms.nDotL;

    // Ambient light from the surroundings. With a sky: image-based lighting, the split-sum approximation (Karis 2013)
    // - diffuse from the sky's spherical harmonics, specular from its environment map blurred for this roughness,
    // weighted by the analytic environment BRDF. Without one: a sky colour above and a ground colour below.
    const float3 reflected = reflect(-terms.view, normal);
    float3 specularAmbient;
    if (sky)
    {
        terms.skyDiffuse = skyIrradiance(frame.irradianceBuffer, normal) / c_pi;
        Texture2D<float4> environment = ResourceDescriptorHeap[frame.environmentMap];
        const float lod = surface.roughness * float(frame.environmentLevels - 1);
        specularAmbient = environment.SampleLevel(heapSampler(frame.environmentSampler),
                                                  octahedralUv(reflected), lod).rgb;
    }
    else
    {
        terms.skyDiffuse = lerp(frame.groundColor.rgb, frame.skyColor.rgb, normal.y * 0.5 + 0.5);
        specularAmbient = lerp(frame.groundColor.rgb, frame.skyColor.rgb, reflected.y * 0.5 + 0.5);
    }
    terms.ambientDiffuse = diffuseColor * terms.skyDiffuse * surface.occlusion;
    terms.ambientSpecular =
        environmentBrdf(f0, terms.nDotV, surface.roughness) * specularAmbient * surface.occlusion;
    return terms;
}

/// A surface's colour as the scene shows it: every light term and the emitted light, times the exposure. In linear
/// light: the tone curve comes later, in its own pass (ToneMap.hlsl).
float3 shadeSurface(FrameConstants frame, Surface surface)
{
    const LightTerms terms = lightSurface(frame, surface);
    const float3 color =
        terms.sunDiffuse + terms.sunSpecular + terms.ambientDiffuse + terms.ambientSpecular + surface.emissive;
    return color * frame.sunColor.w;
}

/// The position a pixel's depth stands for: the pixel's centre and its depth, back through the inverse of the
/// view-projection. This is how deferred lighting gets a position without storing one.
/// @param pixel                  The pixel (its top-left corner; the centre is half a pixel in).
/// @param size                   The target's size in pixels.
/// @param depth                  Its depth (reversed Z: 1 at the near plane, 0 at infinity; 0 means nothing drawn).
/// @param inverseViewProjection  Clip space → the instances' space.
float3 positionFromDepth(uint2 pixel, float2 size, float depth, float4x4 inverseViewProjection)
{
    // Clip space: x right, y up (D3D's convention; Vulkan's flipped viewport gives the same), depth as stored.
    const float2 ndc = float2((float(pixel.x) + 0.5) / size.x * 2.0 - 1.0, 1.0 - (float(pixel.y) + 0.5) / size.y * 2.0);
    const float4 position = mul(float4(ndc, depth, 1.0), inverseViewProjection);
    return position.xyz / position.w;
}

#endif
