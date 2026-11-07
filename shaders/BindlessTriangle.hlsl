// A triangle whose vertices come from a buffer in the heap: the smallest shader that goes through the whole bindless
// path. g_push.userData is the heap index of three TriangleVertex.
#include "Common.hlsli"

struct TriangleVertex
{
    float4 position;
    float4 color;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    const TriangleVertex vertex = loadElement<TriangleVertex>(g_push.userData, vertexId);
    VertexOutput output;
    output.position = vertex.position;
    output.color = vertex.color;
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    return input.color;
}
