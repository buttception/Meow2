/// The last step of the frame before the UI: the scene's HDR colour (linear light, exposure already applied) through
/// the Khronos PBR Neutral tone curve, into the swapchain, whose sRGB view encodes the result.
/// g_push.instance is the heap index of the HDR texture; it's read texel for texel (same size), so no sampler.

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

float4 psMain(VertexOutput input) : SV_Target
{
    const Texture2D<float4> hdr = ResourceDescriptorHeap[g_push.instance];
    const float3 color = hdr.Load(int3(input.position.xy, 0)).rgb;
    return float4(toneMapNeutral(max(color, 0.0)), 1.0);
}
