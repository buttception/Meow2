// The atmosphere model of Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique" (EGSR 2020), shared by the sky's passes (Sky.hlsl) and the scene's shading (GpuDriven.hlsl).
//
// The planet is a sphere of radius c_bottomRadius with an atmosphere up to c_topRadius, in kilometres. Light is
// scattered by air molecules (Rayleigh: strongly by wavelength, which makes the sky blue), by aerosols (Mie: nearly
// grey, forward-peaked, the haze around the sun) and absorbed by ozone (which keeps the zenith blue at sunset).
// Everything here is in the planet's frame: its centre at the origin, +Y up through the camera.
#ifndef MEOW2_ATMOSPHERE_HLSLI
#define MEOW2_ATMOSPHERE_HLSLI

static const float c_pi = 3.14159265;

// Earth, from Hillaire's paper (values per kilometre).
static const float c_bottomRadius = 6360.0;
static const float c_topRadius = 6460.0;
static const float3 c_rayleighScattering = float3(5.802, 13.558, 33.1) * 1e-3;
static const float c_rayleighScaleHeight = 8.0;
static const float c_mieScattering = 3.996e-3;
static const float c_mieExtinction = 4.440e-3;
static const float c_mieScaleHeight = 1.2;
static const float c_mieAnisotropy = 0.8;
static const float3 c_ozoneAbsorption = float3(0.650, 1.881, 0.085) * 1e-3;
static const float c_ozoneCenter = 25.0;    // km: the ozone layer's peak
static const float c_ozoneHalfWidth = 15.0; // km: falls linearly to 0 this far either side

// LUT sizes, also used by SkyAtmosphere.cpp.
static const uint2 c_transmittanceSize = uint2(256, 64);
static const uint2 c_multiScatteringSize = uint2(32, 32);
static const uint2 c_skyViewSize = uint2(192, 108);
static const uint c_environmentSize = 128;  // the environment map, octahedral, every mip down to 1 × 1
static const uint c_prefilteredLevels = 6;  // the prefiltered map: 128 down to 4 pixels, roughness 0 to 1

/// What the medium does at one height: how much light it scatters (per kind) and removes in all.
struct Medium
{
    float3 rayleigh;   ///< Rayleigh scattering coefficient.
    float mie;         ///< Mie scattering coefficient (grey).
    float3 extinction; ///< Scattering plus absorption, all kinds.
};

Medium sampleMedium(float height)
{
    const float rayleighDensity = exp(-height / c_rayleighScaleHeight);
    const float mieDensity = exp(-height / c_mieScaleHeight);
    const float ozoneDensity = saturate(1.0 - abs(height - c_ozoneCenter) / c_ozoneHalfWidth);
    Medium medium;
    medium.rayleigh = c_rayleighScattering * rayleighDensity;
    medium.mie = c_mieScattering * mieDensity;
    medium.extinction = medium.rayleigh + c_mieExtinction * mieDensity + c_ozoneAbsorption * ozoneDensity;
    return medium;
}

/// Rayleigh phase function: how much light scattered by molecules goes off at angle acos(cosTheta).
float rayleighPhase(float cosTheta)
{
    return 3.0 / (16.0 * c_pi) * (1.0 + cosTheta * cosTheta);
}

/// Cornette-Shanks phase function for Mie scattering: strongly forward, so the sky glows around the sun.
float miePhase(float cosTheta)
{
    const float g = c_mieAnisotropy;
    const float k = 3.0 / (8.0 * c_pi) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cosTheta * cosTheta) / pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1e-4), 1.5);
}

/// Distance along a ray to the nearest point where it enters or leaves a sphere at the origin, or -1 if it misses
/// (or the sphere is entirely behind).
float raySphereNearest(float3 origin, float3 direction, float radius)
{
    const float b = dot(origin, direction);
    // |o|² - r² as (|o| - r)(|o| + r): near the ground both squares are ~4e7 km² and their difference a few
    // km², which a float subtraction would lose.
    const float originLength = length(origin);
    const float c = (originLength - radius) * (originLength + radius);
    const float discriminant = b * b - c;
    if (discriminant < 0.0)
    {
        return -1.0;
    }
    const float root = sqrt(discriminant);
    const float near = -b - root;
    const float far = -b + root;
    if (near < 0.0 && far < 0.0)
    {
        return -1.0;
    }
    return near >= 0.0 ? near : far;
}

// A LUT texel's centre maps to its coordinate exactly, so the edges of the parameter range aren't half a texel off.
float2 unitToSubUv(float2 uv, float2 size)
{
    return (uv + 0.5 / size) * (size / (size + 1.0));
}

float2 subUvToUnit(float2 uv, float2 size)
{
    return (uv - 0.5 / size) * (size / (size - 1.0));
}

/// Transmittance LUT coordinates for a height and view zenith (Bruneton's mapping): rows by height, columns by the
/// distance to the top of the atmosphere, which spends the texels where transmittance changes fastest (the horizon).
float2 transmittanceUv(float viewHeight, float viewZenithCos)
{
    const float h = sqrt(c_topRadius * c_topRadius - c_bottomRadius * c_bottomRadius);
    const float rho = sqrt(max(0.0, (viewHeight - c_bottomRadius) * (viewHeight + c_bottomRadius)));
    const float discriminant = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0) + c_topRadius * c_topRadius;
    const float d = max(0.0, -viewHeight * viewZenithCos + sqrt(max(discriminant, 0.0)));
    const float dMin = c_topRadius - viewHeight;
    const float dMax = rho + h;
    return float2((d - dMin) / (dMax - dMin), rho / h);
}

/// The inverse of transmittanceUv(): x is the view height, y the view zenith cosine.
float2 transmittanceParams(float2 uv)
{
    const float h = sqrt(c_topRadius * c_topRadius - c_bottomRadius * c_bottomRadius);
    const float rho = h * uv.y;
    const float viewHeight = sqrt(rho * rho + c_bottomRadius * c_bottomRadius);
    const float dMin = c_topRadius - viewHeight;
    const float dMax = rho + h;
    const float d = dMin + uv.x * (dMax - dMin);
    const float viewZenithCos = d == 0.0 ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * viewHeight * d);
    return float2(viewHeight, clamp(viewZenithCos, -1.0, 1.0));
}

/// Fraction of light that reaches the top of the atmosphere from a height, along a zenith angle.
float3 sampleTransmittance(uint lut, uint linearSampler, float viewHeight, float viewZenithCos)
{
    Texture2D<float4> transmittance = ResourceDescriptorHeap[lut];
    return transmittance.SampleLevel(heapSampler(linearSampler),
                                     transmittanceUv(viewHeight, viewZenithCos), 0.0).rgb;
}

/// Multiple-scattering LUT coordinates: x by the sun's zenith cosine, y by height.
float2 multiScatteringUv(float viewHeight, float sunZenithCos)
{
    const float2 uv = float2(sunZenithCos * 0.5 + 0.5, saturate((viewHeight - c_bottomRadius) /
                                                                (c_topRadius - c_bottomRadius)));
    return unitToSubUv(uv, float2(c_multiScatteringSize));
}

/// Light scattered many times, arriving at a point from everywhere (Hillaire's ψ_ms), per unit of scattering.
float3 sampleMultiScattering(uint lut, uint linearSampler, float viewHeight, float sunZenithCos)
{
    Texture2D<float4> multiScattering = ResourceDescriptorHeap[lut];
    return multiScattering.SampleLevel(heapSampler(linearSampler),
                                       multiScatteringUv(viewHeight, sunZenithCos), 0.0).rgb;
}

/// Sky-view LUT coordinates from a view zenith cosine and the cosine of the view's azimuth from the sun's.
/// Rows squeeze towards the horizon, where the sky changes fastest, and the lower half is the ground.
float2 skyViewUv(float viewHeight, float viewZenithCos, float lightViewCos)
{
    const float horizonDistance = sqrt(max((viewHeight - c_bottomRadius) * (viewHeight + c_bottomRadius), 0.0));
    const float beta = acos(clamp(horizonDistance / viewHeight, -1.0, 1.0));
    const float zenithHorizonAngle = c_pi - beta;
    const float viewZenithAngle = acos(clamp(viewZenithCos, -1.0, 1.0));
    float2 uv;
    if (viewZenithAngle < zenithHorizonAngle) // above the horizon
    {
        float coord = 1.0 - viewZenithAngle / zenithHorizonAngle;
        coord = 1.0 - sqrt(saturate(coord));
        uv.y = coord * 0.5;
    }
    else // the ground
    {
        const float coord = saturate((viewZenithAngle - zenithHorizonAngle) / beta);
        uv.y = sqrt(coord) * 0.5 + 0.5;
    }
    uv.x = sqrt(saturate(-lightViewCos * 0.5 + 0.5));
    return unitToSubUv(uv, float2(c_skyViewSize));
}

/// The inverse of skyViewUv(): x is the view zenith cosine, y the cosine of the azimuth from the sun.
float2 skyViewParams(float viewHeight, float2 uv)
{
    uv = subUvToUnit(uv, float2(c_skyViewSize));
    const float horizonDistance = sqrt(max((viewHeight - c_bottomRadius) * (viewHeight + c_bottomRadius), 0.0));
    const float beta = acos(clamp(horizonDistance / viewHeight, -1.0, 1.0));
    const float zenithHorizonAngle = c_pi - beta;
    float viewZenithCos;
    if (uv.y < 0.5)
    {
        float coord = 1.0 - 2.0 * uv.y;
        coord = 1.0 - coord * coord;
        viewZenithCos = cos(zenithHorizonAngle * coord);
    }
    else
    {
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord;
        viewZenithCos = cos(zenithHorizonAngle + beta * coord);
    }
    const float coord = uv.x * uv.x;
    return float2(viewZenithCos, -(coord * 2.0 - 1.0));
}

/// The camera's distance from the planet's centre, from its altitude in kilometres. Kept at least 50 m up: a float
/// at 6360 km resolves about 0.5 m, so lower altitudes would turn the horizon maths into noise. The horizon drops by
/// only 0.2° at that height.
float viewHeightFromAltitude(float altitudeKm)
{
    return c_bottomRadius + max(altitudeKm, 0.05);
}

/// The sky's luminance in a world direction (+Y up), from the sky-view LUT; the sun's azimuth comes from sunDirection.
float3 sampleSkyView(uint lut, uint linearSampler, float viewHeight, float3 direction, float3 sunDirection)
{
    const float viewZenithCos = direction.y;
    const float2 viewFlat = direction.xz;
    const float2 sunFlat = sunDirection.xz;
    const float flatLengths = length(viewFlat) * length(sunFlat);
    const float lightViewCos = flatLengths > 1e-6 ? dot(viewFlat, sunFlat) / flatLengths : 1.0;
    Texture2D<float4> skyView = ResourceDescriptorHeap[lut];
    return skyView.SampleLevel(heapSampler(linearSampler),
                               skyViewUv(viewHeight, viewZenithCos, lightViewCos), 0.0).rgb;
}

/// Octahedral encoding of a unit direction into 0..1 texture coordinates: the sphere folded onto a square, with
/// nearly even texel areas and no poles (the environment map's layout).
float2 octahedralUv(float3 direction)
{
    float3 n = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    float2 p = n.xz;
    if (n.y < 0.0)
    {
        p = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    return p * 0.5 + 0.5;
}

/// The inverse of octahedralUv().
float3 octahedralDirection(float2 uv)
{
    const float2 p = uv * 2.0 - 1.0;
    float3 n = float3(p.x, 1.0 - abs(p.x) - abs(p.y), p.y);
    const float t = saturate(-n.y);
    n.x += n.x >= 0.0 ? -t : t;
    n.z += n.z >= 0.0 ? -t : t;
    return normalize(n);
}

/// Khronos PBR Neutral tone mapping: keeps base colours as authored up to about 0.8, then rolls highlights off to white.
float3 toneMapNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float lowest = min(color.r, min(color.g, color.b));
    const float offset = lowest < 0.08 ? lowest - 6.25 * lowest * lowest : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

#endif
