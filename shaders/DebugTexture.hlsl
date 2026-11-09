/// Meow2Renderer's texture inspector (never in games): full-screen passes over a texture the render graph wrote.
/// - psCopy: the texel as it is, into a float snapshot of the same size (frame capture keeps raw values, so the range
///   can change afterwards). g_push.instance is the source's heap index.
/// - psDisplay: a value range and channels into a displayable sRGB image. g_push.frameBuffer is a buffer of
///   DebugDisplay records, g_push.instance the record.

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

/// One triangle that covers the target: (-1, -1), (3, -1), (-1, 3); UV 0..1 across the target, top left at 0.
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    // Clip-space x -1..1 is U 0..1; clip-space +Y is up while +V is down.
    output.uv = float2(corner.x, 1.0 - corner.y);
    return output;
}

float4 psCopy(VertexOutput input) : SV_Target
{
    const Texture2D<float4> source = ResourceDescriptorHeap[g_push.instance];
    return source.Load(int3(input.position.xy, 0));
}

float4 psDisplay(VertexOutput input) : SV_Target
{
    const DebugDisplay display = loadElement<DebugDisplay>(g_push.frameBuffer, g_push.instance);
    const Texture2D<float4> source = ResourceDescriptorHeap[display.source];
    float4 value = source.SampleLevel(heapSampler(display.sampler), input.uv, 0.0);
    if (display.isDepth != 0)
    {
        value = float4(value.rrr, 1.0); // one channel: the depth itself
    }
    const float4 mapped = saturate((value - display.rangeMin) / max(display.rangeMax - display.rangeMin, 1e-12));
    const bool4 shown = bool4((display.channels & 1u) != 0, (display.channels & 2u) != 0, (display.channels & 4u) != 0,
                              (display.channels & 8u) != 0);
    const uint count = countbits(display.channels & 15u);
    if (count == 1)
    {
        // One channel: as grey, so a roughness or alpha channel reads like a picture.
        const float grey = dot(mapped, float4(shown));
        return float4(grey, grey, grey, 1.0);
    }
    return float4(shown.r ? mapped.r : 0.0, shown.g ? mapped.g : 0.0, shown.b ? mapped.b : 0.0, 1.0);
}
