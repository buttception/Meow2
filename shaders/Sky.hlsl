// The sky (SkyAtmosphere.cpp): Hillaire's atmosphere LUTs, the environment the scene is lit by, and the sky drawn
// behind the scene. Every entry reads a SkyConstants record at g_push.userData; the compute entries that write one
// mip level take its storage index in g_push.instance, the level in g_push.instanceBuffer and its size in pixels in
// g_push.frameBuffer.
#include "Common.hlsli"
#include "Atmosphere.hlsli"

static const uint c_prefilterSamples = 64;

SkyConstants skyConstants()
{
    return loadElement<SkyConstants>(g_push.userData, 0);
}

// ---- Transmittance LUT ---------------------------------------------------------------------------------------------

/// How much light survives from a height to the top of the atmosphere, for every height and zenith angle.
[numthreads(8, 8, 1)]
void csTransmittance(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_transmittanceSize))
    {
        return;
    }
    const SkyConstants sky = skyConstants();
    const float2 params = transmittanceParams((float2(id.xy) + 0.5) / float2(c_transmittanceSize));
    const float3 origin = float3(0.0, params.x, 0.0);
    const float3 direction = float3(sqrt(max(0.0, 1.0 - params.y * params.y)), params.y, 0.0);
    // Rays start inside the atmosphere, so they always leave through its top.
    const float distance = raySphereNearest(origin, direction, c_topRadius);
    const uint c_steps = 40;
    const float stepLength = distance / c_steps;
    float3 opticalDepth = 0.0;
    for (uint i = 0; i < c_steps; ++i)
    {
        const float3 position = origin + (i + 0.5) * stepLength * direction;
        opticalDepth += sampleMedium(length(position) - c_bottomRadius).extinction * stepLength;
    }
    RWTexture2D<float4> output = ResourceDescriptorHeap[sky.transmittanceStorage];
    output[id.xy] = float4(exp(-opticalDepth), 1.0);
}

// ---- Scattering along a ray ----------------------------------------------------------------------------------------

/// Light scattered towards the viewer along a ray, and what's left of the light behind it.
struct Scattered
{
    float3 luminance;    ///< In-scattered light reaching the ray's origin.
    float3 multiAsOne;   ///< For the multiple-scattering LUT: scattering integrated as if all incoming light were 1.
};

/// Marches a ray through the atmosphere (Hillaire, section 4). With `uniformPhase` (the multiple-scattering LUT)
/// light scatters equally in every direction and multiple scattering isn't added; otherwise (the sky-view LUT) it
/// uses the Rayleigh and Mie phase functions and adds multiple scattering from its LUT. Ground the ray hits adds
/// the sun's light reflected by it.
Scattered integrate(SkyConstants sky, float3 origin, float3 direction, float3 sunDirection, float3 illuminance,
                    uint steps, bool uniformPhase)
{
    Scattered result;
    result.luminance = 0.0;
    result.multiAsOne = 0.0;
    const float groundDistance = raySphereNearest(origin, direction, c_bottomRadius);
    const float topDistance = raySphereNearest(origin, direction, c_topRadius);
    const bool hitsGround = groundDistance > 0.0;
    const float distance = hitsGround ? groundDistance : max(topDistance, 0.0);
    if (distance <= 0.0)
    {
        return result;
    }
    const float cosTheta = dot(direction, sunDirection);
    const float phaseRayleigh = uniformPhase ? 1.0 / (4.0 * c_pi) : rayleighPhase(cosTheta);
    const float phaseMie = uniformPhase ? 1.0 / (4.0 * c_pi) : miePhase(cosTheta);
    const float stepLength = distance / steps;
    float3 throughput = 1.0;
    for (uint i = 0; i < steps; ++i)
    {
        // 0.3 rather than 0.5 into each step, as Hillaire does: a slight bias towards the viewer, where density is
        // highest for most rays.
        const float3 position = origin + (i + 0.3) * stepLength * direction;
        const float height = length(position);
        const float3 up = position / height;
        const Medium medium = sampleMedium(height - c_bottomRadius);
        const float3 scattering = medium.rayleigh + medium.mie;
        const float3 stepTransmittance = exp(-medium.extinction * stepLength);
        const float sunZenithCos = dot(sunDirection, up);
        const float3 toSun = sampleTransmittance(sky.transmittanceLut, sky.linearSampler, height, sunZenithCos);
        // The planet's shadow: past the terminator, no direct sunlight.
        const float shadow = raySphereNearest(position + up * 0.01, sunDirection, c_bottomRadius) >= 0.0 ? 0.0 : 1.0;
        float3 multiple = 0.0;
        if (!uniformPhase)
        {
            multiple = sampleMultiScattering(sky.multiScatteringLut, sky.linearSampler, height, sunZenithCos) *
                       scattering;
        }
        const float3 source =
            illuminance * (shadow * toSun * (medium.rayleigh * phaseRayleigh + medium.mie * phaseMie) + multiple);
        // Integrated analytically over the step (energy-conserving, Hillaire eq. 7), not just source × length.
        const float3 safeExtinction = max(medium.extinction, 1e-6);
        result.luminance += throughput * (source - source * stepTransmittance) / safeExtinction;
        result.multiAsOne += throughput * (scattering - scattering * stepTransmittance) / safeExtinction;
        throughput *= stepTransmittance;
    }
    if (hitsGround)
    {
        // Lambertian ground lit by the sun, seen through what's left of the atmosphere.
        const float3 position = origin + distance * direction;
        const float3 up = normalize(position);
        const float sunZenithCos = dot(sunDirection, up);
        const float3 toSun = sampleTransmittance(sky.transmittanceLut, sky.linearSampler, length(position), sunZenithCos);
        result.luminance += illuminance * toSun * throughput * saturate(sunZenithCos) * sky.groundAlbedo.rgb / c_pi;
    }
    return result;
}

// ---- Multiple-scattering LUT ---------------------------------------------------------------------------------------

groupshared float3 s_luminance[64];
groupshared float3 s_multiAsOne[64];

/// Light that has scattered many times (Hillaire, section 5.2): for each height and sun angle, the second-order
/// scattering ψ seen from all 64 directions, made infinite-order by the geometric series ψ / (1 - f_ms). One thread
/// group per texel, one thread per direction.
[numthreads(64, 1, 1)]
void csMultiScattering(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    const SkyConstants sky = skyConstants();
    const float2 uv = subUvToUnit((float2(group.xy) + 0.5) / float2(c_multiScatteringSize), float2(c_multiScatteringSize));
    const float sunZenithCos = uv.x * 2.0 - 1.0;
    const float viewHeight = c_bottomRadius + saturate(uv.y) * (c_topRadius - c_bottomRadius) + 0.01;
    const float3 origin = float3(0.0, viewHeight, 0.0);
    const float3 sunDirection = float3(sqrt(max(0.0, 1.0 - sunZenithCos * sunZenithCos)), sunZenithCos, 0.0);
    // 8 × 8 directions spread evenly over the sphere.
    const float a = ((thread / 8) + 0.5) / 8.0;
    const float b = ((thread % 8) + 0.5) / 8.0;
    const float theta = 2.0 * c_pi * a;
    const float phi = acos(1.0 - 2.0 * b);
    const float3 direction = float3(cos(theta) * sin(phi), cos(phi), sin(theta) * sin(phi));
    const Scattered scattered = integrate(sky, origin, direction, sunDirection, 1.0, 20, true);
    s_luminance[thread] = scattered.luminance;
    s_multiAsOne[thread] = scattered.multiAsOne;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (thread < stride)
        {
            s_luminance[thread] += s_luminance[thread + stride];
            s_multiAsOne[thread] += s_multiAsOne[thread + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (thread == 0)
    {
        // The averages over the sphere are the integrals times the isotropic phase function (4π / 64 × 1 / 4π).
        const float3 secondOrder = s_luminance[0] / 64.0;
        const float3 transfer = s_multiAsOne[0] / 64.0;
        RWTexture2D<float4> output = ResourceDescriptorHeap[sky.multiScatteringStorage];
        output[group.xy] = float4(secondOrder / max(1.0 - transfer, 1e-4), 1.0);
    }
}

// ---- Sky-view LUT --------------------------------------------------------------------------------------------------

/// The whole sky seen from the camera, by zenith and azimuth from the sun. Rebuilt every frame: it depends on the
/// sun and the camera's altitude, and is cheap at this size.
[numthreads(8, 8, 1)]
void csSkyView(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_skyViewSize))
    {
        return;
    }
    const SkyConstants sky = skyConstants();
    const float viewHeight = viewHeightFromAltitude(sky.sunIlluminance.w);
    const float2 params = skyViewParams(viewHeight, (float2(id.xy) + 0.5) / float2(c_skyViewSize));
    const float viewZenithSin = sqrt(max(0.0, 1.0 - params.x * params.x));
    // A frame where the sun lies in the XY plane: the LUT is relative to the sun's azimuth.
    const float3 direction = float3(viewZenithSin * params.y, params.x,
                                    viewZenithSin * sqrt(max(0.0, 1.0 - params.y * params.y)));
    const float sunZenithCos = sky.sunDirection.y;
    const float3 sunDirection = float3(sqrt(max(0.0, 1.0 - sunZenithCos * sunZenithCos)), sunZenithCos, 0.0);
    const Scattered scattered = integrate(sky, float3(0.0, viewHeight, 0.0), direction, sunDirection,
                                          sky.sunIlluminance.rgb, 30, false);
    RWTexture2D<float4> output = ResourceDescriptorHeap[sky.skyViewStorage];
    output[id.xy] = float4(scattered.luminance, 1.0);
}

// ---- Environment and image-based lighting --------------------------------------------------------------------------

/// One mip level of the environment map: the sky (without the sun's disc, which the shading adds as a light) in an
/// octahedral layout. Each level samples the sky-view LUT directly; the sky is smooth enough that this needs no
/// filtering, and it avoids reading one level while writing the next.
[numthreads(8, 8, 1)]
void csEnvironment(uint3 id : SV_DispatchThreadID)
{
    const uint size = g_push.frameBuffer;
    if (any(id.xy >= size))
    {
        return;
    }
    const SkyConstants sky = skyConstants();
    const float3 direction = octahedralDirection((float2(id.xy) + 0.5) / size);
    const float3 luminance = sampleSkyView(sky.skyViewLut, sky.linearSampler,
                                           viewHeightFromAltitude(sky.sunIlluminance.w), direction,
                                           sky.sunDirection.xyz);
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_push.instance];
    output[id.xy] = float4(luminance, 1.0);
}

/// Van der Corput radical inverse in base 2: the second coordinate of the Hammersley point set.
float radicalInverse(uint bits)
{
    return float(reversebits(bits)) * 2.3283064365386963e-10;
}

/// A GGX-distributed half vector around n, for importance sampling.
float3 sampleGgx(float2 xi, float3 n, float alpha)
{
    const float phi = 2.0 * c_pi * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    const float3 helper = abs(n.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(helper, n));
    const float3 bitangent = cross(n, tangent);
    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

/// One level of the prefiltered environment (the split-sum's first half, Karis 2013): the environment blurred by a
/// GGX lobe of roughness level / (levels - 1), assuming the view direction is the normal. Filtered importance
/// sampling reads a blurrier mip for samples that stand for more of the sphere, so 64 samples don't sparkle.
[numthreads(8, 8, 1)]
void csPrefilter(uint3 id : SV_DispatchThreadID)
{
    const uint size = g_push.frameBuffer;
    if (any(id.xy >= size))
    {
        return;
    }
    const SkyConstants sky = skyConstants();
    const uint level = g_push.instanceBuffer;
    const float3 n = octahedralDirection((float2(id.xy) + 0.5) / size);
    Texture2D<float4> environment = ResourceDescriptorHeap[sky.environment];
    const SamplerState linearSampler = heapSampler(sky.linearSampler);
    float3 result;
    if (level == 0)
    {
        result = environment.SampleLevel(linearSampler, octahedralUv(n), 0.0).rgb;
    }
    else
    {
        const float roughness = float(level) / float(c_prefilteredLevels - 1);
        const float alpha = max(roughness * roughness, 1e-3);
        uint environmentSize = 0;
        uint height = 0;
        uint levels = 0;
        environment.GetDimensions(0, environmentSize, height, levels);
        const float texelSolidAngle = 4.0 * c_pi / float(environmentSize * environmentSize);
        float3 sum = 0.0;
        float weight = 0.0;
        for (uint i = 0; i < c_prefilterSamples; ++i)
        {
            const float2 xi = float2(float(i) / c_prefilterSamples, radicalInverse(i));
            const float3 h = sampleGgx(xi, n, alpha);
            const float3 l = 2.0 * dot(n, h) * h - n;
            const float nDotL = dot(n, l);
            if (nDotL <= 0.0)
            {
                continue;
            }
            // With v = n the pdf of l is D / 4.
            const float nDotH = saturate(dot(n, h));
            const float d = (nDotH * nDotH * (alpha * alpha - 1.0) + 1.0);
            const float distribution = alpha * alpha / (c_pi * d * d);
            const float sampleSolidAngle = 1.0 / (c_prefilterSamples * distribution * 0.25 + 1e-4);
            const float lod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, float(levels - 1));
            sum += environment.SampleLevel(linearSampler, octahedralUv(l), lod).rgb * nDotL;
            weight += nDotL;
        }
        result = sum / max(weight, 1e-4);
    }
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_push.instance];
    output[id.xy] = float4(result, 1.0);
}

groupshared float3 s_harmonics[9][256];

/// The real spherical harmonics up to band 2 (9 functions) in a direction.
void harmonicBasis(float3 d, out float basis[9])
{
    basis[0] = 0.282095;
    basis[1] = 0.488603 * d.y;
    basis[2] = 0.488603 * d.z;
    basis[3] = 0.488603 * d.x;
    basis[4] = 1.092548 * d.x * d.y;
    basis[5] = 1.092548 * d.y * d.z;
    basis[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    basis[7] = 1.092548 * d.x * d.z;
    basis[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

/// Diffuse light from the environment as 9 spherical-harmonic coefficients (Ramamoorthi and Hanrahan 2001): its
/// 16 × 16 mip projected onto the basis, each texel weighted by the solid angle it covers, then convolved with the
/// cosine lobe, so evaluating them in a normal gives the irradiance there. One group of 256 threads, one per texel.
[numthreads(16, 16, 1)]
void csIrradiance(uint3 id : SV_GroupThreadID, uint thread : SV_GroupIndex)
{
    const SkyConstants sky = skyConstants();
    const float lod = float(g_push.instance);
    const float texel = 1.0 / 16.0;
    const float2 uv = (float2(id.xy) + 0.5) * texel;
    const float3 direction = octahedralDirection(uv);
    // The texel's solid angle, from the directions at three of its corners.
    const float3 corner = octahedralDirection(uv - 0.5 * texel);
    const float3 right = octahedralDirection(uv + float2(0.5, -0.5) * texel);
    const float3 down = octahedralDirection(uv + float2(-0.5, 0.5) * texel);
    const float solidAngle = length(cross(right - corner, down - corner));
    Texture2D<float4> environment = ResourceDescriptorHeap[sky.environment];
    const float3 radiance = environment.SampleLevel(heapSampler(sky.linearSampler), uv, lod).rgb;
    float basis[9];
    harmonicBasis(direction, basis);
    for (uint k = 0; k < 9; ++k)
    {
        s_harmonics[k][thread] = radiance * basis[k] * solidAngle;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (thread < stride)
        {
            for (uint k = 0; k < 9; ++k)
            {
                s_harmonics[k][thread] += s_harmonics[k][thread + stride];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (thread < 9)
    {
        // The cosine lobe's band factors: π, 2π/3, π/4.
        const float band = thread == 0 ? c_pi : (thread < 4 ? 2.0 * c_pi / 3.0 : c_pi / 4.0);
        RWByteAddressBuffer output = ResourceDescriptorHeap[sky.irradianceStorage];
        output.Store<float4>(thread * 16, float4(s_harmonics[thread][0] * band, 0.0));
    }
}

// ---- The sky behind the scene --------------------------------------------------------------------------------------

struct SkyVertex
{
    float4 position : SV_Position;
    float2 clip : TEXCOORD0;
};

/// A triangle covering the screen at depth 0: with reversed Z that's infinitely far, so it only passes the depth
/// test where nothing was drawn.
SkyVertex vsSky(uint vertexId : SV_VertexID)
{
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    SkyVertex output;
    output.clip = corner * 2.0 - 1.0;
    output.position = float4(output.clip, 0.0, 1.0);
    return output;
}

float4 psSky(SkyVertex input) : SV_Target
{
    const SkyConstants sky = skyConstants();
    const FrameConstants frame = frameConstants();
    // Two points along the pixel's ray (depth 1 is the near plane in reversed Z, 0.5 further out): their difference
    // is the view direction, whether or not the view matrix moves the camera away from the origin.
    const float4x4 inverseViewProjection = MEOW2_ROWS(sky, inverseViewProjection);
    const float4 nearPoint = mul(float4(input.clip, 1.0, 1.0), inverseViewProjection);
    const float4 farPoint = mul(float4(input.clip, 0.5, 1.0), inverseViewProjection);
    const float3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    const float viewHeight = viewHeightFromAltitude(sky.sunIlluminance.w);
    const float3 sun = sky.sunDirection.xyz;
    float3 luminance = sampleSkyView(sky.skyViewLut, sky.linearSampler, viewHeight, direction, sun);
    // The sun's disc, unless the ground is in the way, dimmed by the air in front of it.
    const float angularRadius = sky.sunDirection.w;
    const float3 origin = float3(0.0, viewHeight, 0.0);
    if (dot(direction, sun) > cos(angularRadius) && raySphereNearest(origin, direction, c_bottomRadius) < 0.0)
    {
        const float3 toSun = sampleTransmittance(sky.transmittanceLut, sky.linearSampler, viewHeight, sun.y);
        luminance += sky.sunIlluminance.rgb * toSun / (c_pi * angularRadius * angularRadius);
    }
    // Exposure and the tone curve, as the scene's shading does.
    return float4(toneMapNeutral(min(luminance * frame.sunColor.w, 60000.0)), 1.0);
}
