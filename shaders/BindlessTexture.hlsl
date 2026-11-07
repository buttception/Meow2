// Fills the target from a texture and a sampler picked by index: a full-screen triangle with no vertex data.
// g_push.userData is the texture's heap index, g_push.instance the sampler's.
#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    Texture2D<float4> image = ResourceDescriptorHeap[g_push.userData];
    return image.Sample(heapSampler(g_push.instance), input.uv);
}
