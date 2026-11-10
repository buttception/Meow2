/// The last step of the frame before the UI: the scene's HDR colour (linear light, exposure already applied) through
/// the Khronos PBR Neutral tone curve, into the swapchain, whose sRGB view encodes the result.
/// g_push.instance is the heap index of the HDR texture; it's read texel for texel (same size), so no sampler.
///
/// DEBUG_OUTPUT = 1 is Meow2Renderer's debug present (games never build it): g_push.userData 0 shows debug-view
/// colours as they are (no tone curve), 1 turns an overdraw count into a heat map.

#include "Common.hlsli"
#include "Atmosphere.hlsli" // toneMapNeutral

struct VertexOutput
{
    float4 position : SV_Position;
};

/// One triangle that covers the screen: (-1, -1), (3, -1), (-1, 3).
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

#if DEBUG_OUTPUT
/// Overdraw count → colour: black (none), blue (1), green (2), yellow (4), red (8), white (16 or more).
float3 heat(float count)
{
    const float3 stops[6] = {float3(0, 0, 0), float3(0, 0.2, 1), float3(0, 0.9, 0.2), float3(1, 0.9, 0), float3(1, 0, 0),
                             float3(1, 1, 1)};
    const float t = count <= 0.0 ? 0.0 : 1.0 + log2(count); // 1 → 1, 2 → 2, 4 → 3, 8 → 4, 16 → 5
    const uint low = min(uint(t), 4u);
    return lerp(stops[low], stops[low + 1], saturate(t - float(low)));
}
#endif

float4 psMain(VertexOutput input) : SV_Target
{
    const Texture2D<float4> hdr = ResourceDescriptorHeap[g_push.instance];
    const float3 color = hdr.Load(int3(input.position.xy, 0)).rgb;
#if DEBUG_OUTPUT
    return float4(g_push.userData == 1 ? heat(color.r * 16.0) : saturate(color), 1.0); // overdraw adds 1/16 a layer
#else
    return float4(toneMapNeutral(max(color, 0.0)), 1.0);
#endif
}
