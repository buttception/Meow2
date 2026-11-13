/// Tests of pipeline and draw state (RhiDrawTests.cpp): a triangle covering the whole target, in the colour packed into
/// g_push.userData (RGBA8, red in the low byte).

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    // One triangle twice the size of the screen covers all of it.
    const float2 positions[3] = {float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)};
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.5, 1.0);
    const uint packed = g_push.userData;
    output.color = float4(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255, packed >> 24) / 255.0;
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    return input.color;
}

/// A horizontal line through row 31 of a 64-pixel target (y = 1/64 in clip space, the row's centre), from x = -0.8 to
/// 0.8: for line-list pipelines (PrimitiveTopology::Lines).
VertexOutput vsLine(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(vertexId == 0 ? -0.8 : 0.8, 1.0 / 64.0, 0.5, 1.0);
    const uint packed = g_push.userData;
    output.color = float4(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255, packed >> 24) / 255.0;
    return output;
}
