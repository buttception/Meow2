/// Meow2Renderer's debug lines (never in games): a line list in world space, drawn over the scene with a line-list
/// pipeline (PrimitiveTopology::Lines). g_push.userData is the heap index of a buffer holding the view-projection
/// matrix (four rows, 64 bytes), then one LineVertex (32 bytes) per vertex, two per line.

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
    const uint vertex = 64 + vertexId * 32;
    VertexOutput output;
    output.position = mul(float4(lines.Load<float4>(vertex).xyz, 1.0), viewProjection);
    output.color = lines.Load<float4>(vertex + 16);
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    return input.color;
}
