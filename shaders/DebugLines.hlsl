/// Meow2Renderer's debug lines (never in games): a line list in world space, drawn over the scene with a line-list
/// pipeline (PrimitiveTopology::Lines). g_push.userData is the heap index of a buffer holding the view-projection
/// matrix (four rows, 64 bytes), then one LineVertex (32 bytes) per vertex: three per filled triangle (drawn first,
/// with alpha blending), then two per line. g_push.instance is the first vertex of a draw.

#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    const ByteAddressBuffer lines = heapBuffer(g_push.userData);
    const float4x4 viewProjection =
        float4x4(lines.Load<float4>(0), lines.Load<float4>(16), lines.Load<float4>(32), lines.Load<float4>(48));
    const uint vertex = 64 + (vertexId + g_push.instance) * 32; // g_push.instance: where this draw's vertices start
    VertexOutput output;
    output.position = mul(float4(lines.Load<float4>(vertex).xyz, 1.0), viewProjection);
    output.color = lines.Load<float4>(vertex + 16);
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    return input.color;
}
