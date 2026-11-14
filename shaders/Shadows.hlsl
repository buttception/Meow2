/// The sun's shadow maps: each frame, a cascade's map starts as a copy of its cached map of static casters (drawn only
/// when the cascade moved, the sun turned or a static object changed), then the movable casters are drawn over it.
/// This copy is the full-screen pass that does it: psCopyDepth writes, as depth, the cached map's value at each texel.
///
/// g_push.userData is the heap index of the cached map (D32, read as one float).

#include "Common.hlsli"

struct FullscreenOutput
{
    float4 position : SV_Position;
};

/// One triangle that covers the target.
FullscreenOutput vsFullscreen(uint vertexId : SV_VertexID)
{
    FullscreenOutput output;
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

/// The cached map's depth, texel for texel (the two maps are the same size).
float psCopyDepth(FullscreenOutput input) : SV_Depth
{
    const Texture2D<float> cached = ResourceDescriptorHeap[g_push.userData];
    return cached.Load(int3(input.position.xy, 0));
}
