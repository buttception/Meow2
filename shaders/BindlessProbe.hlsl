// Proves the bindless contract compiles for both backends: a vertex shader that pulls its instance from a heap buffer,
// a pixel shader that samples a texture picked by index, and a compute shader that writes through the heap.
#include "Common.hlsli"

SamplerState g_sampler : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    const FrameConstants frame = frameConstants();
    const InstanceData instance = instanceData();
    const float4 local = float4(float(vertexId & 1), float(vertexId >> 1), 0.0, 1.0);
    VertexOutput output;
    output.position = mul(mul(local, instance.world), frame.viewProjection);
    output.uv = local.xy;
    return output;
}

float4 psMain(VertexOutput input) : SV_Target
{
    Texture2D<float4> albedo = ResourceDescriptorHeap[g_push.userData];
    return albedo.Sample(g_sampler, input.uv);
}

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    RWByteAddressBuffer output = ResourceDescriptorHeap[g_push.userData];
    const InstanceData instance = loadElement<InstanceData>(g_push.instanceBuffer, id.x);
    output.Store<uint>(id.x * sizeof(uint), instance.material);
}
